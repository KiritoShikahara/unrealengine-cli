use std::ffi::OsStr;
use std::path::Path;
use std::time::{Duration, Instant};

use serde_json::{Value, json};

use super::locate::Engine;
use super::spawn;
use crate::core::error::CliError;
use crate::core::paths;
use crate::instances::{self, Instance};
use crate::transport::client::BridgeClient;

pub fn running_for(uproject: &Path) -> Option<Instance> {
    instances::list()
        .into_iter()
        .find(|i| paths::same_path(Path::new(&i.project), uproject))
}

/// Starts the editor detached and waits until its bridge answers a ping.
impl WindowMode {
    /// Mode recorded in an instance file.
    pub fn from_instance(mode: &str) -> Self {
        match mode {
            "headless" => Self::Headless,
            "nullrhi" => Self::NullRhi,
            _ => Self::Normal,
        }
    }
}

/// How the editor window is shown.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WindowMode {
    Normal,
    /// No window, offscreen rendering.
    Headless,
    /// No rendering at all (fast, but mesh actors can crash the editor).
    NullRhi,
}

/// Editor command-line switches for a window mode. Offscreen rendering keeps a real RHI (mesh actors
/// need it); -nullrhi is opt-in and also needs -RenderOffscreen, or window code hits a fatal error.
pub fn mode_args(mode: WindowMode) -> Vec<&'static str> {
    match mode {
        WindowMode::Normal => vec![],
        WindowMode::Headless => vec!["-RenderOffscreen", "-nosound"],
        WindowMode::NullRhi => vec!["-nullrhi", "-RenderOffscreen", "-nosound"],
    }
}

pub fn open(
    engine: &Engine,
    uproject: &Path,
    mode: WindowMode,
    wait: Duration,
) -> Result<Value, CliError> {
    if let Some(i) = running_for(uproject) {
        return Ok(
            json!({ "alreadyRunning": true, "pid": i.pid, "engineVersion": i.engine_version }),
        );
    }
    let exe = engine.editor_exe();
    if !exe.is_file() {
        return Err(CliError::usage(
            "engine.no_editor",
            format!("{} not found", exe.display()),
        ));
    }
    let mut args: Vec<&OsStr> = vec![
        uproject.as_os_str(),
        OsStr::new("-nosplash"),
        OsStr::new("-unattended"),
    ];
    args.extend(mode_args(mode).into_iter().map(OsStr::new));
    let pid = spawn::spawn_detached(&exe, &args)?;
    let started = Instant::now();
    while started.elapsed() < wait {
        std::thread::sleep(Duration::from_millis(1000));
        if let Some(i) = running_for(uproject)
            && let Ok(mut c) = BridgeClient::connect(i.addr(), &i.token, Duration::from_secs(5))
            && let Ok(p) = c.call("system_ping", json!({}), false, 5000)
            // Older plugins do not report pythonReady; treat a missing field as ready.
            && p["pythonReady"] != false
        {
            return Ok(json!({
                "alreadyRunning": false,
                "pid": i.pid,
                "engineVersion": i.engine_version,
                "secondsToReady": started.elapsed().as_secs(),
            }));
        }
    }
    Err(CliError::timeout(
        "editor.start_timeout",
        format!(
            "editor (pid {pid}) did not answer within {}s",
            wait.as_secs()
        ),
    )
    .with_hint(
        "first launches compile shaders; retry with a larger --wait, or check: unrealengine-cli doctor",
    ))
}

/// Asks the editor to quit and waits for its instance to disappear. With `force`, an editor that does not
/// answer (game thread stuck) or does not exit is terminated with its child processes; unsaved work is lost.
pub fn close(inst: &Instance, force: bool, timeout: Duration) -> Result<Value, CliError> {
    // A responsive editor answers at once; cap the wait so a stuck one is detected quickly
    // even when a long --timeout-ms was given for the surrounding command.
    let timeout = timeout.min(Duration::from_secs(30));
    let quit = BridgeClient::connect(inst.addr(), &inst.token, timeout).and_then(|mut c| {
        c.call(
            "editor_quit",
            json!({ "force": force }),
            false,
            timeout.as_millis() as u64,
        )
    });
    let data = match quit {
        Ok(d) => d,
        Err(e) if e.code == "editor.unsaved_changes" => return Err(e),
        Err(_) if force => return terminate(inst),
        Err(e) => {
            return Err(e.with_hint(
                "the editor does not answer; `unrealengine-cli editor close --force` ends the process (unsaved work is lost)",
            ));
        }
    };
    let started = Instant::now();
    let mut shutting_down_since: Option<Instant> = None;
    while started.elapsed() < Duration::from_secs(90) {
        // The instance file goes away early in shutdown; the process (and its DLL locks) a little later.
        if !instances::list().iter().any(|i| i.pid == inst.pid) {
            if !process_alive(inst.pid) {
                return Ok(
                    json!({ "closed": true, "pid": inst.pid, "discarded": data["discarded"] }),
                );
            }
            // Past the point of saving: an editor that hangs while shutting down is ended.
            let since = *shutting_down_since.get_or_insert_with(Instant::now);
            if since.elapsed() > Duration::from_secs(30) {
                return terminate(inst);
            }
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    if force {
        return terminate(inst);
    }
    Err(CliError::timeout(
        "editor.close_timeout",
        format!("editor {} is still running", inst.pid),
    )
    .with_hint("`unrealengine-cli editor close --force` ends the process"))
}

fn terminate(inst: &Instance) -> Result<Value, CliError> {
    kill_process_tree(inst.pid);
    let started = Instant::now();
    while started.elapsed() < Duration::from_secs(30) {
        if !process_alive(inst.pid) {
            let _ = std::fs::remove_file(
                crate::core::paths::instances_dir().join(format!("{}.json", inst.pid)),
            );
            return Ok(json!({ "closed": true, "pid": inst.pid, "terminated": true }));
        }
        std::thread::sleep(Duration::from_millis(300));
    }
    Err(CliError::editor(
        "editor.kill_failed",
        format!("could not end editor process {}", inst.pid),
    ))
}

/// Ends a process and its children (Live Coding console, shader workers).
pub fn kill_process_tree(pid: u32) {
    let pid = pid.to_string();
    let _ = if cfg!(windows) {
        std::process::Command::new("taskkill")
            .args(["/PID", &pid, "/T", "/F"])
            .output()
    } else {
        std::process::Command::new("kill")
            .args(["-9", &pid])
            .output()
    };
}

/// True while a process with this id exists.
pub fn process_alive(pid: u32) -> bool {
    instances::process_alive(pid)
}

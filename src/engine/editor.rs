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
    // Offscreen rendering keeps a real RHI (mesh actors need it); -nullrhi is opt-in.
    match mode {
        WindowMode::Normal => {}
        WindowMode::Headless => {
            args.extend([OsStr::new("-RenderOffscreen"), OsStr::new("-nosound")])
        }
        WindowMode::NullRhi => args.extend([OsStr::new("-nullrhi"), OsStr::new("-nosound")]),
    }
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
        "first launches compile shaders; retry with a larger --wait, or check: unreal-cli doctor",
    ))
}

/// Asks the editor to quit and waits for its instance to disappear.
pub fn close(inst: &Instance, force: bool, timeout: Duration) -> Result<Value, CliError> {
    let mut c = BridgeClient::connect(inst.addr(), &inst.token, timeout)?;
    let data = c.call(
        "editor_quit",
        json!({ "force": force }),
        false,
        timeout.as_millis() as u64,
    )?;
    drop(c);
    let started = Instant::now();
    while started.elapsed() < Duration::from_secs(60) {
        if !instances::list().iter().any(|i| i.pid == inst.pid) {
            return Ok(json!({ "closed": true, "pid": inst.pid, "discarded": data["discarded"] }));
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    Err(CliError::timeout(
        "editor.close_timeout",
        format!("editor {} is still running", inst.pid),
    ))
}

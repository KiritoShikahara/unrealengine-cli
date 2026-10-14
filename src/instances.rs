use std::net::{SocketAddr, TcpStream};
use std::path::{Path, PathBuf};
use std::time::Duration;

use serde::{Deserialize, Serialize};

use crate::core::error::CliError;
use crate::core::paths;

/// Contents of `<data dir>/instances/<pid>.json`, written by the editor plugin.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Instance {
    pub pid: u32,
    pub port: u16,
    #[serde(skip_serializing)]
    pub token: String,
    pub project: String,
    pub engine_version: String,
    pub plugin_version: String,
    pub protocol_version: u32,
    pub started_at: String,
    /// normal | headless | nullrhi (older plugins omit it)
    #[serde(default)]
    pub mode: String,
}

impl Instance {
    pub fn addr(&self) -> SocketAddr {
        SocketAddr::from(([127, 0, 0, 1], self.port))
    }

    fn reachable(&self) -> bool {
        TcpStream::connect_timeout(&self.addr(), Duration::from_millis(300)).is_ok()
    }
}

/// True while a process with this id exists (fast: no child process).
pub fn process_alive(pid: u32) -> bool {
    #[cfg(windows)]
    {
        use windows_sys::Win32::Foundation::{CloseHandle, ERROR_ACCESS_DENIED, GetLastError};
        use windows_sys::Win32::System::Threading::{
            GetExitCodeProcess, OpenProcess, PROCESS_QUERY_LIMITED_INFORMATION,
        };
        const STILL_ACTIVE: u32 = 259;
        // SAFETY: plain Win32 calls on a process id; the handle is closed before returning.
        unsafe {
            let h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
            if h.is_null() {
                return GetLastError() == ERROR_ACCESS_DENIED;
            }
            let mut code = 0u32;
            let ok = GetExitCodeProcess(h, &mut code) != 0;
            CloseHandle(h);
            ok && code == STILL_ACTIVE
        }
    }
    #[cfg(not(windows))]
    {
        std::process::Command::new("kill")
            .args(["-0", &pid.to_string()])
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
    }
}

fn age(path: &Path) -> Duration {
    std::fs::metadata(path)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.elapsed().ok())
        .unwrap_or_default()
}

/// Live instances. A file is removed only when its editor is gone: the process has exited, or the file has not
/// been refreshed (the plugin rewrites it every 10 s) for two minutes. A busy editor that does not accept a
/// connection right now keeps its file; files still being written are left alone.
pub fn list() -> Vec<Instance> {
    let dir = paths::instances_dir();
    let Ok(entries) = std::fs::read_dir(&dir) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for path in entries.flatten().map(|e| e.path()) {
        if path.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }
        let parsed = std::fs::read_to_string(&path)
            .ok()
            .and_then(|s| serde_json::from_str::<Instance>(&s).ok());
        match parsed {
            Some(i) if i.reachable() => out.push(i),
            Some(i) => {
                if !process_alive(i.pid) || age(&path) > Duration::from_secs(120) {
                    let _ = std::fs::remove_file(&path);
                }
            }
            None => {
                if age(&path) > Duration::from_secs(10) {
                    let _ = std::fs::remove_file(&path);
                }
            }
        }
    }
    out.sort_by_key(|i| i.pid);
    out
}

fn default_file() -> PathBuf {
    paths::data_dir().join("default-instance")
}

/// Remembers a pid for later commands (`instances use`).
pub fn set_default(pid: u32) -> Result<(), CliError> {
    std::fs::create_dir_all(paths::data_dir())
        .and_then(|_| std::fs::write(default_file(), pid.to_string()))
        .map_err(|e| CliError::internal("io.write", e.to_string()))
}

fn default_pid() -> Option<u32> {
    std::fs::read_to_string(default_file())
        .ok()?
        .trim()
        .parse()
        .ok()
}

/// Picks the editor to talk to: --instance, then --project / cwd project, then the remembered
/// default, then the only running instance.
pub fn select(pid: Option<u32>, project: Option<&Path>, cwd: &Path) -> Result<Instance, CliError> {
    let mut all = list();
    let wanted = match (pid, project) {
        (Some(_), _) => None,
        (None, Some(p)) => Some(paths::find_uproject(Some(p), cwd)?),
        (None, None) => paths::find_uproject(None, cwd).ok(),
    };
    // A running editor rewrites its instance file every 10 s; a read during the rename misses it.
    for _ in 0..5 {
        let found = match (pid, &wanted) {
            (Some(pid), _) => all.iter().any(|i| i.pid == pid),
            (None, Some(w)) => all
                .iter()
                .any(|i| paths::same_path(Path::new(&i.project), w)),
            (None, None) => !all.is_empty(),
        };
        if found {
            break;
        }
        std::thread::sleep(Duration::from_millis(200));
        all = list();
    }
    if let Some(pid) = pid {
        return all.into_iter().find(|i| i.pid == pid).ok_or_else(|| {
            CliError::connect(
                "connect.no_instance",
                format!("no running editor with pid {pid}"),
            )
            .with_hint("list editors: unrealengine-cli instances list")
        });
    }
    if all.is_empty() {
        return Err(no_instance());
    }
    if let Some(w) = &wanted {
        if let Some(i) = all
            .iter()
            .find(|i| paths::same_path(Path::new(&i.project), w))
        {
            return Ok(i.clone());
        }
        if project.is_some() {
            return Err(CliError::connect(
                "connect.no_instance",
                format!("no running editor has {} open", w.display()),
            )
            .with_hint("start it: unrealengine-cli editor open"));
        }
    }
    if let Some(d) = default_pid()
        && let Some(i) = all.iter().find(|i| i.pid == d)
    {
        return Ok(i.clone());
    }
    // Never drive another project's editor just because it is the only one running.
    if let Some(w) = &wanted {
        return Err(CliError::connect(
            "connect.no_instance",
            format!("no running editor has {} open", w.display()),
        )
        .with_hint(
            "start it: unrealengine-cli editor open (or pick another editor with --instance <pid>)",
        ));
    }
    if all.len() == 1 {
        return Ok(all.into_iter().next().unwrap());
    }
    let pids: Vec<String> = all
        .iter()
        .map(|i| format!("{} ({})", i.pid, i.project))
        .collect();
    Err(CliError::usage(
        "instances.ambiguous",
        format!("several editors are running: {}", pids.join(", ")),
    )
    .with_hint("pick one with --instance <pid> or --project <path>"))
}

pub fn no_instance() -> CliError {
    CliError::connect(
        "connect.no_instance",
        "no running Unreal Editor with UnrealCliBridge was found",
    )
    .with_hint(
        "start the editor: unrealengine-cli editor open (first time: unrealengine-cli setup)",
    )
}

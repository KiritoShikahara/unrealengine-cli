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

/// Live instances; files whose editor no longer listens are removed.
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
            _ => {
                let _ = std::fs::remove_file(&path);
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
    let all = list();
    if let Some(pid) = pid {
        return all.into_iter().find(|i| i.pid == pid).ok_or_else(|| {
            CliError::connect(
                "connect.no_instance",
                format!("no running editor with pid {pid}"),
            )
            .with_hint("list editors: unreal-cli instances list")
        });
    }
    if all.is_empty() {
        return Err(no_instance());
    }
    let wanted = match project {
        Some(p) => Some(paths::find_uproject(Some(p), cwd)?),
        None => paths::find_uproject(None, cwd).ok(),
    };
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
            .with_hint("start it: unreal-cli editor open"));
        }
    }
    if let Some(d) = default_pid()
        && let Some(i) = all.iter().find(|i| i.pid == d)
    {
        return Ok(i.clone());
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
    .with_hint("start the editor: unreal-cli editor open (first time: unreal-cli setup)")
}

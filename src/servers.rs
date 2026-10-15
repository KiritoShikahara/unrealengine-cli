//! Records of dedicated servers started by `server start` (`<data dir>/servers/<pid>.json`).
//! `server stop` only ends processes recorded here.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::core::paths;
use crate::instances;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct ServerRecord {
    pub pid: u32,
    pub project: PathBuf,
    pub executable: PathBuf,
    pub map: Option<String>,
    pub port: u16,
    pub log_file: PathBuf,
    /// Unix seconds.
    pub started_at: u64,
}

pub fn dir() -> PathBuf {
    paths::data_dir().join("servers")
}

fn file(pid: u32) -> PathBuf {
    dir().join(format!("{pid}.json"))
}

pub fn save(r: &ServerRecord) -> std::io::Result<()> {
    std::fs::create_dir_all(dir())?;
    std::fs::write(file(r.pid), serde_json::to_string_pretty(r)?)
}

pub fn load(pid: u32) -> Option<ServerRecord> {
    let text = std::fs::read_to_string(file(pid)).ok()?;
    serde_json::from_str(&text).ok()
}

pub fn remove(pid: u32) {
    let _ = std::fs::remove_file(file(pid));
}

/// Recorded servers whose process is still running; records of exited ones are deleted.
pub fn list_live() -> Vec<ServerRecord> {
    let mut out = Vec::new();
    for e in std::fs::read_dir(dir()).into_iter().flatten().flatten() {
        let Ok(text) = std::fs::read_to_string(e.path()) else {
            continue;
        };
        let Ok(r) = serde_json::from_str::<ServerRecord>(&text) else {
            continue;
        };
        if instances::process_alive(r.pid) {
            out.push(r);
        } else {
            let _ = std::fs::remove_file(e.path());
        }
    }
    out.sort_by_key(|r| r.pid);
    out
}

/// Last `n` lines of a log file (empty when unreadable).
pub fn log_tail(path: &Path, n: usize) -> Vec<String> {
    let bytes = std::fs::read(path).unwrap_or_default();
    let text = String::from_utf8_lossy(&bytes);
    let lines: Vec<&str> = text.lines().collect();
    lines[lines.len().saturating_sub(n)..]
        .iter()
        .map(|l| l.to_string())
        .collect()
}

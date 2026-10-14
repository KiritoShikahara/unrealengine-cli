#![allow(dead_code)]
pub mod mock_bridge;

use std::path::Path;

use assert_cmd::Command;
use serde_json::{Value, json};

/// Writes an instance file pointing at a mock bridge (pid = this test process, so it looks alive).
pub fn write_instance(home: &Path, bridge: &mock_bridge::MockBridge, project: &str) {
    let dir = home.join("instances");
    std::fs::create_dir_all(&dir).unwrap();
    let pid = std::process::id();
    let body = json!({
        "pid": pid, "port": bridge.addr.port(), "token": bridge.token, "project": project,
        "engineVersion": "5.7.1", "pluginVersion": "0.1.0", "protocolVersion": 1,
        "startedAt": "2026-10-14T00:00:00Z"
    });
    std::fs::write(dir.join(format!("{pid}.json")), body.to_string()).unwrap();
}

/// `unreal-cli --output json ...` with an isolated data dir; returns (exit code, envelope).
pub fn run_json(home: &Path, cwd: &Path, args: &[&str]) -> (i32, Value) {
    let out = Command::cargo_bin("unreal-cli")
        .unwrap()
        .env("UNREAL_CLI_HOME", home)
        .env("UNREAL_CLI_NO_AUTO_UPDATE", "1")
        .current_dir(cwd)
        .arg("--output")
        .arg("json")
        .args(args)
        .output()
        .unwrap();
    let code = out.status.code().unwrap_or(-1);
    let text = String::from_utf8_lossy(&out.stdout);
    let v = serde_json::from_str(&text).unwrap_or_else(|e| {
        panic!(
            "stdout is not an envelope ({e}): {text}\nstderr: {}",
            String::from_utf8_lossy(&out.stderr)
        )
    });
    (code, v)
}

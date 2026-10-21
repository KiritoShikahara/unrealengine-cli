mod support;

use assert_cmd::Command;
use serde_json::{Value, json};
use std::path::Path;
use support::run_json;

fn session_start(cwd: &Path) -> (i32, String) {
    let out = Command::cargo_bin("unrealengine-cli")
        .unwrap()
        .env("UNREALENGINE_CLI_NO_AUTO_UPDATE", "1")
        .current_dir(cwd)
        .args(["hook", "session-start"])
        .write_stdin(r#"{"hook_event_name":"SessionStart","source":"startup"}"#)
        .output()
        .unwrap();
    (
        out.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&out.stdout).to_string(),
    )
}

#[test]
fn session_start_reports_the_project_from_a_subdirectory() {
    let proj = tempfile::tempdir().unwrap();
    std::fs::write(
        proj.path().join("MyGame.uproject"),
        r#"{"FileVersion":3,"EngineAssociation":"5.7"}"#,
    )
    .unwrap();
    let sub = proj.path().join("Source/MyGame");
    std::fs::create_dir_all(&sub).unwrap();
    let (code, out) = session_start(&sub);
    assert_eq!(code, 0);
    let v: Value = serde_json::from_str(&out).expect(&out);
    assert_eq!(v["hookSpecificOutput"]["hookEventName"], "SessionStart");
    let ctx = v["hookSpecificOutput"]["additionalContext"]
        .as_str()
        .unwrap();
    assert!(ctx.contains("MyGame"), "{ctx}");
    assert!(ctx.contains("5.7"), "{ctx}");
    assert!(ctx.contains("unrealengine-cli"), "{ctx}");
}

#[test]
fn session_start_is_silent_outside_a_project() {
    let dir = tempfile::tempdir().unwrap();
    let (code, out) = session_start(dir.path());
    assert_eq!(code, 0);
    assert!(out.trim().is_empty(), "{out}");
}

fn read(p: &Path) -> Value {
    serde_json::from_str(&std::fs::read_to_string(p).unwrap()).unwrap()
}

fn hook_commands(v: &Value) -> Vec<String> {
    v["hooks"]["SessionStart"]
        .as_array()
        .into_iter()
        .flatten()
        .flat_map(|g| g["hooks"].as_array().cloned().unwrap_or_default())
        .filter_map(|h| h["command"].as_str().map(String::from))
        .collect()
}

#[test]
fn install_merges_into_existing_settings_and_is_idempotent() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    let settings = proj.path().join(".claude/settings.json");
    std::fs::create_dir_all(settings.parent().unwrap()).unwrap();
    let existing = json!({
        "permissions": {"allow": ["Bash(ls)"]},
        "hooks": {"SessionStart": [{"matcher": "startup", "hooks": [{"type": "command", "command": "echo hi"}]}]}
    });
    std::fs::write(&settings, existing.to_string()).unwrap();

    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &["hook", "install", "claude-code"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["changed"], true);
    let after = read(&settings);
    assert_eq!(after["permissions"], existing["permissions"]);
    let cmds = hook_commands(&after);
    assert!(cmds.contains(&"echo hi".to_string()));
    assert_eq!(
        cmds.iter()
            .filter(|c| c.ends_with("hook session-start"))
            .count(),
        1,
        "{cmds:?}"
    );

    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &["hook", "install", "claude-code"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["changed"], false);
    assert_eq!(read(&settings), after);
}

#[test]
fn install_dry_run_writes_nothing() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &["--dry-run", "hook", "install", "claude-code"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["changed"], true);
    assert!(!proj.path().join(".claude/settings.json").exists());
}

#[test]
fn install_refuses_invalid_settings() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    let settings = proj.path().join(".claude/settings.json");
    std::fs::create_dir_all(settings.parent().unwrap()).unwrap();
    std::fs::write(&settings, "{ not json").unwrap();
    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &["hook", "install", "claude-code"],
    );
    assert_ne!(code, 0);
    assert_eq!(v["error"]["code"], "hook.settings_invalid");
    assert_eq!(std::fs::read_to_string(&settings).unwrap(), "{ not json");
}

#[test]
fn install_rejects_unknown_client() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    let (code, v) = run_json(home.path(), proj.path(), &["hook", "install", "vim"]);
    assert_ne!(code, 0);
    assert_eq!(v["error"]["code"], "hook.unknown_client");
}

mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

#[test]
fn ping_without_editor_fails_with_connect_exit_and_hint() {
    let home = tempfile::tempdir().unwrap();
    let (code, v) = run_json(home.path(), home.path(), &["ping"]);
    assert_eq!(code, 3);
    assert_eq!(v["ok"], false);
    assert_eq!(v["error"]["code"], "connect.no_instance");
    assert!(v["error"]["hint"].as_str().unwrap().contains("editor open"));
}

#[test]
fn ping_reaches_the_only_instance() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("system_ping", |_| {
            Ok(json!({"pong": true, "engineVersion": "5.7.1"}))
        })
        .start();
    write_instance(home.path(), &m, "C:/Proj/Mock.uproject");
    let (code, v) = run_json(home.path(), home.path(), &["ping"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["pong"], true);
    assert!(v["data"]["rttMs"].is_u64());
    assert_eq!(v["meta"]["instance"], std::process::id().to_string());
}

#[test]
fn instances_list_shows_live_and_prunes_stale() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new().start();
    write_instance(home.path(), &m, "C:/Proj/Mock.uproject");
    let stale = home.path().join("instances").join("1.json");
    std::fs::write(
        &stale,
        json!({"pid":1,"port":1,"token":"x","project":"p","engineVersion":"5.7",
        "pluginVersion":"0","protocolVersion":1,"startedAt":""})
        .to_string(),
    )
    .unwrap();
    let (code, v) = run_json(home.path(), home.path(), &["instances", "list"]);
    assert_eq!(code, 0);
    let list = v["data"].as_array().unwrap();
    assert_eq!(list.len(), 1);
    assert!(list[0].get("token").is_none(), "token must not be printed");
    assert!(!stale.exists());
}

#[test]
fn busy_live_editors_keep_their_instance_file() {
    // An editor that is alive but does not accept a connection right now (busy) must not lose its file.
    let home = tempfile::tempdir().unwrap();
    let dir = home.path().join("instances");
    std::fs::create_dir_all(&dir).unwrap();
    let pid = std::process::id();
    let busy = dir.join(format!("{pid}.json"));
    std::fs::write(
        &busy,
        json!({"pid":pid,"port":1,"token":"x","project":"p","engineVersion":"5.7",
        "pluginVersion":"0","protocolVersion":1,"startedAt":""})
        .to_string(),
    )
    .unwrap();
    // A file being written (not valid JSON yet) is left alone too.
    let partial = dir.join("999999.json");
    std::fs::write(&partial, "{\"pid\":99").unwrap();
    let (code, _) = run_json(home.path(), home.path(), &["instances", "list"]);
    assert_eq!(code, 0);
    assert!(busy.exists(), "live editor's file was deleted");
    assert!(partial.exists(), "a file being written was deleted");
}

#[test]
fn logs_forwards_filters() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("editor_logs", |a| {
            Ok(json!({"lines": [], "nextSince": 7, "echo": a}))
        })
        .start();
    write_instance(home.path(), &m, "C:/Proj/Mock.uproject");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &[
            "logs",
            "--since",
            "5",
            "--severity",
            "warning",
            "--category",
            "LogBlueprint",
        ],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(
        v["data"]["echo"],
        json!({"since": 5, "severity": "warning", "category": "LogBlueprint"})
    );
}

#[test]
fn cwd_project_never_falls_back_to_another_projects_editor() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("system_ping", |_| Ok(json!({"pong": true})))
        .start();
    write_instance(home.path(), &m, "C:/Other/Other.uproject");
    let proj = tempfile::tempdir().unwrap();
    std::fs::write(proj.path().join("Game.uproject"), "{}").unwrap();
    let (code, v) = run_json(home.path(), proj.path(), &["ping"]);
    assert_eq!(code, 3, "{v}");
    assert_eq!(v["error"]["code"], "connect.no_instance");
    assert!(
        v["error"]["message"]
            .as_str()
            .unwrap()
            .contains("Game.uproject"),
        "{v}"
    );
    let pid = std::process::id().to_string();
    let (code, v) = run_json(home.path(), proj.path(), &["--instance", &pid, "ping"]);
    assert_eq!(code, 0, "an explicit --instance still works: {v}");
}

#[test]
fn project_flag_selects_matching_instance() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    let up = proj.path().join("Game.uproject");
    std::fs::write(&up, "{}").unwrap();
    let m = MockBuilder::new()
        .tool("system_ping", |_| Ok(json!({"pong": true})))
        .start();
    write_instance(home.path(), &m, up.to_str().unwrap());
    let (code, v) = run_json(home.path(), proj.path(), &["ping"]);
    assert_eq!(code, 0, "{v}");

    let other = tempfile::tempdir().unwrap();
    std::fs::write(other.path().join("Other.uproject"), "{}").unwrap();
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["ping", "--project", other.path().to_str().unwrap()],
    );
    assert_eq!(code, 3);
    assert_eq!(v["error"]["code"], "connect.no_instance");
}

#[test]
fn tool_call_maps_args_errors_to_usage_exit() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("bp_get", |_| {
            Err(json!({"code": "args.invalid", "message": "missing required argument 'path'"}))
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["tool", "call", "bp_get", "--args", "{}"],
    );
    assert_eq!(code, 2, "{v}");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["tool", "call", "bp_get", "--args", "[1]"],
    );
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "args.invalid_json");
}

#[test]
fn batch_stops_at_first_failure() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("a", |_| Ok(json!(1)))
        .tool("b", |_| Err(json!({"code": "x.fail", "message": "no"})))
        .start();
    write_instance(home.path(), &m, "p");
    let file = home.path().join("b.json");
    std::fs::write(&file, r#"[{"tool":"a"},{"tool":"b"},{"tool":"a"}]"#).unwrap();
    let (code, v) = run_json(home.path(), home.path(), &["batch", file.to_str().unwrap()]);
    assert_eq!(code, 4);
    assert_eq!(v["error"]["details"]["failedStep"], 1);
    assert_eq!(m.calls.lock().unwrap().len(), 2);
}

#[test]
fn parse_errors_are_envelopes_in_json_mode() {
    let home = tempfile::tempdir().unwrap();
    let (code, v) = run_json(home.path(), home.path(), &["blueprint", "create"]);
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "args.parse");
}

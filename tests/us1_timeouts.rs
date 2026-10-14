mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

#[test]
fn slow_editor_maps_to_timeout_exit_5() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new().slow_tool("system_ping", 3000).start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(home.path(), home.path(), &["--timeout-ms", "500", "ping"]);
    assert_eq!(code, 5, "{v}");
    assert_eq!(v["error"]["code"], "connect.timeout");
    assert!(v["error"]["hint"].is_string());
}

#[test]
fn editor_side_timeout_code_maps_to_exit_5() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("pie_run", |_| {
            Err(json!({"code": "tool.timeout", "message": "did not finish"}))
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, _) = run_json(home.path(), home.path(), &["tool", "call", "pie_run"]);
    assert_eq!(code, 5);
}

#[test]
fn instances_use_remembers_and_validates() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("system_ping", |_| Ok(json!({"pong": true})))
        .start();
    write_instance(home.path(), &m, "p");
    let pid = std::process::id().to_string();
    let (code, v) = run_json(home.path(), home.path(), &["instances", "use", &pid]);
    assert_eq!(code, 0, "{v}");
    assert!(home.path().join("default-instance").is_file());
    let (code, v) = run_json(home.path(), home.path(), &["instances", "use", "1"]);
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "instances.not_found");
    let (code, _) = run_json(home.path(), home.path(), &["--instance", &pid, "ping"]);
    assert_eq!(code, 0);
    let (code, v) = run_json(home.path(), home.path(), &["--instance", "1", "ping"]);
    assert_eq!(code, 3);
    assert_eq!(v["error"]["code"], "connect.no_instance");
}

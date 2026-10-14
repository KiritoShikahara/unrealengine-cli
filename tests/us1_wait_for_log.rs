mod support;

use std::sync::Arc;
use std::sync::atomic::{AtomicU32, Ordering};

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

#[test]
fn waits_until_a_matching_line_appears() {
    let home = tempfile::tempdir().unwrap();
    let calls = Arc::new(AtomicU32::new(0));
    let c = calls.clone();
    let m = MockBuilder::new()
        .tool("editor_logs", move |_| {
            let n = c.fetch_add(1, Ordering::SeqCst);
            let lines = if n >= 3 {
                json!([{"seq": 10, "text": "LiveCoding: patch DONE"}])
            } else {
                json!([])
            };
            Ok(json!({ "lines": lines, "nextSince": 10 + n }))
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["logs", "--wait-for", "DONE", "--wait-seconds", "10"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["matched"]["text"], "LiveCoding: patch DONE");
    assert!(calls.load(Ordering::SeqCst) >= 4);
}

#[test]
fn gives_up_with_timeout_exit() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("editor_logs", |_| {
            Ok(json!({ "lines": [], "nextSince": 1 }))
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["logs", "--wait-for", "never", "--wait-seconds", "1"],
    );
    assert_eq!(code, 5, "{v}");
    assert_eq!(v["error"]["code"], "logs.wait_timeout");
}

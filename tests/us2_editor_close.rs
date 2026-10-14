mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

#[test]
fn close_waits_for_the_instance_to_go_away() {
    let home = tempfile::tempdir().unwrap();
    let file = home
        .path()
        .join("instances")
        .join(format!("{}.json", std::process::id()));
    let f = file.clone();
    let m = MockBuilder::new()
        .tool("editor_quit", move |_| {
            // The real plugin removes its instance file when it shuts down.
            let _ = std::fs::remove_file(&f);
            Ok(json!({"quitting": true, "discarded": []}))
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(home.path(), home.path(), &["editor", "close"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["closed"], true);
    assert!(!file.exists());
}

#[test]
fn close_refuses_unsaved_changes_with_hint() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("editor_quit", |a| {
            if a["force"] == true {
                Ok(json!({"quitting": true}))
            } else {
                Err(json!({"code": "editor.unsaved_changes", "message": "2 unsaved package(s)", "hint": "save first", "details": ["/Game/A", "/Game/B"]}))
            }
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(home.path(), home.path(), &["editor", "close"]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "editor.unsaved_changes");
    assert_eq!(v["error"]["details"], json!(["/Game/A", "/Game/B"]));
    let (_, _) = run_json(
        home.path(),
        home.path(),
        &["--dry-run", "editor", "close", "--force"],
    );
    assert_eq!(
        m.calls.lock().unwrap().len(),
        1,
        "dry-run must not call the editor"
    );
}

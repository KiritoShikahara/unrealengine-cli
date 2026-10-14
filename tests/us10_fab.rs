mod support;

use std::sync::atomic::{AtomicU32, Ordering};

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

#[test]
fn fab_queries_map_to_tools() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("fab_status", |a| Ok(a.clone()))
        .tool("fab_search", |a| Ok(a.clone()))
        .tool("fab_library", |a| Ok(a.clone()))
        .start();
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, serde_json::Value)> = vec![
        (vec!["fab", "status"], json!({})),
        (
            vec!["fab", "search", "rock", "--free", "--count", "5"],
            json!({"query": "rock", "freeOnly": true, "count": 5}),
        ),
        (vec!["fab", "library"], json!({})),
        (vec!["fab", "library", "slash"], json!({"query": "slash"})),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

#[test]
fn fab_add_waits_for_the_import_job() {
    let home = tempfile::tempdir().unwrap();
    let polls = std::sync::Arc::new(AtomicU32::new(0));
    let p = polls.clone();
    let m = MockBuilder::new()
        .tool("fab_add", |a| {
            assert_eq!(a["listing"], "abc");
            assert_eq!(a["format"], "fbx");
            assert_eq!(a["acquireFree"], true);
            Ok(json!({"jobId": "job-7"}))
        })
        .tool("jobs_get", move |a| {
            assert_eq!(a["jobId"], "job-7");
            if p.fetch_add(1, Ordering::SeqCst) < 1 {
                Ok(json!({"state": "running"}))
            } else {
                Ok(json!({"state": "succeeded", "result": {"path": "/Game/Fab/X", "assetCount": 3}}))
            }
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["fab", "add", "abc", "--format", "fbx", "--acquire-free"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["path"], "/Game/Fab/X");
    assert!(polls.load(Ordering::SeqCst) >= 2);
}

#[test]
fn fab_add_reports_the_job_error() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("fab_add", |_| Ok(json!({"jobId": "job-1"})))
        .tool("jobs_get", |_| {
            Ok(json!({"state": "failed", "error": {"code": "fab.not_owned", "message": "\"Rock\" is not in your Fab library"}}))
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(home.path(), home.path(), &["fab", "add", "abc"]);
    assert_ne!(code, 0);
    assert_eq!(v["error"]["code"], "fab.not_owned");
}

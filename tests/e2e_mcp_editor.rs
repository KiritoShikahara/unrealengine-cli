//! MCP server against a running TestProject editor (UNREALENGINE_CLI_E2E=1, see scripts/e2e.sh).

use std::io::Write;
use std::process::{Command, Stdio};

use serde_json::{Value, json};

#[test]
fn mcp_calls_reach_the_editor() {
    if std::env::var("UNREALENGINE_CLI_E2E").as_deref() != Ok("1") {
        eprintln!("skipped: set UNREALENGINE_CLI_E2E=1 with the TestProject editor running");
        return;
    }
    let mut child = Command::new(env!("CARGO_BIN_EXE_unrealengine-cli"))
        .current_dir(concat!(env!("CARGO_MANIFEST_DIR"), "/TestProject"))
        .args(["mcp", "serve"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    {
        let mut stdin = child.stdin.take().unwrap();
        for m in [
            json!({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}),
            json!({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"unreal_ping","arguments":{}}}),
            json!({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"unreal_find_tools","arguments":{"query":"landscape"}}}),
            json!({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"unreal_call","arguments":{"tool":"editor_info"}}}),
        ] {
            writeln!(stdin, "{m}").unwrap();
        }
    }
    let out = child.wait_with_output().unwrap();
    let replies: Vec<Value> = String::from_utf8_lossy(&out.stdout)
        .lines()
        .map(|l| serde_json::from_str(l).unwrap())
        .collect();
    assert_eq!(replies.len(), 4);
    for r in &replies[1..] {
        assert_eq!(r["result"]["isError"], false, "{r}");
    }
    let found: Value =
        serde_json::from_str(replies[2]["result"]["content"][0]["text"].as_str().unwrap()).unwrap();
    assert_eq!(found["source"], "editor");
    assert!(
        found["tools"]
            .as_array()
            .unwrap()
            .iter()
            .any(|t| t["name"] == "landscape_create")
    );
}

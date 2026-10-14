mod support;

use std::io::Write;
use std::process::{Command, Stdio};

use serde_json::{Value, json};
use support::mock_bridge::MockBuilder;
use support::write_instance;

fn session(home: &std::path::Path, lines: &[Value]) -> Vec<Value> {
    let mut child = Command::new(env!("CARGO_BIN_EXE_unrealengine-cli"))
        .env("UNREALENGINE_CLI_HOME", home)
        .current_dir(home)
        .args(["mcp", "serve"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    {
        let mut stdin = child.stdin.take().unwrap();
        for l in lines {
            writeln!(stdin, "{l}").unwrap();
        }
    }
    let out = child.wait_with_output().unwrap();
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .map(|l| serde_json::from_str(l).unwrap())
        .collect()
}

#[test]
fn initialize_list_and_call() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("system_ping", |_| Ok(json!({"pong": true})))
        .tool("bp_get", |a| Ok(json!({"echo": a})))
        .start();
    write_instance(home.path(), &m, "p");
    let replies = session(
        home.path(),
        &[
            json!({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}),
            json!({"jsonrpc":"2.0","method":"notifications/initialized"}),
            json!({"jsonrpc":"2.0","id":2,"method":"tools/list"}),
            json!({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"unreal_ping","arguments":{}}}),
            json!({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"unreal_call","arguments":{"tool":"bp_get","args":{"path":"/Game/X"}}}}),
            json!({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"unreal_call","arguments":{"tool":"missing"}}}),
            json!({"jsonrpc":"2.0","id":6,"method":"bogus"}),
        ],
    );
    assert_eq!(replies.len(), 6, "notifications get no reply: {replies:?}");
    assert_eq!(
        replies[0]["result"]["serverInfo"]["name"],
        "unrealengine-cli"
    );
    assert_eq!(replies[1]["result"]["tools"].as_array().unwrap().len(), 4);
    assert_eq!(replies[2]["result"]["isError"], false);
    let body: Value =
        serde_json::from_str(replies[3]["result"]["content"][0]["text"].as_str().unwrap()).unwrap();
    assert_eq!(body["echo"]["path"], "/Game/X");
    // Warnings (here: the mock runs an older plugin) reach MCP clients as a second text item.
    let warning = replies[3]["result"]["content"][1]["text"]
        .as_str()
        .unwrap_or_default();
    assert!(warning.contains("UnrealCliBridge 0.1.0"), "{}", replies[3]);
    assert_eq!(replies[4]["result"]["isError"], true);
    assert_eq!(replies[5]["error"]["code"], -32601);
}

#[test]
fn configure_merges_client_config() {
    let home = tempfile::tempdir().unwrap();
    std::fs::write(
        home.path().join(".mcp.json"),
        r#"{"mcpServers":{"other":{"command":"x"}}}"#,
    )
    .unwrap();
    let (code, v) = support::run_json(
        home.path(),
        home.path(),
        &["mcp", "configure", "claude-code"],
    );
    assert_eq!(code, 0, "{v}");
    let cfg: Value =
        serde_json::from_str(&std::fs::read_to_string(home.path().join(".mcp.json")).unwrap())
            .unwrap();
    assert_eq!(cfg["mcpServers"]["other"]["command"], "x");
    assert_eq!(
        cfg["mcpServers"]["unrealengine-cli"]["args"],
        json!(["mcp", "serve"])
    );
    let (_, v) = support::run_json(
        home.path(),
        home.path(),
        &["mcp", "configure", "claude-code"],
    );
    assert_eq!(v["data"]["changed"], false);
}

mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

fn echo_bridge() -> support::mock_bridge::MockBridge {
    let mut b = MockBuilder::new();
    for t in [
        "bp_create",
        "bp_add_node",
        "bp_connect",
        "bp_set_pin",
        "bp_add_function",
        "bp_save",
        "bp_compile",
    ] {
        b = b.tool(t, |a| Ok(a.clone()));
    }
    b.start()
}

#[test]
fn subcommands_map_to_tools_and_args() {
    let home = tempfile::tempdir().unwrap();
    let m = echo_bridge();
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, serde_json::Value)> = vec![
        (
            vec!["blueprint", "create", "/Game/BP_A"],
            json!({"path": "/Game/BP_A", "parent": "Actor"}),
        ),
        (
            vec![
                "blueprint",
                "add-node",
                "/Game/BP_A",
                "--type",
                "CallFunction",
                "--member",
                "PrintString",
                "--position",
                "10,20",
            ],
            json!({"path": "/Game/BP_A", "type": "CallFunction", "memberName": "PrintString", "graph": "EventGraph", "position": [10.0, 20.0]}),
        ),
        (
            vec![
                "blueprint",
                "connect",
                "/Game/BP_A",
                "N1.then",
                "N2.execute",
            ],
            json!({"path": "/Game/BP_A", "from": "N1.then", "to": "N2.execute", "graph": "EventGraph"}),
        ),
        (
            vec![
                "blueprint",
                "set-pin",
                "/Game/BP_A",
                "N2",
                "InString",
                "Hello",
            ],
            json!({"path": "/Game/BP_A", "node": "N2", "pin": "InString", "value": "Hello", "graph": "EventGraph"}),
        ),
        (
            vec![
                "blueprint",
                "add-function",
                "/Game/BP_A",
                "Heal",
                "--param",
                "Amount:float",
                "--returns",
                "Ok:bool",
                "--pure",
            ],
            json!({"path": "/Game/BP_A", "name": "Heal", "inputs": [{"name": "Amount", "type": "float"}], "outputs": [{"name": "Ok", "type": "bool"}], "pure": true}),
        ),
        (
            vec!["blueprint", "save", "/Game/BP_A", "--force"],
            json!({"path": "/Game/BP_A", "force": true}),
        ),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

#[test]
fn dry_run_flag_is_forwarded() {
    let home = tempfile::tempdir().unwrap();
    let m = echo_bridge();
    write_instance(home.path(), &m, "p");
    let (code, _) = run_json(
        home.path(),
        home.path(),
        &["--dry-run", "blueprint", "create", "/Game/BP_B"],
    );
    assert_eq!(code, 0);
    assert_eq!(m.calls.lock().unwrap()[0]["dryRun"], true);
}

#[test]
fn bad_param_spec_is_a_usage_error() {
    let home = tempfile::tempdir().unwrap();
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &[
            "blueprint",
            "add-function",
            "/Game/X",
            "F",
            "--param",
            "nocolon",
        ],
    );
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "args.invalid");
}

#[test]
fn compile_errors_on_save_exit_4() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("bp_save", |_| Err(json!({"code": "blueprint.compile_errors", "message": "1 compile error(s); not saved", "hint": "fix the errors or pass force=true"})))
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(home.path(), home.path(), &["blueprint", "save", "/Game/X"]);
    assert_eq!(code, 4);
    assert_eq!(v["error"]["code"], "blueprint.compile_errors");
}

#[test]
fn tool_list_and_find_work_offline_from_bundled_catalog() {
    let home = tempfile::tempdir().unwrap();
    let (code, v) = run_json(home.path(), home.path(), &["tool", "list"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["source"], "bundled");
    assert!(
        v["data"]["count"].as_u64().unwrap() >= 100,
        "bundled catalog must list 100+ tools"
    );
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["tool", "find", "blueprint", "connect"],
    );
    assert_eq!(code, 0);
    assert_eq!(v["data"]["tools"][0]["name"], "bp_connect");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["tool", "describe", "bp_connect"],
    );
    assert_eq!(code, 0);
    assert!(
        v["data"]["inputSchema"]["required"]
            .as_array()
            .unwrap()
            .contains(&json!("from"))
    );
    let (code, v) = run_json(home.path(), home.path(), &["tool", "describe", "bp_conect"]);
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "tool.unknown");
}

#[test]
fn clap_definition_is_consistent() {
    use clap::CommandFactory;
    unreal_cli::cli::Cli::command().debug_assert();
}

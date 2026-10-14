mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

fn echo(tools: &[&str]) -> support::mock_bridge::MockBridge {
    let mut b = MockBuilder::new();
    for t in tools {
        b = b.tool(t, |a| Ok(a.clone()));
    }
    b.start()
}

#[test]
fn pie_start_takes_players_and_net_mode() {
    let home = tempfile::tempdir().unwrap();
    let m = echo(&["pie_start", "pie_worlds"]);
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, serde_json::Value)> = vec![
        (vec!["pie", "start"], json!({})),
        (
            vec!["pie", "start", "--players", "2", "--net-mode", "listen"],
            json!({"players": 2, "netMode": "listen"}),
        ),
        (
            vec!["pie", "start", "--players", "3", "--net-mode", "client"],
            json!({"players": 3, "netMode": "client"}),
        ),
        (vec!["pie", "worlds"], json!({})),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

#[test]
fn bad_net_mode_is_a_usage_error() {
    let home = tempfile::tempdir().unwrap();
    let (code, _) = run_json(
        home.path(),
        home.path(),
        &["pie", "start", "--net-mode", "lan"],
    );
    assert_eq!(code, 2);
}

#[test]
fn world_option_reaches_play_tools() {
    let home = tempfile::tempdir().unwrap();
    let m = echo(&[
        "pie_actors",
        "pie_input",
        "pie_get",
        "pie_screenshot",
        "pie_widgets",
        "pie_exec",
    ]);
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, serde_json::Value)> = vec![
        (
            vec!["pie", "actors", "--world", "client:1"],
            json!({"world": "client:1"}),
        ),
        (
            vec!["pie", "--world", "server", "actors", "--class", "BP_Coin"],
            json!({"world": "server", "classFilter": "BP_Coin"}),
        ),
        (
            vec![
                "pie",
                "input",
                "W",
                "--world",
                "client:2",
                "--seconds",
                "0.5",
            ],
            json!({"world": "client:2", "key": "W", "seconds": 0.5}),
        ),
        (
            vec!["pie", "get", "player", "Health", "--world", "server"],
            json!({"world": "server", "actor": "player", "properties": ["Health"]}),
        ),
        (vec!["pie", "screenshot"], json!({})),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

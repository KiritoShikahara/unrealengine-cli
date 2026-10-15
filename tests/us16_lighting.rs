mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

#[test]
fn light_and_render_commands_reach_their_tools() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("light_preset", |a| Ok(a.clone()))
        .tool("render_settings", |a| Ok(a.clone()))
        .start();
    write_instance(home.path(), &m, "p");
    let cases = vec![
        (
            vec!["light", "preset", "outdoor"],
            json!({"preset": "outdoor", "replace": true}),
        ),
        (
            vec!["light", "preset", "indoor", "--keep"],
            json!({"preset": "indoor", "replace": false}),
        ),
        (vec!["render", "get"], json!({})),
        (
            vec!["render", "set", "lumen", "off"],
            json!({"values": {"lumen": false}}),
        ),
        (
            vec!["render", "set", "antiAliasing", "TSR"],
            json!({"values": {"antiAliasing": "TSR"}}),
        ),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
    let (code, _) = run_json(home.path(), home.path(), &["light", "preset", "noon"]);
    assert_eq!(code, 2);
}

#[test]
fn hlod_needs_a_map_path_and_a_closed_editor() {
    let home = tempfile::tempdir().unwrap();
    let p = tempfile::tempdir().unwrap();
    std::fs::write(
        p.path().join("Foo.uproject"),
        r#"{"EngineAssociation":"5.7"}"#,
    )
    .unwrap();
    let (code, v) = run_json(home.path(), p.path(), &["light", "hlod", "OpenWorld"]);
    assert_eq!(code, 2, "{v}");
    let m = MockBuilder::new().start();
    support::write_instance(
        home.path(),
        &m,
        p.path().join("Foo.uproject").to_str().unwrap(),
    );
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &["light", "hlod", "/Game/Maps/World"],
    );
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "lighting.editor_running");
}

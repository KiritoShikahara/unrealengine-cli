mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

#[test]
fn anim_commands_reach_their_tools() {
    let home = tempfile::tempdir().unwrap();
    let mut b = MockBuilder::new();
    for t in [
        "anim_ikrig_create",
        "anim_retargeter_create",
        "anim_retarget",
        "controlrig_create",
        "posesearch_create",
    ] {
        b = b.tool(t, |a| Ok(a.clone()));
    }
    let m = b.start();
    write_instance(home.path(), &m, "p");
    let cases = vec![
        (
            vec!["anim", "ikrig", "/Game/IK", "--mesh", "/Game/M"],
            json!({"path": "/Game/IK", "mesh": "/Game/M", "auto": true}),
        ),
        (
            vec!["anim", "ikrig", "/Game/IK", "--mesh", "/Game/M", "--empty"],
            json!({"path": "/Game/IK", "mesh": "/Game/M", "auto": false}),
        ),
        (
            vec![
                "anim",
                "retargeter",
                "/Game/R",
                "--source",
                "/Game/A",
                "--target",
                "/Game/B",
            ],
            json!({"path": "/Game/R", "source": "/Game/A", "target": "/Game/B"}),
        ),
        (
            vec![
                "anim", "retarget", "/Game/R", "/Game/W", "/Game/I", "--suffix", "_X",
            ],
            json!({"retargeter": "/Game/R", "assets": ["/Game/W", "/Game/I"], "suffix": "_X"}),
        ),
        (
            vec!["anim", "controlrig", "/Game/CR", "--mesh", "/Game/M"],
            json!({"path": "/Game/CR", "mesh": "/Game/M", "modular": false}),
        ),
        (
            vec![
                "anim",
                "posesearch",
                "/Game/P",
                "--skeleton",
                "/Game/S",
                "/Game/W",
            ],
            json!({"path": "/Game/P", "skeleton": "/Game/S", "animations": ["/Game/W"]}),
        ),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
    let (code, _) = run_json(home.path(), home.path(), &["anim", "retarget", "/Game/R"]);
    assert_eq!(code, 2, "assets are required");
}

mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

#[test]
fn audio_physics_and_pcg_commands_reach_their_tools() {
    let home = tempfile::tempdir().unwrap();
    let mut b = MockBuilder::new();
    for t in [
        "metasound_create",
        "metasound_add_node",
        "metasound_connect",
        "metasound_set_input",
        "geometry_collection_create",
        "pcg_create",
        "pcg_add_node",
        "pcg_connect",
    ] {
        b = b.tool(t, |a| Ok(a.clone()));
    }
    b = b.tool("pcg_generate", |_| Ok(json!({"label": "PCG_G"})));
    b = b.tool("pcg_status", |_| {
        Ok(json!({"generated": true, "generating": false, "instances": 4}))
    });
    let m = b.start();
    write_instance(home.path(), &m, "p");
    let cases = vec![
        (
            vec!["audio", "metasound", "create", "/Game/M", "--stereo"],
            json!({"path": "/Game/M", "format": "stereo", "oneShot": true}),
        ),
        (
            vec!["audio", "metasound", "node", "/Game/M", "UE.Sine.Audio"],
            json!({"path": "/Game/M", "node": "UE.Sine.Audio"}),
        ),
        (
            vec![
                "audio",
                "metasound",
                "connect",
                "/Game/M",
                "A:Audio",
                "graph:Out",
            ],
            json!({"path": "/Game/M", "from": "A:Audio", "to": "graph:Out"}),
        ),
        (
            vec![
                "audio",
                "metasound",
                "set",
                "/Game/M",
                "A",
                "Frequency",
                "440",
            ],
            json!({"path": "/Game/M", "node": "A", "input": "Frequency", "value": 440}),
        ),
        (
            vec![
                "audio",
                "metasound",
                "set",
                "/Game/M",
                "A",
                "Enabled",
                "false",
            ],
            json!({"path": "/Game/M", "node": "A", "input": "Enabled", "value": false}),
        ),
        (
            vec![
                "physics",
                "collection",
                "/Game/GC",
                "--mesh",
                "/Game/A",
                "--mesh",
                "/Game/B",
            ],
            json!({"path": "/Game/GC", "meshes": ["/Game/A", "/Game/B"]}),
        ),
        (vec!["pcg", "create", "/Game/G"], json!({"path": "/Game/G"})),
        (
            vec![
                "pcg",
                "node",
                "/Game/G",
                "CreatePointsGrid",
                "--props",
                r#"{"cell_size":[1,1,1]}"#,
            ],
            json!({"path": "/Game/G", "type": "CreatePointsGrid", "properties": {"cell_size": [1, 1, 1]}}),
        ),
        (
            vec!["pcg", "connect", "/Game/G", "A:Out", "B:In"],
            json!({"path": "/Game/G", "from": "A:Out", "to": "B:In"}),
        ),
        (
            vec!["pcg", "generate", "/Game/G"],
            json!({"generated": true, "generating": false, "instances": 4}),
        ),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

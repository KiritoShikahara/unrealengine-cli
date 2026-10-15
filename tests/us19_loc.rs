mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};
use unrealengine_cli::commands::loc::fill_po;

const PO: &str = "msgid \"\"\nmsgstr \"\"\n\n#. Key: 1\nmsgctxt \",1\"\nmsgid \"Hello\"\nmsgstr \"\"\n\nmsgctxt \",2\"\nmsgid \"Say \\\"hi\\\"\"\nmsgstr \"old\"\n";

#[test]
fn po_entries_are_filled_by_source_text() {
    let (out, filled) = fill_po(
        PO,
        &[
            ("Hello".into(), "こんにちは".into()),
            ("Say \"hi\"".into(), "「やあ」と言う".into()),
            ("Missing".into(), "x".into()),
        ],
    );
    assert_eq!(filled, vec!["Hello".to_string(), "Say \"hi\"".to_string()]);
    assert!(
        out.contains("msgid \"Hello\"\nmsgstr \"こんにちは\"\n"),
        "{out}"
    );
    assert!(out.contains("msgstr \"「やあ」と言う\""), "{out}");
    assert!(
        out.starts_with("msgid \"\"\nmsgstr \"\"\n"),
        "header untouched: {out}"
    );
}

#[test]
fn loc_commands() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("loc_target", |a| Ok(a.clone()))
        .tool("loc_preview", |a| Ok(a.clone()))
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &[
            "loc",
            "target",
            "--culture",
            "ja",
            "--culture",
            "fr",
            "--path",
            "Content/UI",
        ],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(
        v["data"],
        json!({"target": "Game", "cultures": ["ja", "fr"], "paths": ["Content/UI"]})
    );
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &[
            "loc",
            "preview",
            "ja",
            "--asset",
            "/Game/BP",
            "--property",
            "Greeting",
        ],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(
        v["data"],
        json!({"culture": "ja", "asset": "/Game/BP", "property": "Greeting"})
    );

    // Steps need the target's configs; translate needs an exported PO file.
    let p = tempfile::tempdir().unwrap();
    std::fs::write(p.path().join("Foo.uproject"), "{}").unwrap();
    let (code, v) = run_json(home.path(), p.path(), &["loc", "gather"]);
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "loc.no_target");
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &["loc", "translate", "ja", "--entry", "A=B"],
    );
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "loc.no_po");
    let dir = p.path().join("Content/Localization/Game/ja");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("Game.po"), PO).unwrap();
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &["loc", "translate", "ja", "--entry", "Hello=やあ"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["filled"], json!(["Hello"]));
    assert!(
        std::fs::read_to_string(dir.join("Game.po"))
            .unwrap()
            .contains("msgstr \"やあ\"")
    );
}

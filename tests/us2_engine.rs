use std::path::PathBuf;

use unrealengine_cli::engine::locate::{
    Engine, parse_launcher_installed, parse_reg_query, resolve_association,
};

#[test]
fn launcher_file_yields_only_engine_entries() {
    let text = r#"{"InstallationList":[
        {"InstallLocation":"C:\\Epic\\UE_5.8","AppName":"QuixelBridge_5.8"},
        {"InstallLocation":"C:\\Epic\\UE_5.7","AppName":"UE_5.7"},
        {"InstallLocation":"C:\\Epic\\UE_5.5","AppName":"UE_5.5"},
        {"InstallLocation":"C:\\Epic\\UE_5.7","AppName":"UE_5.7"}]}"#;
    let engines = parse_launcher_installed(text);
    assert_eq!(
        engines.iter().map(|e| e.id.as_str()).collect::<Vec<_>>(),
        vec!["5.5", "5.7"]
    );
    assert_eq!(engines[1].root, PathBuf::from(r"C:\Epic\UE_5.7"));
}

#[test]
fn malformed_launcher_file_is_empty() {
    assert!(parse_launcher_installed("not json").is_empty());
}

#[test]
fn reg_query_values_are_parsed() {
    let text = "\r\nHKEY_CURRENT_USER\\Software\\Epic Games\\Unreal Engine\\Builds\r\n    {A1B2}    REG_SZ    D:/UE/Source\r\n";
    assert_eq!(
        parse_reg_query(text),
        vec![("{A1B2}".to_string(), "D:/UE/Source".to_string())]
    );
}

#[test]
fn association_matches_version_or_falls_back_to_newest_when_empty() {
    let engines = vec![
        Engine {
            id: "5.6".into(),
            root: "a".into(),
        },
        Engine {
            id: "5.8".into(),
            root: "b".into(),
        },
    ];
    assert_eq!(
        resolve_association("5.6", &engines).unwrap().root,
        PathBuf::from("a")
    );
    assert_eq!(resolve_association("", &engines).unwrap().id, "5.8");
    assert!(resolve_association("5.3", &engines).is_none());
}

#[test]
fn association_can_be_an_engine_path() {
    let dir = tempfile::tempdir().unwrap();
    std::fs::create_dir(dir.path().join("Engine")).unwrap();
    let e = resolve_association(dir.path().to_str().unwrap(), &[]).unwrap();
    assert_eq!(e.root, dir.path());
}

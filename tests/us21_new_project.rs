use unrealengine_cli::scaffold::{create, validate_name};

#[test]
fn names_follow_unreal_rules() {
    assert!(validate_name("MyGame").is_ok());
    assert!(validate_name("My_Game2").is_ok());
    for bad in ["", "2Game", "My Game", "My-Game", "ThisNameIsWayTooLong21"] {
        assert!(validate_name(bad).is_err(), "{bad}");
    }
}

#[test]
fn creates_a_blueprint_project_that_opens() {
    let tmp = tempfile::tempdir().unwrap();
    let data = create(tmp.path(), "MyGame", "5.7", false).unwrap();
    let dir = tmp.path().join("MyGame");
    let up: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(dir.join("MyGame.uproject")).unwrap())
            .unwrap();
    assert_eq!(up["EngineAssociation"], "5.7");
    assert_eq!(up["FileVersion"], 3);
    assert!(dir.join("Content").is_dir());
    assert!(dir.join("Config/DefaultEngine.ini").is_file());
    assert!(dir.join("Config/DefaultGame.ini").is_file());
    assert_eq!(
        data["uproject"].as_str().unwrap(),
        dir.join("MyGame.uproject").display().to_string()
    );
}

#[test]
fn dry_run_writes_nothing_and_existing_folder_is_refused() {
    let tmp = tempfile::tempdir().unwrap();
    let data = create(tmp.path(), "MyGame", "5.7", true).unwrap();
    assert!(!data["changes"].as_array().unwrap().is_empty());
    assert!(!tmp.path().join("MyGame").exists());

    std::fs::create_dir_all(tmp.path().join("Taken/x")).unwrap();
    let err = create(tmp.path(), "Taken", "5.7", false).unwrap_err();
    assert_eq!(err.code, "project.exists");
}

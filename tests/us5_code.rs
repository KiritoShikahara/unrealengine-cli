mod support;

use std::path::Path;

use support::run_json;

fn project(dir: &Path) {
    std::fs::write(dir.join("Game.uproject"), r#"{"EngineAssociation":"5.7"}"#).unwrap();
    std::fs::create_dir_all(dir.join("Source/Game")).unwrap();
    std::fs::write(dir.join("Source/Game/Game.Build.cs"), "// rules").unwrap();
    std::fs::write(
        dir.join("Source/Game/Hero.cpp"),
        "void Hero::Tick()\n{\n\tSpeed = 10;\n}\n",
    )
    .unwrap();
}

#[test]
fn search_read_and_edit() {
    let home = tempfile::tempdir().unwrap();
    let p = tempfile::tempdir().unwrap();
    project(p.path());
    let (code, v) = run_json(home.path(), p.path(), &["code", "search", "speed"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["hits"][0]["file"], "Source/Game/Hero.cpp");
    assert_eq!(v["data"]["hits"][0]["line"], 3);
    let (_, v) = run_json(
        home.path(),
        p.path(),
        &[
            "code",
            "read",
            "Source/Game/Hero.cpp",
            "--start",
            "3",
            "--end",
            "3",
        ],
    );
    assert_eq!(v["data"]["text"], "3\t\tSpeed = 10;");
    let (code, _) = run_json(
        home.path(),
        p.path(),
        &[
            "code",
            "edit",
            "Source/Game/Hero.cpp",
            "--old",
            "Speed = 10",
            "--new",
            "Speed = 20",
        ],
    );
    assert_eq!(code, 0);
    assert!(
        std::fs::read_to_string(p.path().join("Source/Game/Hero.cpp"))
            .unwrap()
            .contains("Speed = 20")
    );
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &[
            "code",
            "edit",
            "Source/Game/Hero.cpp",
            "--old",
            "e",
            "--new",
            "x",
        ],
    );
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "code.edit_ambiguous");
    let (code, v) = run_json(home.path(), p.path(), &["code", "read", "../outside.txt"]);
    assert_eq!(code, 2, "{v}");
}

#[test]
fn class_create_writes_uclass_pair_and_respects_dry_run() {
    let home = tempfile::tempdir().unwrap();
    let p = tempfile::tempdir().unwrap();
    project(p.path());
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &["--dry-run", "code", "class-create", "Enemy"],
    );
    assert_eq!(code, 0, "{v}");
    assert!(!p.path().join("Source/Game/Enemy.h").exists());
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &["code", "class-create", "Enemy", "--parent", "Character"],
    );
    assert_eq!(code, 0, "{v}");
    let h = std::fs::read_to_string(p.path().join("Source/Game/Enemy.h")).unwrap();
    assert!(h.contains("class GAME_API AEnemy : public ACharacter"));
    assert!(h.contains("#include \"Enemy.generated.h\""));
    let (code, v) = run_json(home.path(), p.path(), &["code", "class-create", "Enemy"]);
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "code.exists");
    let (code, _) = run_json(home.path(), p.path(), &["code", "class-create", "Bad Name"]);
    assert_eq!(code, 2);
}

#[test]
fn apply_plan_without_editor_reports_rebuild() {
    let home = tempfile::tempdir().unwrap();
    let p = tempfile::tempdir().unwrap();
    project(p.path());
    let (code, v) = run_json(home.path(), p.path(), &["code", "apply", "--plan"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["action"], "rebuild");
    assert_eq!(v["data"]["applied"], false);
}

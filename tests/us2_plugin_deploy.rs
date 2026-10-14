mod support;

use std::path::Path;

use serde_json::Value;
use support::run_json;

fn project(dir: &Path, with_code: bool) -> std::path::PathBuf {
    let up = dir.join("Game.uproject");
    std::fs::write(&up, "{\n\t\"FileVersion\": 3,\n\t\"EngineAssociation\": \"5.7\",\n\t\"Plugins\": [ { \"Name\": \"Other\", \"Enabled\": true } ]\n}\n").unwrap();
    if with_code {
        std::fs::create_dir_all(dir.join("Source")).unwrap();
        std::fs::write(dir.join("Source/GameEditor.Target.cs"), "").unwrap();
    }
    up
}

fn snapshot(dir: &Path) -> Vec<String> {
    let mut out = Vec::new();
    for e in walk(dir) {
        out.push(format!(
            "{}:{}",
            e.display(),
            std::fs::read(&e).map(|b| b.len()).unwrap_or(0)
        ));
    }
    out.sort();
    out
}

fn walk(dir: &Path) -> Vec<std::path::PathBuf> {
    let mut out = Vec::new();
    for e in std::fs::read_dir(dir).unwrap().flatten() {
        let p = e.path();
        if p.is_dir() {
            out.extend(walk(&p));
        } else {
            out.push(p);
        }
    }
    out
}

#[test]
fn dry_run_changes_nothing_and_lists_changes() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    project(proj.path(), true);
    let before = snapshot(proj.path());
    let (code, v) = run_json(home.path(), proj.path(), &["setup", "--dry-run"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["dryRun"], true);
    assert!(v["data"]["changes"].as_array().unwrap().len() > 5);
    assert_eq!(snapshot(proj.path()), before);
}

#[test]
fn setup_installs_plugin_and_enables_it_once() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    let up = project(proj.path(), true);
    let (code, v) = run_json(home.path(), proj.path(), &["setup"]);
    assert_eq!(code, 0, "{v}");
    assert!(
        proj.path()
            .join("Plugins/UnrealCliBridge/UnrealCliBridge.uplugin")
            .is_file()
    );
    assert!(
        proj.path()
            .join("Plugins/UnrealCliBridge/Content/Python/unreal_cli_bridge/__init__.py")
            .is_file()
    );
    assert!(
        !proj
            .path()
            .join("Plugins/UnrealCliBridge/Binaries")
            .exists()
    );
    let u: Value = serde_json::from_str(&std::fs::read_to_string(&up).unwrap()).unwrap();
    let names: Vec<&str> = u["Plugins"]
        .as_array()
        .unwrap()
        .iter()
        .map(|p| p["Name"].as_str().unwrap())
        .collect();
    assert_eq!(names, vec!["Other", "UnrealCliBridge"]);
    assert_eq!(u["EngineAssociation"], "5.7");
    let keys: Vec<&String> = u.as_object().unwrap().keys().collect();
    assert_eq!(
        keys,
        vec!["FileVersion", "EngineAssociation", "Plugins"],
        "key order is preserved"
    );

    let (code, v) = run_json(home.path(), proj.path(), &["setup"]);
    assert_eq!(code, 0);
    assert_eq!(v["data"]["upToDate"], true);
}

#[test]
fn blueprint_only_project_needs_code_module_flag() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    let up = project(proj.path(), false);
    let (code, v) = run_json(home.path(), proj.path(), &["setup"]);
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "setup.no_code_module");

    let (code, v) = run_json(home.path(), proj.path(), &["setup", "--add-code-module"]);
    assert_eq!(code, 0, "{v}");
    assert!(proj.path().join("Source/GameEditor.Target.cs").is_file());
    assert!(proj.path().join("Source/Game/Game.Build.cs").is_file());
    let u: Value = serde_json::from_str(&std::fs::read_to_string(&up).unwrap()).unwrap();
    assert_eq!(u["Modules"][0]["Name"], "Game");
}

#[test]
fn doctor_reports_checks_without_failing() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    project(proj.path(), true);
    let (code, v) = run_json(home.path(), proj.path(), &["doctor"]);
    assert_eq!(code, 0, "{v}");
    let checks = v["data"]["checks"].as_array().unwrap();
    let plugin = checks.iter().find(|c| c["name"] == "plugin").unwrap();
    assert_eq!(plugin["ok"], false);
    assert_eq!(plugin["hint"], "unrealengine-cli setup");
}

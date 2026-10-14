//! Python tool modules must at least compile (checked with the host Python when available).

use std::path::Path;
use std::process::Command;

#[test]
fn python_tool_modules_compile() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("Plugin/UnrealCliBridge/Content/Python/unreal_cli_bridge");
    let python = ["python3", "python"].into_iter().find(|p| {
        Command::new(p)
            .arg("--version")
            .output()
            .is_ok_and(|o| o.status.success())
    });
    let Some(python) = python else {
        eprintln!("skipped: no python on PATH");
        return;
    };
    let script = "import ast,pathlib,sys\nfor p in pathlib.Path(sys.argv[1]).rglob('*.py'):\n    ast.parse(p.read_text(encoding='utf-8'), str(p))\n";
    let out = Command::new(python)
        .args(["-c", script])
        .arg(&root)
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
}

#[test]
fn every_python_tool_is_in_the_catalog() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("Plugin/UnrealCliBridge/Content/Python/unreal_cli_bridge/tools");
    let catalog: Vec<serde_json::Value> =
        serde_json::from_str(include_str!("../tools/catalog.json")).unwrap();
    let names: Vec<&str> = catalog.iter().filter_map(|t| t["name"].as_str()).collect();
    let re = regex::Regex::new(r#"@tool\("([a-z0-9_]+)""#).unwrap();
    for e in std::fs::read_dir(&root).unwrap().flatten() {
        if e.path().extension().and_then(|x| x.to_str()) != Some("py") {
            continue;
        }
        let text = std::fs::read_to_string(e.path()).unwrap();
        for c in re.captures_iter(&text) {
            assert!(
                names.contains(&&c[1]),
                "{} is not in tools/catalog.json (run scripts/gen-catalog.sh)",
                &c[1]
            );
        }
    }
}

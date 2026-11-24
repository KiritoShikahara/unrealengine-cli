use std::path::Path;

fn json(path: &str) -> serde_json::Value {
    let text = std::fs::read_to_string(Path::new(env!("CARGO_MANIFEST_DIR")).join(path))
        .unwrap_or_else(|e| panic!("{path}: {e}"));
    serde_json::from_str(&text).unwrap()
}

#[test]
fn plugin_manifests_match_the_crate_version() {
    let version = env!("CARGO_PKG_VERSION");
    let plugin = json(".claude-plugin/plugin.json");
    assert_eq!(plugin["name"], "unrealengine-cli");
    assert_eq!(plugin["version"], version);
    let market = json(".claude-plugin/marketplace.json");
    assert_eq!(market["plugins"][0]["name"], "unrealengine-cli");
    assert_eq!(market["plugins"][0]["version"], version);
    assert_eq!(market["plugins"][0]["source"], "./");
}

#[test]
fn plugin_adds_the_session_start_hook() {
    let hooks = json("hooks/hooks.json");
    let command = hooks["hooks"]["SessionStart"][0]["hooks"][0]["command"]
        .as_str()
        .unwrap();
    assert!(command.starts_with(unrealengine_cli::hook::SESSION_START_COMMAND));
}

#[test]
fn usage_skill_bootstraps_a_missing_cli() {
    let text = std::fs::read_to_string(
        Path::new(env!("CARGO_MANIFEST_DIR")).join("skills/unrealengine-cli-usage/SKILL.md"),
    )
    .unwrap();
    assert!(text.contains("scripts/install.sh"));
    assert!(text.contains("scripts/install.ps1"));
    assert!(text.contains("unrealengine-cli setup"));
}

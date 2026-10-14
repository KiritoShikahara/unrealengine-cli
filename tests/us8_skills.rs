mod support;

use std::collections::HashSet;

use support::run_json;
use unrealengine_cli::skills::lint_skills;

#[test]
fn bundled_skills_pass_lint() {
    let home = tempfile::tempdir().unwrap();
    let (code, v) = run_json(home.path(), home.path(), &["skills", "lint"]);
    assert_eq!(code, 0, "{v}");
    assert!(v["data"]["skills"].as_u64().unwrap() >= 19);
}

#[test]
fn install_refresh_uninstall_for_every_client() {
    let home = tempfile::tempdir().unwrap();
    let proj = tempfile::tempdir().unwrap();
    for (client, dir) in [
        ("claude-code", ".claude/skills"),
        ("codex", ".agents/skills"),
        ("cursor", ".cursor/skills"),
        ("windsurf", ".windsurf/skills"),
        ("vscode", ".github/skills"),
    ] {
        let (code, v) = run_json(
            home.path(),
            proj.path(),
            &["skills", "install", client, "--local"],
        );
        assert_eq!(code, 0, "{client}: {v}");
        assert!(
            proj.path()
                .join(dir)
                .join("unrealengine-cli-usage/SKILL.md")
                .is_file(),
            "{client}"
        );
    }
    std::fs::write(
        proj.path().join(".claude/skills/unreal-umg/SKILL.md"),
        "stale",
    )
    .unwrap();
    let (code, v) = run_json(home.path(), proj.path(), &["skills", "refresh", "--local"]);
    assert_eq!(code, 0, "{v}");
    assert!(
        std::fs::read_to_string(proj.path().join(".claude/skills/unreal-umg/SKILL.md"))
            .unwrap()
            .contains("## Steps")
    );
    let (code, _) = run_json(
        home.path(),
        proj.path(),
        &["skills", "uninstall", "cursor", "--local"],
    );
    assert_eq!(code, 0);
    assert!(
        !proj
            .path()
            .join(".cursor/skills/unrealengine-cli-usage")
            .exists()
    );
    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &["skills", "install", "emacs", "--local"],
    );
    assert_eq!(code, 2);
    assert_eq!(v["error"]["code"], "skills.unknown_client");
}

#[test]
fn install_needs_no_editor_and_global_goes_to_home() {
    let home = tempfile::tempdir().unwrap();
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["--dry-run", "skills", "install", "claude-code", "--global"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["dryRun"], true);
}

fn skill(name: &str, body: &str) -> (String, String) {
    (name.to_string(), body.to_string())
}

#[test]
fn lint_rules_catch_contract_violations() {
    let tools: HashSet<String> = ["bp_create".to_string()].into();
    let cmds: HashSet<String> = ["blueprint".to_string()].into();
    let good = "---\nname: s\ndescription: Use when testing.\nversion: 1\ntools: [bp_create]\n---\n## When to use\nx\n## Steps\n`unrealengine-cli blueprint create`\n## Verify\nx\n## On failure\nx\n";
    assert!(lint_skills(&[skill("s", good)], &tools, &cmds, None).is_empty());
    let rules = |text: &str, dir: &str| -> Vec<String> {
        lint_skills(&[skill(dir, text)], &tools, &cmds, None)
            .iter()
            .map(|f| f["rule"].as_str().unwrap().to_string())
            .collect()
    };
    assert!(rules("no frontmatter", "s").contains(&"frontmatter".to_string()));
    assert!(rules(good, "other").contains(&"name-matches-dir".to_string()));
    assert!(
        rules(&good.replace("bp_create]", "bp_nope]"), "s").contains(&"unknown-tool".to_string())
    );
    assert!(
        rules(&good.replace("blueprint create", "frobnicate"), "s")
            .contains(&"unknown-command".to_string())
    );
    assert!(
        rules(&good.replace("## Verify\nx", "## Verify\n"), "s")
            .contains(&"empty-section".to_string())
    );
    assert!(
        rules(&good.replace("## On failure\nx\n", ""), "s")
            .contains(&"required-section".to_string())
    );
    assert!(
        rules(&good.replace("Use when testing.", "Testing."), "s")
            .contains(&"description".to_string())
    );
    assert!(
        rules(
            &good.replace("## Verify\nx", "## Verify\nsee C:\\Users\\me\\x"),
            "s"
        )
        .contains(&"absolute-path".to_string())
    );
    assert!(
        rules(
            &good.replace("## Verify\nx", "## Verify\nlike Unity does"),
            "s"
        )
        .contains(&"foreign-product".to_string())
    );
    let dup = lint_skills(&[skill("s", good), skill("s", good)], &tools, &cmds, None);
    assert!(dup.iter().any(|f| f["rule"] == "duplicate-name"));
}

#[test]
fn claude_global_dir_follows_claude_config_dir() {
    use std::ffi::OsStr;
    use std::path::Path;
    use unrealengine_cli::skills::claude_global_dir;
    let home = Path::new("/home/u");
    assert_eq!(
        claude_global_dir(Some(OsStr::new("/home/u/.claude-work")), home),
        Path::new("/home/u/.claude-work/skills")
    );
    assert_eq!(claude_global_dir(None, home), home.join(".claude/skills"));
    assert_eq!(
        claude_global_dir(Some(OsStr::new("")), home),
        home.join(".claude/skills")
    );
}

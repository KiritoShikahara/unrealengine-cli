//! Commands shown in the READMEs and docs must exist.

use std::collections::HashSet;
use std::path::Path;

use clap::CommandFactory;
use regex::Regex;
use unrealengine_cli::cli::Cli;

#[test]
fn documented_commands_exist() {
    let cmd = Cli::command();
    let top: HashSet<String> = cmd
        .get_subcommands()
        .map(|c| c.get_name().to_string())
        .collect();
    let re = Regex::new(r"unrealengine-cli ([a-z][a-z-]*)(?: ([a-z][a-z-]*))?").unwrap();
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let mut bad = Vec::new();
    for f in [
        "README.md",
        "README.ja.md",
        "docs/skills.md",
        "docs/architecture.md",
        "CLAUDE.md",
    ] {
        let text = std::fs::read_to_string(root.join(f)).unwrap();
        // Only code: fenced blocks and inline `...` spans, not prose.
        let mut code = String::new();
        let mut fenced = false;
        for line in text.lines() {
            if line.trim_start().starts_with("```") {
                fenced = !fenced;
                continue;
            }
            if fenced {
                code.push_str(line);
                code.push('\n');
            } else {
                for (i, span) in line.split('`').enumerate() {
                    if i % 2 == 1 {
                        code.push_str(span);
                        code.push('\n');
                    }
                }
            }
        }
        for c in re.captures_iter(&code) {
            let first = &c[1];
            if !top.contains(first) {
                bad.push(format!("{f}: unrealengine-cli {first}"));
                continue;
            }
            let sub = cmd.find_subcommand(first).unwrap();
            if let Some(second) = c.get(2).map(|m| m.as_str())
                && sub.has_subcommands()
                && sub.find_subcommand(second).is_none()
            {
                bad.push(format!("{f}: unrealengine-cli {first} {second}"));
            }
        }
    }
    assert!(bad.is_empty(), "{bad:#?}");
}

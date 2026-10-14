//! The repository must not reference other products' code or tools (constitution I).

use std::path::Path;
use std::sync::LazyLock;

use regex::Regex;

static FOREIGN: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?i)\bunity\b|unity-cli|\bue-cli\b|\buecli\b").unwrap());

/// Files that must name the forbidden words to check for them.
const SELF_CHECKING: [&str; 3] = ["independence.rs", "skills.rs", "us8_skills.rs"];

fn scan(dir: &Path, hits: &mut Vec<String>) {
    for e in std::fs::read_dir(dir).unwrap().flatten() {
        let p = e.path();
        let name = p.file_name().unwrap().to_string_lossy().to_string();
        if p.is_dir() {
            if !matches!(name.as_str(), "Binaries" | "Intermediate" | "__pycache__") {
                scan(&p, hits);
            }
            continue;
        }
        if SELF_CHECKING.contains(&name.as_str()) {
            continue;
        }
        let Ok(text) = std::fs::read_to_string(&p) else {
            continue;
        };
        for (i, line) in text.lines().enumerate() {
            if FOREIGN.is_match(line) {
                hits.push(format!("{}:{}: {}", p.display(), i + 1, line.trim()));
            }
        }
    }
}

#[test]
fn no_references_to_other_products() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let mut hits = Vec::new();
    for d in [
        "src",
        "skills",
        "Plugin",
        "tests",
        "scripts",
        "docs",
        "tools",
        "packaging",
    ] {
        if root.join(d).is_dir() {
            scan(&root.join(d), &mut hits);
        }
    }
    for f in ["README.md", "README.ja.md", "CLAUDE.md", "Cargo.toml"] {
        if let Ok(text) = std::fs::read_to_string(root.join(f)) {
            assert!(!FOREIGN.is_match(&text), "{f} mentions another product");
        }
    }
    assert!(hits.is_empty(), "{hits:#?}");
}

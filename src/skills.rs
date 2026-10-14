//! Bundled agent skills: install / refresh / uninstall / list / lint (Skill Contract v1).

use std::collections::{BTreeSet, HashSet};
use std::path::{Path, PathBuf};
use std::sync::LazyLock;

use include_dir::{Dir, include_dir};
use regex::Regex;
use serde_json::{Value, json};

use crate::catalog;
use crate::core::error::CliError;
use crate::core::paths;

pub static SKILLS: Dir<'_> = include_dir!("$CARGO_MANIFEST_DIR/skills");
const MARKER: &str = ".unrealengine-cli-skills.json";

/// Bundled skills as (name, SKILL.md text).
pub fn bundled() -> Vec<(String, String)> {
    let mut out: Vec<(String, String)> = SKILLS
        .dirs()
        .filter_map(|d| {
            let name = d.path().file_name()?.to_string_lossy().to_string();
            let text = d
                .get_file(d.path().join("SKILL.md"))?
                .contents_utf8()?
                .to_string();
            Some((name, text))
        })
        .collect();
    out.sort();
    out
}

/// Claude Code's user skill directory: `$CLAUDE_CONFIG_DIR/skills` when set (non-empty), else `~/.claude/skills`.
pub fn claude_global_dir(config_dir: Option<&std::ffi::OsStr>, home: &Path) -> PathBuf {
    match config_dir {
        Some(d) if !d.is_empty() => PathBuf::from(d).join("skills"),
        _ => home.join(".claude/skills"),
    }
}

/// Skill directory for a client: project-local or user-global.
pub fn target_dir(client: &str, global: bool, cwd: &Path) -> Result<PathBuf, CliError> {
    let rel = match client {
        "claude-code" => ".claude/skills",
        "codex" => ".agents/skills",
        "cursor" => ".cursor/skills",
        "windsurf" => ".windsurf/skills",
        "vscode" => ".github/skills",
        other => {
            return Err(CliError::usage(
                "skills.unknown_client",
                format!("unknown client '{other}'"),
            )
            .with_hint("clients: claude-code, codex, cursor, windsurf, vscode"));
        }
    };
    let base = if global {
        match client {
            "codex" => paths::home().join(".codex/skills"),
            "claude-code" => claude_global_dir(
                std::env::var_os("CLAUDE_CONFIG_DIR").as_deref(),
                &paths::home(),
            ),
            _ => paths::home().join(rel),
        }
    } else {
        cwd.join(rel)
    };
    Ok(base)
}

pub fn install(client: &str, global: bool, cwd: &Path, dry_run: bool) -> Result<Value, CliError> {
    let dir = target_dir(client, global, cwd)?;
    let mut written = Vec::new();
    for (name, text) in bundled() {
        let path = dir.join(&name).join("SKILL.md");
        if std::fs::read_to_string(&path).ok().as_deref() == Some(text.as_str()) {
            continue;
        }
        written.push(name.clone());
        if !dry_run {
            std::fs::create_dir_all(path.parent().unwrap())
                .and_then(|_| std::fs::write(&path, &text))
                .map_err(|e| CliError::internal("io.write", format!("{}: {e}", path.display())))?;
        }
    }
    if !dry_run {
        let names: Vec<String> = bundled().into_iter().map(|(n, _)| n).collect();
        std::fs::create_dir_all(&dir)
            .and_then(|_| std::fs::write(dir.join(MARKER), json!({ "client": client, "skills": names, "cliVersion": env!("CARGO_PKG_VERSION") }).to_string()))
            .map_err(|e| CliError::internal("io.write", e.to_string()))?;
    }
    Ok(
        json!({ "client": client, "dir": dir, "updated": written, "total": bundled().len(), "dryRun": dry_run }),
    )
}

/// Re-installs into every client directory that has a marker (local or global).
pub fn refresh(global: bool, cwd: &Path, dry_run: bool) -> Result<Value, CliError> {
    let mut done = Vec::new();
    for client in ["claude-code", "codex", "cursor", "windsurf", "vscode"] {
        if target_dir(client, global, cwd)?.join(MARKER).is_file() {
            done.push(install(client, global, cwd, dry_run)?);
        }
    }
    Ok(json!({ "refreshed": done }))
}

pub fn uninstall(client: &str, global: bool, cwd: &Path, dry_run: bool) -> Result<Value, CliError> {
    let dir = target_dir(client, global, cwd)?;
    let mut removed = Vec::new();
    for (name, _) in bundled() {
        let d = dir.join(&name);
        if d.is_dir() {
            removed.push(name);
            if !dry_run {
                std::fs::remove_dir_all(&d)
                    .map_err(|e| CliError::internal("io.write", e.to_string()))?;
            }
        }
    }
    if !dry_run {
        let _ = std::fs::remove_file(dir.join(MARKER));
    }
    Ok(json!({ "client": client, "removed": removed, "dryRun": dry_run }))
}

pub fn list() -> Value {
    let items: Vec<Value> = bundled()
        .iter()
        .map(|(n, t)| json!({ "name": n, "description": frontmatter(t).and_then(|f| field(&f, "description")).unwrap_or_default() }))
        .collect();
    json!({ "skills": items })
}

fn frontmatter(text: &str) -> Option<String> {
    let rest = text.strip_prefix("---\n")?;
    let end = rest.find("\n---")?;
    Some(rest[..end].to_string())
}

fn field(front: &str, key: &str) -> Option<String> {
    front.lines().find_map(|l| {
        l.strip_prefix(&format!("{key}:"))
            .map(|v| v.trim().to_string())
    })
}

static CLI_REF: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"unrealengine-cli ([a-z][a-z-]*)").unwrap());
static LINK: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\]\(([^)#:]+)\)").unwrap());
static ABS: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?i)\b[a-z]:[\\/](users|develop|program files)|/users/[a-z]|/home/[a-z]").unwrap()
});
static FOREIGN: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?i)\bunity\b|unity-cli|\bue-cli\b").unwrap());

const REQUIRED_SECTIONS: [&str; 4] = ["## When to use", "## Steps", "## Verify", "## On failure"];

/// One lint finding.
fn finding(skill: &str, rule: &str, severity: &str, message: String) -> Value {
    json!({ "skill": skill, "rule": rule, "severity": severity, "message": message })
}

/// Checks skills against the contract. `commands` are valid top-level subcommands.
pub fn lint_skills(
    skills: &[(String, String)],
    tools: &HashSet<String>,
    commands: &HashSet<String>,
    base: Option<&Path>,
) -> Vec<Value> {
    let mut out = Vec::new();
    let mut names = BTreeSet::new();
    for (dir, text) in skills {
        let Some(front) = frontmatter(text) else {
            out.push(finding(
                dir,
                "frontmatter",
                "error",
                "missing --- frontmatter ---".into(),
            ));
            continue;
        };
        for key in ["name", "description", "version"] {
            if field(&front, key).filter(|v| !v.is_empty()).is_none() {
                out.push(finding(
                    dir,
                    "frontmatter-field",
                    "error",
                    format!("missing '{key}'"),
                ));
            }
        }
        let name = field(&front, "name").unwrap_or_default();
        if name != *dir {
            out.push(finding(
                dir,
                "name-matches-dir",
                "error",
                format!("name '{name}' differs from directory"),
            ));
        }
        if !names.insert(name.clone()) {
            out.push(finding(
                dir,
                "duplicate-name",
                "error",
                format!("duplicate name '{name}'"),
            ));
        }
        let desc = field(&front, "description").unwrap_or_default();
        if desc.chars().count() > 300 || !desc.starts_with("Use when") {
            out.push(finding(
                dir,
                "description",
                "error",
                "description must start with 'Use when' and be at most 300 characters".into(),
            ));
        }
        for s in REQUIRED_SECTIONS {
            match text.find(s) {
                None => out.push(finding(
                    dir,
                    "required-section",
                    "error",
                    format!("missing '{s}'"),
                )),
                Some(i) => {
                    let after = &text[i + s.len()..];
                    let body = after.split("\n## ").next().unwrap_or_default();
                    if body.trim().is_empty() {
                        out.push(finding(
                            dir,
                            "empty-section",
                            "error",
                            format!("'{s}' is empty"),
                        ));
                    }
                }
            }
        }
        if let Some(list) = field(&front, "tools") {
            for t in list
                .trim_matches(|c| c == '[' || c == ']')
                .split(',')
                .map(str::trim)
                .filter(|t| !t.is_empty())
            {
                if !tools.contains(t) {
                    out.push(finding(
                        dir,
                        "unknown-tool",
                        "error",
                        format!("tool '{t}' is not in the catalog"),
                    ));
                }
            }
        }
        for c in CLI_REF.captures_iter(text) {
            if !commands.contains(&c[1]) {
                out.push(finding(
                    dir,
                    "unknown-command",
                    "error",
                    format!("'unrealengine-cli {}' is not a command", &c[1]),
                ));
            }
        }
        if let Some(b) = base {
            for l in LINK.captures_iter(text) {
                if !b.join(dir).join(&l[1]).exists() {
                    out.push(finding(
                        dir,
                        "broken-link",
                        "error",
                        format!("link '{}' does not exist", &l[1]),
                    ));
                }
            }
        }
        if text.lines().count() > 500 {
            out.push(finding(
                dir,
                "line-limit",
                "warning",
                "more than 500 lines; move details to references/".into(),
            ));
        }
        if ABS.is_match(text) {
            out.push(finding(
                dir,
                "absolute-path",
                "error",
                "contains a machine-specific absolute path".into(),
            ));
        }
        if FOREIGN.is_match(text) {
            out.push(finding(
                dir,
                "foreign-product",
                "error",
                "mentions another product".into(),
            ));
        }
    }
    out
}

pub fn lint(severity: &str, commands: &HashSet<String>) -> Result<Value, CliError> {
    let tools: HashSet<String> = catalog::bundled()
        .iter()
        .filter_map(|t| t["name"].as_str().map(str::to_string))
        .collect();
    let findings = lint_skills(&bundled(), &tools, commands, None);
    let errors = findings.iter().filter(|f| f["severity"] == "error").count();
    let shown: Vec<&Value> = findings
        .iter()
        .filter(|f| severity != "error" || f["severity"] == "error")
        .collect();
    let data = json!({ "skills": bundled().len(), "errors": errors, "findings": shown });
    if errors > 0 {
        return Err(
            CliError::usage("skills.lint_failed", format!("{errors} lint error(s)"))
                .with_details(data),
        );
    }
    Ok(data)
}

use std::path::Path;
use std::process::Command;
use std::sync::LazyLock;

use regex::Regex;
use serde::Serialize;
use serde_json::{Value, json};

use super::locate::Engine;
use crate::core::error::CliError;
use crate::core::exit_code::ExitCode;

/// One compiler or UBT diagnostic.
#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct BuildDiagnostic {
    pub file: String,
    pub line: u32,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub column: Option<u32>,
    pub severity: String,
    pub code: String,
    pub message: String,
}

static MSVC: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"^\s*(?P<file>[^\r\n]+?)\((?P<line>\d+)(?:,(?P<col>\d+))?\)\s*:\s*(?P<sev>fatal error|error|warning)\s+(?P<code>[A-Z]+\d+)\s*:\s*(?P<msg>.*)$").unwrap()
});
static CLANG: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"^\s*(?P<file>[^\r\n:]+(?::[\\/][^\r\n:]+)?):(?P<line>\d+):(?P<col>\d+):\s*(?P<sev>fatal error|error|warning):\s*(?P<msg>.*?)(?:\s*\[(?P<code>-W[^\]]+)\])?$").unwrap()
});
static UBT: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^\s*(?:ERROR|Error):\s*(?P<msg>.+)$").unwrap());

/// Extracts diagnostics from MSVC, clang and UnrealBuildTool output (deduplicated, in order).
pub fn parse_diagnostics(output: &str) -> Vec<BuildDiagnostic> {
    let mut out: Vec<BuildDiagnostic> = Vec::new();
    for line in output.lines() {
        let d = if let Some(c) = MSVC.captures(line) {
            BuildDiagnostic {
                file: c["file"].trim().to_string(),
                line: c["line"].parse().unwrap_or(0),
                column: c.name("col").and_then(|m| m.as_str().parse().ok()),
                severity: severity(&c["sev"]),
                code: c["code"].to_string(),
                message: c["msg"].trim().to_string(),
            }
        } else if let Some(c) = CLANG.captures(line) {
            BuildDiagnostic {
                file: c["file"].trim().to_string(),
                line: c["line"].parse().unwrap_or(0),
                column: c["col"].parse().ok(),
                severity: severity(&c["sev"]),
                code: c
                    .name("code")
                    .map(|m| m.as_str().to_string())
                    .unwrap_or_default(),
                message: c["msg"].trim().to_string(),
            }
        } else if let Some(c) = UBT.captures(line) {
            BuildDiagnostic {
                file: String::new(),
                line: 0,
                column: None,
                severity: "error".into(),
                code: "UBT".into(),
                message: c["msg"].trim().to_string(),
            }
        } else {
            continue;
        };
        if !out.contains(&d) {
            out.push(d);
        }
    }
    out
}

fn severity(s: &str) -> String {
    if s.contains("error") {
        "error"
    } else {
        "warning"
    }
    .to_string()
}

/// Name of the editor target: `<Project>Editor` from Source/*Editor.Target.cs, else the project name.
pub fn editor_target(uproject: &Path) -> String {
    let stem = uproject
        .file_stem()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();
    let source = uproject.parent().unwrap_or(Path::new(".")).join("Source");
    if let Ok(entries) = std::fs::read_dir(source) {
        for e in entries.flatten() {
            let name = e.file_name().to_string_lossy().to_string();
            if let Some(t) = name.strip_suffix(".Target.cs")
                && t.ends_with("Editor")
            {
                return t.to_string();
            }
        }
    }
    format!("{stem}Editor")
}

fn host_platform() -> &'static str {
    if cfg!(windows) {
        "Win64"
    } else if cfg!(target_os = "macos") {
        "Mac"
    } else {
        "Linux"
    }
}

/// Builds the project's editor target. Returns diagnostics; fails with exit code 6 on errors.
pub fn build_editor(engine: &Engine, uproject: &Path) -> Result<Value, CliError> {
    let script = engine.build_script();
    if !script.is_file() {
        return Err(CliError::usage(
            "engine.no_build_tools",
            format!("{} not found", script.display()),
        ));
    }
    let target = editor_target(uproject);
    let started = std::time::Instant::now();
    let out = Command::new(&script)
        .arg(&target)
        .arg(host_platform())
        .arg("Development")
        .arg(format!("-Project={}", uproject.display()))
        .arg("-WaitMutex")
        .arg("-NoHotReloadFromIDE")
        .env("VSLANG", "1033")
        .output()
        .map_err(|e| {
            CliError::internal(
                "build.spawn",
                format!("cannot run {}: {e}", script.display()),
            )
        })?;
    let text = format!(
        "{}{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
    let diagnostics = parse_diagnostics(&text);
    let errors = diagnostics.iter().filter(|d| d.severity == "error").count();
    let tail: Vec<&str> = text
        .lines()
        .rev()
        .take(15)
        .collect::<Vec<_>>()
        .into_iter()
        .rev()
        .collect();
    let data = json!({
        "target": target,
        "succeeded": out.status.success(),
        "seconds": started.elapsed().as_secs(),
        "errors": errors,
        "diagnostics": diagnostics,
        "logTail": tail,
    });
    if out.status.success() {
        Ok(data)
    } else {
        let hint = if text.contains("Live Coding")
            || text.contains("Unable to build while Live Coding is active")
        {
            "close the editor first: unrealengine-cli editor close"
        } else {
            "fix the first error and rebuild: unrealengine-cli editor build"
        };
        Err(CliError::new(
            ExitCode::Build,
            "build.failed",
            format!("{target} failed with {errors} error(s)"),
        )
        .with_hint(hint)
        .with_details(data))
    }
}

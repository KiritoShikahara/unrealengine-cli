//! `hook`: Claude Code hooks (SessionStart context for Unreal projects, and registering it in settings.json).

use std::io::Write;
use std::path::{Path, PathBuf};

use serde_json::{Value, json};

use crate::core::error::CliError;

pub const SESSION_START_COMMAND: &str = "unrealengine-cli hook session-start";

/// Context text for a project, or None when `start` is not inside an Unreal project.
pub fn session_context(start: &Path) -> Option<String> {
    let uproject = crate::core::paths::find_uproject(None, start).ok()?;
    let name = uproject.file_stem()?.to_string_lossy().to_string();
    let engine = std::fs::read_to_string(&uproject)
        .ok()
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .and_then(|v| v["EngineAssociation"].as_str().map(String::from))
        .filter(|s| !s.is_empty());
    let engine = engine.map_or_else(String::new, |e| format!(" (engine {e})"));
    Some(format!(
        "This repository is the Unreal Engine project '{name}'{engine} at {}. \
         Drive the editor with unrealengine-cli (or the ue_* MCP tools) instead of asking the user to click through it: \
         start with the unrealengine-cli-usage skill, and use the unreal-* skills for Blueprints, C++, materials, AI, animation and levels. \
         Follow UE conventions (UCLASS/UPROPERTY reflection, asset prefixes BP_/M_/WBP_, Live Coding for .cpp-only edits).",
        uproject.display()
    ))
}

/// Prints the SessionStart hook output (nothing outside a project) and exits 0.
pub fn session_start(cwd: &Path) -> ! {
    if let Some(ctx) = session_context(cwd) {
        let out = json!({
            "hookSpecificOutput": { "hookEventName": "SessionStart", "additionalContext": ctx }
        });
        let mut stdout = std::io::stdout();
        let _ = writeln!(stdout, "{out}");
        let _ = stdout.flush();
    }
    std::process::exit(0);
}

fn settings_path(client: &str, global: bool, cwd: &Path) -> Result<PathBuf, CliError> {
    if client != "claude-code" {
        return Err(
            CliError::usage("hook.unknown_client", format!("unknown client '{client}'"))
                .with_hint("clients: claude-code"),
        );
    }
    Ok(if global {
        match std::env::var_os("CLAUDE_CONFIG_DIR") {
            Some(d) if !d.is_empty() => PathBuf::from(d).join("settings.json"),
            _ => crate::core::paths::home().join(".claude/settings.json"),
        }
    } else {
        cwd.join(".claude/settings.json")
    })
}

/// Adds the SessionStart hook to `settings`; returns false when it is already there.
pub fn merge_settings(settings: &mut Value) -> Result<bool, CliError> {
    let bad = || CliError::usage("hook.settings_invalid", "settings must be a JSON object");
    let hooks = settings
        .as_object_mut()
        .ok_or_else(bad)?
        .entry("hooks")
        .or_insert_with(|| json!({}));
    let groups = hooks
        .as_object_mut()
        .ok_or_else(bad)?
        .entry("SessionStart")
        .or_insert_with(|| json!([]));
    let groups = groups.as_array_mut().ok_or_else(bad)?;
    let present = groups.iter().any(|g| {
        g["hooks"].as_array().is_some_and(|hs| {
            hs.iter()
                .any(|h| h["command"].as_str() == Some(SESSION_START_COMMAND))
        })
    });
    if present {
        return Ok(false);
    }
    groups.push(json!({
        "matcher": "startup|resume|clear",
        "hooks": [{ "type": "command", "command": SESSION_START_COMMAND, "timeout": 10 }]
    }));
    Ok(true)
}

pub fn install(client: &str, global: bool, cwd: &Path, dry_run: bool) -> Result<Value, CliError> {
    let path = settings_path(client, global, cwd)?;
    let mut v: Value = match std::fs::read_to_string(&path) {
        Ok(t) => serde_json::from_str(&t).map_err(|e| {
            CliError::usage("hook.settings_invalid", format!("{}: {e}", path.display()))
                .with_hint("fix the JSON, then run hook install again")
        })?,
        Err(_) => json!({}),
    };
    let changed = merge_settings(&mut v)?;
    if changed && !dry_run {
        std::fs::create_dir_all(path.parent().unwrap())
            .and_then(|_| std::fs::write(&path, serde_json::to_string_pretty(&v).unwrap() + "\n"))
            .map_err(|e| CliError::internal("io.write", e.to_string()))?;
    }
    Ok(
        json!({ "client": client, "file": path, "changed": changed, "dryRun": dry_run, "command": SESSION_START_COMMAND }),
    )
}

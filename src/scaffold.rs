//! Creates a new, empty Blueprint project.

use std::path::{Path, PathBuf};

use serde_json::{Value, json};

use crate::core::error::CliError;

/// Unreal project names: a letter, then letters, digits or `_`, at most 20 characters.
pub fn validate_name(name: &str) -> Result<(), CliError> {
    let ok = name.len() <= 20
        && name.starts_with(|c: char| c.is_ascii_alphabetic())
        && name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_');
    if ok {
        Ok(())
    } else {
        Err(CliError::usage(
            "project.invalid_name",
            format!("'{name}' is not a valid project name"),
        )
        .with_hint("start with a letter; use only letters, digits and _; at most 20 characters"))
    }
}

fn files(name: &str, engine: &str) -> Vec<(PathBuf, String)> {
    let uproject = json!({
        "FileVersion": 3,
        "EngineAssociation": engine,
        "Category": "",
        "Description": "",
    });
    vec![
        (
            PathBuf::from(format!("{name}.uproject")),
            serde_json::to_string_pretty(&uproject).expect("json serializes") + "\n",
        ),
        (
            PathBuf::from("Config/DefaultEngine.ini"),
            "[/Script/EngineSettings.GameMapsSettings]\n".to_string(),
        ),
        (
            PathBuf::from("Config/DefaultGame.ini"),
            format!("[/Script/EngineSettings.GeneralProjectSettings]\nProjectName={name}\n"),
        ),
    ]
}

/// Creates `<parent>/<name>/` for `engine` (an EngineAssociation). With `dry_run`, only reports.
pub fn create(parent: &Path, name: &str, engine: &str, dry_run: bool) -> Result<Value, CliError> {
    validate_name(name)?;
    let dir = parent.join(name);
    if dir.read_dir().is_ok_and(|mut d| d.next().is_some()) {
        return Err(CliError::usage(
            "project.exists",
            format!("{} already exists and is not empty", dir.display()),
        ));
    }
    let files = files(name, engine);
    let mut changes: Vec<Value> = files
        .iter()
        .map(|(p, _)| json!({ "action": "create", "path": dir.join(p).display().to_string() }))
        .collect();
    changes.push(json!({ "action": "create", "path": dir.join("Content").display().to_string() }));
    if !dry_run {
        let io = |e: std::io::Error| CliError::usage("project.write_failed", e.to_string());
        std::fs::create_dir_all(dir.join("Content")).map_err(io)?;
        for (p, text) in &files {
            let path = dir.join(p);
            std::fs::create_dir_all(path.parent().unwrap()).map_err(io)?;
            std::fs::write(path, text).map_err(io)?;
        }
    }
    Ok(json!({
        "uproject": dir.join(format!("{name}.uproject")).display().to_string(),
        "engine": engine,
        "dryRun": dry_run,
        "changes": changes,
    }))
}

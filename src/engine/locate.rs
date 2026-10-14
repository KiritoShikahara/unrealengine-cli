use std::path::{Path, PathBuf};
use std::process::Command;

use serde::Serialize;
use serde_json::Value;

use crate::core::error::CliError;

/// An installed or source-built engine.
#[derive(Debug, Clone, Serialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct Engine {
    /// Version (`5.7`) or build GUID / custom id.
    pub id: String,
    pub root: PathBuf,
}

impl Engine {
    pub fn editor_exe(&self) -> PathBuf {
        let bin = self.root.join("Engine/Binaries");
        if cfg!(windows) {
            bin.join("Win64/UnrealEditor.exe")
        } else if cfg!(target_os = "macos") {
            bin.join("Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor")
        } else {
            bin.join("Linux/UnrealEditor")
        }
    }

    pub fn editor_cmd_exe(&self) -> PathBuf {
        if cfg!(windows) {
            self.root.join("Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
        } else {
            self.editor_exe()
        }
    }

    pub fn build_script(&self) -> PathBuf {
        let dir = self.root.join("Engine/Build/BatchFiles");
        if cfg!(windows) {
            dir.join("Build.bat")
        } else if cfg!(target_os = "macos") {
            dir.join("Mac/Build.sh")
        } else {
            dir.join("Linux/Build.sh")
        }
    }

    /// `Major.Minor.Patch` from Engine/Build/Build.version, if readable.
    pub fn version(&self) -> Option<String> {
        let text = std::fs::read_to_string(self.root.join("Engine/Build/Build.version")).ok()?;
        let v: Value = serde_json::from_str(&text).ok()?;
        Some(format!(
            "{}.{}.{}",
            v["MajorVersion"].as_u64()?,
            v["MinorVersion"].as_u64()?,
            v["PatchVersion"].as_u64()?
        ))
    }
}

/// `EngineAssociation` of a .uproject.
pub fn engine_association(uproject: &Path) -> Result<String, CliError> {
    let text = std::fs::read_to_string(uproject).map_err(|e| {
        CliError::usage("project.unreadable", format!("{}: {e}", uproject.display()))
    })?;
    let v: Value = serde_json::from_str(&text).map_err(|e| {
        CliError::usage(
            "project.invalid",
            format!("{} is not valid JSON: {e}", uproject.display()),
        )
    })?;
    Ok(v["EngineAssociation"]
        .as_str()
        .unwrap_or_default()
        .to_string())
}

/// Launcher installs from `LauncherInstalled.dat` (only `UE_x.y` entries).
pub fn parse_launcher_installed(text: &str) -> Vec<Engine> {
    let Ok(v) = serde_json::from_str::<Value>(text) else {
        return Vec::new();
    };
    let mut out: Vec<Engine> = v["InstallationList"]
        .as_array()
        .into_iter()
        .flatten()
        .filter_map(|e| {
            let app = e["AppName"].as_str()?;
            let ver = app.strip_prefix("UE_")?;
            Some(Engine {
                id: ver.to_string(),
                root: PathBuf::from(e["InstallLocation"].as_str()?),
            })
        })
        .collect();
    out.sort_by(|a, b| a.id.cmp(&b.id));
    out.dedup_by(|a, b| a.id == b.id);
    out
}

/// Value lines of `reg query` output: (name, data).
pub fn parse_reg_query(text: &str) -> Vec<(String, String)> {
    text.lines()
        .filter_map(|l| {
            let l = l.trim();
            let (name, rest) = l.split_once("REG_SZ")?;
            Some((name.trim().to_string(), rest.trim().to_string()))
        })
        .collect()
}

fn launcher_dat() -> Option<PathBuf> {
    if cfg!(windows) {
        let base = std::env::var_os("PROGRAMDATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("C:/ProgramData"));
        Some(base.join("Epic/UnrealEngineLauncher/LauncherInstalled.dat"))
    } else if cfg!(target_os = "macos") {
        Some(
            crate::core::paths::home().join(
                "Library/Application Support/Epic/UnrealEngineLauncher/LauncherInstalled.dat",
            ),
        )
    } else {
        None
    }
}

fn source_builds() -> Vec<Engine> {
    if cfg!(windows) {
        let out = Command::new("reg")
            .args(["query", r"HKCU\Software\Epic Games\Unreal Engine\Builds"])
            .output();
        let Ok(out) = out else { return Vec::new() };
        parse_reg_query(&String::from_utf8_lossy(&out.stdout))
            .into_iter()
            .map(|(id, root)| Engine {
                id,
                root: PathBuf::from(root),
            })
            .collect()
    } else {
        let file = crate::core::paths::home().join(".config/Epic/UnrealEngine/Install.ini");
        std::fs::read_to_string(file)
            .unwrap_or_default()
            .lines()
            .filter_map(|l| l.split_once('='))
            .filter(|(k, _)| !k.starts_with('['))
            .map(|(k, v)| Engine {
                id: k.trim().to_string(),
                root: PathBuf::from(v.trim()),
            })
            .collect()
    }
}

/// Every engine this machine knows about.
pub fn list_engines() -> Vec<Engine> {
    let mut all = launcher_dat()
        .and_then(|p| std::fs::read_to_string(p).ok())
        .map(|t| parse_launcher_installed(&t))
        .unwrap_or_default();
    all.extend(source_builds());
    all.retain(|e| e.root.join("Engine").is_dir());
    all
}

/// Resolves the engine for a project: `UNREAL_CLI_ENGINE_DIR`, then the association.
pub fn resolve(uproject: &Path) -> Result<Engine, CliError> {
    if let Some(dir) = std::env::var_os("UNREAL_CLI_ENGINE_DIR").filter(|v| !v.is_empty()) {
        return Ok(Engine {
            id: "override".into(),
            root: PathBuf::from(dir),
        });
    }
    let assoc = engine_association(uproject)?;
    resolve_association(&assoc, &list_engines()).ok_or_else(|| {
        CliError::usage(
            "engine.not_found",
            format!(
                "engine '{assoc}' for {} is not installed",
                uproject.display()
            ),
        )
        .with_hint("install it from the Epic Games Launcher, or set UNREAL_CLI_ENGINE_DIR")
    })
}

/// Matches an association (`5.7`, a GUID, or a path) against known engines.
pub fn resolve_association(assoc: &str, engines: &[Engine]) -> Option<Engine> {
    if let Some(e) = engines.iter().find(|e| e.id.eq_ignore_ascii_case(assoc)) {
        return Some(e.clone());
    }
    let p = PathBuf::from(assoc);
    if !assoc.is_empty() && p.join("Engine").is_dir() {
        return Some(Engine {
            id: assoc.to_string(),
            root: p,
        });
    }
    if assoc.is_empty() {
        return engines.iter().max_by(|a, b| a.id.cmp(&b.id)).cloned();
    }
    None
}

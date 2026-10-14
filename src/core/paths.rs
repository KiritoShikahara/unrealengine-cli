use std::path::{Path, PathBuf};

use super::error::CliError;

/// Per-user data directory shared with the editor plugin (instance files, config, caches).
pub fn data_dir() -> PathBuf {
    if let Some(p) = std::env::var_os("UNREAL_CLI_HOME").filter(|v| !v.is_empty()) {
        return PathBuf::from(p);
    }
    #[cfg(windows)]
    {
        let base = std::env::var_os("LOCALAPPDATA")
            .map(PathBuf::from)
            .unwrap_or_default();
        base.join("unreal-cli")
    }
    #[cfg(target_os = "macos")]
    {
        home().join("Library/Application Support/unreal-cli")
    }
    #[cfg(all(unix, not(target_os = "macos")))]
    {
        match std::env::var_os("XDG_DATA_HOME").filter(|v| !v.is_empty()) {
            Some(x) => PathBuf::from(x).join("unreal-cli"),
            None => home().join(".local/share/unreal-cli"),
        }
    }
}

pub fn home() -> PathBuf {
    std::env::var_os("HOME")
        .or_else(|| std::env::var_os("USERPROFILE"))
        .map(PathBuf::from)
        .unwrap_or_default()
}

pub fn instances_dir() -> PathBuf {
    data_dir().join("instances")
}

/// Resolves `--project` (a .uproject or its folder) or searches upward from `start`.
pub fn find_uproject(explicit: Option<&Path>, start: &Path) -> Result<PathBuf, CliError> {
    if let Some(p) = explicit {
        let p = if p.is_absolute() {
            p.to_path_buf()
        } else {
            start.join(p)
        };
        if p.extension()
            .is_some_and(|e| e.eq_ignore_ascii_case("uproject"))
        {
            return if p.is_file() {
                Ok(normalize(&p))
            } else {
                Err(CliError::usage(
                    "project.not_found",
                    format!("{} does not exist", p.display()),
                ))
            };
        }
        return uproject_in(&p).ok_or_else(|| {
            CliError::usage(
                "project.not_found",
                format!("no .uproject in {}", p.display()),
            )
        });
    }
    let mut dir = Some(start);
    while let Some(d) = dir {
        if let Some(found) = uproject_in(d) {
            return Ok(found);
        }
        dir = d.parent();
    }
    Err(CliError::usage(
        "project.not_found",
        "no .uproject found in this directory or its parents",
    )
    .with_hint("pass --project <path to .uproject>"))
}

fn uproject_in(dir: &Path) -> Option<PathBuf> {
    let mut found: Vec<PathBuf> = std::fs::read_dir(dir)
        .ok()?
        .flatten()
        .map(|e| e.path())
        .filter(|p| {
            p.extension()
                .is_some_and(|e| e.eq_ignore_ascii_case("uproject"))
                && p.is_file()
        })
        .collect();
    found.sort();
    found.into_iter().next().map(|p| normalize(&p))
}

/// Absolute path with forward slashes, without the `\\?\` prefix.
pub fn normalize(p: &Path) -> PathBuf {
    let abs = std::fs::canonicalize(p).unwrap_or_else(|_| p.to_path_buf());
    let s = abs.to_string_lossy().replace('\\', "/");
    PathBuf::from(s.strip_prefix("//?/").unwrap_or(&s).to_string())
}

/// Compares two project paths the way the host file system does.
pub fn same_path(a: &Path, b: &Path) -> bool {
    let (a, b) = (normalize(a), normalize(b));
    if cfg!(windows) || cfg!(target_os = "macos") {
        a.to_string_lossy().to_lowercase() == b.to_string_lossy().to_lowercase()
    } else {
        a == b
    }
}

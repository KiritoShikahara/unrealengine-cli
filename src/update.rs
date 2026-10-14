//! `self update`: download a release, verify it against SHA256SUMS, replace the running binary.

use std::path::Path;
use std::process::Command;

use serde_json::{Value, json};
use sha2::{Digest, Sha256};

use crate::core::error::CliError;

pub const DEFAULT_REPO: &str = "KiritoShikahara/unrealengine-cli";

/// Release asset name for this platform.
pub fn asset_name() -> &'static str {
    if cfg!(all(windows, target_arch = "x86_64")) {
        "unreal-cli-x86_64-pc-windows-msvc.zip"
    } else if cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        "unreal-cli-aarch64-apple-darwin.tar.gz"
    } else if cfg!(target_os = "macos") {
        "unreal-cli-x86_64-apple-darwin.tar.gz"
    } else {
        "unreal-cli-x86_64-unknown-linux-gnu.tar.gz"
    }
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    Sha256::digest(bytes)
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect()
}

/// Checks `bytes` against the line for `file` in a SHA256SUMS text (`<hex>  <file>`).
pub fn verify(bytes: &[u8], sums: &str, file: &str) -> Result<(), CliError> {
    let expected = sums
        .lines()
        .find_map(|l| {
            let mut parts = l.split_whitespace();
            let hash = parts.next()?;
            let name = parts.next()?.trim_start_matches('*');
            (name == file).then(|| hash.to_lowercase())
        })
        .ok_or_else(|| {
            CliError::internal(
                "update.no_checksum",
                format!("SHA256SUMS has no entry for {file}"),
            )
        })?;
    let actual = sha256_hex(bytes);
    if actual != expected {
        return Err(CliError::internal(
            "update.checksum_mismatch",
            format!("{file}: expected {expected}, got {actual}"),
        )
        .with_hint("the download is corrupt or tampered with; nothing was changed"));
    }
    Ok(())
}

fn fetch(url: &str) -> Result<Vec<u8>, CliError> {
    let out = Command::new("curl")
        .args(["-fsSL", url])
        .output()
        .map_err(|e| CliError::internal("update.curl", format!("curl is required: {e}")))?;
    if !out.status.success() {
        return Err(CliError::connect(
            "update.download_failed",
            format!("download failed: {url}"),
        ));
    }
    Ok(out.stdout)
}

fn extract(archive: &Path, dest: &Path) -> Result<(), CliError> {
    let ok = Command::new("tar")
        .arg("-xf")
        .arg(archive)
        .arg("-C")
        .arg(dest)
        .status()
        .map(|s| s.success())
        .unwrap_or(false);
    if ok {
        Ok(())
    } else {
        Err(CliError::internal(
            "update.extract_failed",
            "could not extract the archive",
        ))
    }
}

/// Tag of the latest release (from the `releases/latest` redirect), or None when offline.
pub fn latest_tag() -> Option<String> {
    let repo = std::env::var("UNREAL_CLI_REPO").unwrap_or_else(|_| DEFAULT_REPO.to_string());
    let out = Command::new("curl")
        .args([
            "-fsSI",
            "-m",
            "3",
            &format!("https://github.com/{repo}/releases/latest"),
        ])
        .output()
        .ok()?;
    parse_latest_location(&String::from_utf8_lossy(&out.stdout))
}

/// Extracts `vX.Y.Z` from a `location: .../releases/tag/vX.Y.Z` response header.
pub fn parse_latest_location(headers: &str) -> Option<String> {
    headers.lines().find_map(|l| {
        let (k, v) = l.split_once(':')?;
        if !k.trim().eq_ignore_ascii_case("location") {
            return None;
        }
        v.trim()
            .rsplit_once("/tag/")
            .map(|(_, t)| t.trim().to_string())
    })
}

/// True when `tag` (vX.Y.Z) is newer than this binary.
pub fn is_newer(tag: &str) -> bool {
    let parse = |s: &str| -> Vec<u64> {
        s.trim_start_matches('v')
            .split('.')
            .map(|p| p.parse().unwrap_or(0))
            .collect()
    };
    parse(tag) > parse(env!("CARGO_PKG_VERSION"))
}

/// Update check honouring UNREAL_CLI_NO_AUTO_UPDATE.
pub fn check() -> Value {
    if std::env::var_os("UNREAL_CLI_NO_AUTO_UPDATE").is_some() {
        return json!({ "checked": false, "reason": "UNREAL_CLI_NO_AUTO_UPDATE is set" });
    }
    match latest_tag() {
        Some(t) => {
            json!({ "checked": true, "current": env!("CARGO_PKG_VERSION"), "latest": t, "updateAvailable": is_newer(&t) })
        }
        None => {
            json!({ "checked": false, "reason": "no release information (offline or no releases)" })
        }
    }
}

pub fn self_update(version: Option<&str>, dry_run: bool) -> Result<Value, CliError> {
    let repo = std::env::var("UNREAL_CLI_REPO").unwrap_or_else(|_| DEFAULT_REPO.to_string());
    let base = match version {
        Some(v) => format!("https://github.com/{repo}/releases/download/{v}"),
        None => format!("https://github.com/{repo}/releases/latest/download"),
    };
    let asset = asset_name();
    if dry_run {
        return Ok(json!({ "dryRun": true, "from": format!("{base}/{asset}") }));
    }
    let sums = String::from_utf8_lossy(&fetch(&format!("{base}/SHA256SUMS"))?).to_string();
    let bytes = fetch(&format!("{base}/{asset}"))?;
    verify(&bytes, &sums, asset)?;
    let tmp = std::env::temp_dir().join(format!("unreal-cli-update-{}", std::process::id()));
    std::fs::create_dir_all(&tmp).map_err(|e| CliError::internal("io.write", e.to_string()))?;
    let archive = tmp.join(asset);
    std::fs::write(&archive, &bytes).map_err(|e| CliError::internal("io.write", e.to_string()))?;
    extract(&archive, &tmp)?;
    let exe_name = if cfg!(windows) {
        "unreal-cli.exe"
    } else {
        "unreal-cli"
    };
    let new_exe = tmp.join(exe_name);
    let current =
        std::env::current_exe().map_err(|e| CliError::internal("update.self", e.to_string()))?;
    let old = current.with_extension("old");
    let _ = std::fs::remove_file(&old);
    std::fs::rename(&current, &old)
        .and_then(|_| std::fs::copy(&new_exe, &current).map(|_| ()))
        .map_err(|e| CliError::internal("update.replace_failed", e.to_string()))?;
    Ok(json!({ "updated": true, "from": env!("CARGO_PKG_VERSION"), "binary": current }))
}

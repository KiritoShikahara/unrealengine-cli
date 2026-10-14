use std::path::Path;
use std::time::Duration;

use serde_json::{Value, json};

use crate::engine::{editor, locate};
use crate::setup;
use crate::transport::client::BridgeClient;

fn check(name: &str, ok: bool, detail: impl Into<String>, hint: &str) -> Value {
    let mut v = json!({ "name": name, "ok": ok, "detail": detail.into() });
    if !ok && !hint.is_empty() {
        v["hint"] = json!(hint);
    }
    v
}

/// Environment checks; never fails, reports each problem with a hint.
pub fn run(uproject: Option<&Path>) -> Value {
    let mut checks = Vec::new();
    checks.push(check("cli", true, env!("CARGO_PKG_VERSION"), ""));
    let engines = locate::list_engines();
    checks.push(check(
        "engines",
        !engines.is_empty(),
        engines
            .iter()
            .map(|e| e.id.clone())
            .collect::<Vec<_>>()
            .join(", "),
        "install Unreal Engine 5.5-5.8 from the Epic Games Launcher",
    ));
    if cfg!(windows) {
        let vswhere =
            Path::new("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe");
        let vc = vswhere.is_file()
            && std::process::Command::new(vswhere)
                .args([
                    "-latest",
                    "-products",
                    "*",
                    "-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-property",
                    "installationPath",
                ])
                .output()
                .map(|o| !o.stdout.is_empty())
                .unwrap_or(false);
        checks.push(check(
            "build_tools",
            vc,
            if vc {
                "Visual Studio C++ tools found"
            } else {
                "not found"
            },
            "install Visual Studio 2022 with the 'Game development with C++' workload",
        ));
    }
    let Some(up) = uproject else {
        checks.push(check(
            "project",
            false,
            "no .uproject found",
            "run inside a project or pass --project",
        ));
        return json!({ "checks": checks, "healthy": false });
    };
    checks.push(check("project", true, up.to_string_lossy(), ""));
    match locate::resolve(up) {
        Ok(e) => checks.push(check(
            "engine",
            true,
            format!(
                "{} ({})",
                e.version().unwrap_or(e.id.clone()),
                e.root.display()
            ),
            "",
        )),
        Err(e) => checks.push(check(
            "engine",
            false,
            e.message,
            e.hint.as_deref().unwrap_or(""),
        )),
    }
    let dir = up.parent().unwrap_or(Path::new("."));
    let plugin = dir
        .join("Plugins")
        .join(setup::PLUGIN_NAME)
        .join("UnrealCliBridge.uplugin");
    let installed = std::fs::read_to_string(&plugin)
        .ok()
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .and_then(|v| v["VersionName"].as_str().map(str::to_string));
    let bundled = setup::bundled_plugin_version();
    checks.push(match &installed {
        None => check("plugin", false, "not installed", "unreal-cli setup"),
        Some(v) if *v != bundled => check(
            "plugin",
            false,
            format!("{v} installed, {bundled} bundled"),
            "update: unreal-cli setup --build",
        ),
        Some(v) => check("plugin", true, v.clone(), ""),
    });
    let enabled = std::fs::read_to_string(up)
        .ok()
        .and_then(|t| setup::enable_in_uproject(&t).ok())
        .map(|changed| changed.is_none())
        .unwrap_or(false);
    checks.push(check(
        "plugin_enabled",
        enabled,
        if enabled {
            "enabled in .uproject"
        } else {
            "not enabled"
        },
        "unreal-cli setup",
    ));
    match editor::running_for(up) {
        Some(i) => {
            let ping = BridgeClient::connect(i.addr(), &i.token, Duration::from_secs(5))
                .and_then(|mut c| c.call("system_ping", json!({}), false, 5000));
            checks.push(check(
                "editor",
                ping.is_ok(),
                format!("pid {} ({})", i.pid, i.engine_version),
                "the bridge did not answer; restart the editor",
            ));
        }
        None => checks.push(check(
            "editor",
            false,
            "not running",
            "unreal-cli editor open",
        )),
    }
    let healthy = checks
        .iter()
        .all(|c| c["ok"] == true || c["name"] == "editor");
    json!({ "checks": checks, "healthy": healthy })
}

use std::path::Path;
use std::time::Duration;

use serde_json::{Value, json};

use crate::core::paths;
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

/// True when DefaultEngine.ini turns distributed shader compilation off: `r.ShaderCompiler.AllowDistributedCompilation=0`
/// (UE 5.5+) or the older `r.XGEShaderCompile=0`.
pub fn distributed_shaders_disabled(engine_ini: &str) -> bool {
    engine_ini.lines().any(|l| {
        let l = l.replace(' ', "");
        l.eq_ignore_ascii_case("r.ShaderCompiler.AllowDistributedCompilation=0")
            || l.eq_ignore_ascii_case("r.XGEShaderCompile=0")
    })
}

fn xge_disabled(uproject: &Path) -> bool {
    let ini = uproject
        .parent()
        .unwrap_or(Path::new("."))
        .join("Config/DefaultEngine.ini");
    std::fs::read_to_string(ini)
        .map(|t| distributed_shaders_disabled(&t))
        .unwrap_or(false)
}

/// Markdown environment summary for bug reports; the user's home directory is shown as `~`.
pub fn report(doctor: &Value, uproject: Option<&Path>) -> String {
    let engine = uproject
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|s| serde_json::from_str::<Value>(&s).ok())
        .and_then(|v| v["EngineAssociation"].as_str().map(str::to_string))
        .unwrap_or_else(|| "unknown (no .uproject found)".into());
    let mut md = format!(
        "## Environment\n\n- unrealengine-cli {}\n- OS: {} {}\n- Engine version (project): {}\n- Healthy: {}\n\n| check | result | detail |\n|---|---|---|\n",
        env!("CARGO_PKG_VERSION"),
        std::env::consts::OS,
        std::env::consts::ARCH,
        engine,
        if doctor["healthy"] == true {
            "yes"
        } else {
            "no"
        },
    );
    let home = paths::home().to_string_lossy().to_string();
    for c in doctor["checks"].as_array().into_iter().flatten() {
        let detail = c["detail"].as_str().unwrap_or("").replace('|', "/");
        let detail = if home.len() > 1 {
            detail.replace(&home, "~")
        } else {
            detail
        };
        md.push_str(&format!(
            "| {} | {} | {} |\n",
            c["name"].as_str().unwrap_or("?"),
            if c["ok"] == true { "OK" } else { "NG" },
            detail
        ));
    }
    md
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
    if cfg!(windows) && std::env::var_os("XGE_INSTALL_DIR").is_some()
        || Path::new("C:/Program Files (x86)/Incredibuild/xgConsole.exe").is_file()
    {
        checks.push(check(
            "shader_compile",
            uproject.is_some_and(xge_disabled),
            "XGE / Incredibuild is installed; the editor sends shader jobs to it",
            "if shader compilation or cooking never finishes, add r.ShaderCompiler.AllowDistributedCompilation=0 under [ConsoleVariables] in Config/DefaultEngine.ini",
        ));
    }
    let upd = crate::update::check();
    let newer = upd["updateAvailable"] == true;
    checks.push(check(
        "update",
        !newer,
        if newer {
            format!("{} available", upd["latest"].as_str().unwrap_or_default())
        } else {
            upd.get("reason")
                .and_then(|r| r.as_str())
                .unwrap_or("up to date")
                .to_string()
        },
        "unrealengine-cli self update",
    ));
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
        None => check("plugin", false, "not installed", "unrealengine-cli setup"),
        Some(v) if *v != bundled => check(
            "plugin",
            false,
            format!("{v} installed, {bundled} bundled"),
            "update: unrealengine-cli setup --build",
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
        "unrealengine-cli setup",
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
            "unrealengine-cli editor open",
        )),
    }
    let healthy = checks
        .iter()
        .all(|c| c["ok"] == true || c["name"] == "editor");
    json!({ "checks": checks, "healthy": healthy })
}

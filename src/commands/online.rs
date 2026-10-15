use std::path::{Path, PathBuf};
use std::sync::LazyLock;

use clap::{Subcommand, ValueEnum};
use regex::Regex;
use serde_json::{Map, Value, json};

use super::Ctx;
use crate::core::error::CliError;
use crate::core::ini;
use crate::setup;

#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Subsystem {
    Null,
    Eos,
    Steam,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum SessionAction {
    Create,
    Find,
    Join,
    Destroy,
}

#[derive(Debug, Subcommand)]
pub enum OnlineCmd {
    /// Enable the subsystem's plugins and write its settings to Config/DefaultEngine.ini
    Configure {
        #[arg(long, value_enum)]
        subsystem: Subsystem,
        /// KEY=VALUE (EOS: ProductId, SandboxId, DeploymentId, ClientId, ClientSecret, EncryptionKey,
        /// ArtifactName; Steam: SteamDevAppId). Secret values are never printed
        #[arg(long = "set", value_name = "KEY=VALUE")]
        set: Vec<String>,
    },
    /// Check the configured subsystem: missing keys and plugins (secrets shown as ***)
    Check,
    /// Session in a play world: create, find, join (a find result) or destroy
    Session {
        #[arg(value_enum)]
        action: SessionAction,
        /// Play world: standalone, standalone:2, server, client:1, ... (see pie worlds)
        #[arg(long)]
        world: Option<String>,
        /// Session name (default GameSession)
        #[arg(long)]
        name: Option<String>,
        #[arg(long)]
        max_players: Option<u32>,
        /// Use the online service instead of LAN
        #[arg(long)]
        online: bool,
        /// Find result to join (default 0)
        #[arg(long)]
        index: Option<u32>,
        /// After joining, travel the world's player to the session
        #[arg(long)]
        travel: bool,
        /// Seconds to wait for the subsystem (default 10)
        #[arg(long)]
        timeout: Option<f64>,
    },
}

const SERVICE_SECTION: &str = "OnlineSubsystem";
const EOS_SECTION: &str = "/Script/OnlineSubsystemEOS.EOSSettings";
const EOS_KEYS: [&str; 7] = [
    "ArtifactName",
    "ClientId",
    "ClientSecret",
    "ProductId",
    "SandboxId",
    "DeploymentId",
    "EncryptionKey",
];
const SECRETS: [&str; 2] = ["ClientSecret", "EncryptionKey"];

struct Spec {
    service: &'static str,
    plugins: &'static [&'static str],
    /// Plugins the engine enables by default (enabled unless the project disables them).
    default_plugins: &'static [&'static str],
    fixed: &'static [(&'static str, &'static str, &'static str)],
    keys: &'static [&'static str],
    required: &'static [&'static str],
}

fn spec(s: Subsystem) -> Spec {
    const DEFAULTS: &[&str] = &[
        "OnlineSubsystem",
        "OnlineSubsystemUtils",
        "OnlineSubsystemNull",
    ];
    match s {
        Subsystem::Null => Spec {
            service: "Null",
            plugins: &[
                "OnlineSubsystem",
                "OnlineSubsystemUtils",
                "OnlineSubsystemNull",
            ],
            default_plugins: DEFAULTS,
            fixed: &[],
            keys: &[],
            required: &[],
        },
        Subsystem::Eos => Spec {
            service: "EOS",
            plugins: &[
                "OnlineSubsystem",
                "OnlineSubsystemUtils",
                "OnlineSubsystemEOS",
                "EOSShared",
            ],
            default_plugins: DEFAULTS,
            fixed: &[
                ("OnlineSubsystemEOS", "bEnabled", "true"),
                (EOS_SECTION, "bEnableOverlay", "true"),
            ],
            keys: &EOS_KEYS,
            required: &[
                "ProductId",
                "SandboxId",
                "DeploymentId",
                "ClientId",
                "ClientSecret",
            ],
        },
        Subsystem::Steam => Spec {
            service: "Steam",
            plugins: &[
                "OnlineSubsystem",
                "OnlineSubsystemUtils",
                "OnlineSubsystemSteam",
                "SteamShared",
            ],
            default_plugins: DEFAULTS,
            fixed: &[
                ("OnlineSubsystemSteam", "bEnabled", "true"),
                ("OnlineSubsystemSteam", "bInitServerOnClient", "true"),
                (
                    "/Script/OnlineSubsystemSteam.SteamNetDriver",
                    "NetConnectionClassName",
                    "\"OnlineSubsystemSteam.SteamNetConnection\"",
                ),
            ],
            keys: &["SteamDevAppId"],
            required: &["SteamDevAppId"],
        },
    }
}

fn name_of(s: Subsystem) -> &'static str {
    match s {
        Subsystem::Null => "null",
        Subsystem::Eos => "eos",
        Subsystem::Steam => "steam",
    }
}

fn mask(key: &str, value: &str) -> String {
    if SECRETS.contains(&key) && !value.is_empty() {
        "***".into()
    } else {
        value.into()
    }
}

static ARTIFACT_FIELD: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r#"(\w+)="([^"]*)""#).expect("valid regex"));

/// Fields of the EOS artifact line `+Artifacts=(ArtifactName="..",ClientId="..",...)`.
fn artifact_fields(text: &str) -> Map<String, Value> {
    let line = ini::get(text, EOS_SECTION, "+Artifacts").unwrap_or_default();
    ARTIFACT_FIELD
        .captures_iter(&line)
        .map(|c| (c[1].to_string(), json!(c[2].to_string())))
        .collect()
}

fn artifact_line(fields: &Map<String, Value>) -> String {
    let parts: Vec<String> = EOS_KEYS
        .iter()
        .filter_map(|k| {
            fields
                .get(*k)
                .and_then(Value::as_str)
                .map(|v| format!("{k}=\"{v}\""))
        })
        .collect();
    format!("({})", parts.join(","))
}

fn ini_path(up: &Path) -> PathBuf {
    up.parent()
        .unwrap_or(Path::new("."))
        .join("Config/DefaultEngine.ini")
}

/// Current values of a subsystem's keys (unmasked).
fn current_values(s: Subsystem, text: &str) -> Map<String, Value> {
    match s {
        Subsystem::Eos => artifact_fields(text),
        Subsystem::Steam => ini::get(text, "OnlineSubsystemSteam", "SteamDevAppId")
            .map(|v| {
                [("SteamDevAppId".to_string(), json!(v))]
                    .into_iter()
                    .collect()
            })
            .unwrap_or_default(),
        Subsystem::Null => Map::new(),
    }
}

pub fn run(ctx: &mut Ctx, cmd: OnlineCmd) -> Result<Value, CliError> {
    match cmd {
        OnlineCmd::Configure { subsystem, set } => configure(ctx, subsystem, &set),
        OnlineCmd::Check => check(ctx),
        OnlineCmd::Session {
            action,
            world,
            name,
            max_players,
            online,
            index,
            travel,
            timeout,
        } => {
            let mut a = Map::new();
            let action = match action {
                SessionAction::Create => "create",
                SessionAction::Find => "find",
                SessionAction::Join => "join",
                SessionAction::Destroy => "destroy",
            };
            a.insert("action".into(), json!(action));
            let mut put = |k: &str, v: Option<Value>| {
                if let Some(v) = v {
                    a.insert(k.into(), v);
                }
            };
            put("world", world.map(Value::from));
            put("sessionName", name.map(Value::from));
            put("maxPlayers", max_players.map(Value::from));
            put("lan", online.then_some(json!(false)));
            put("index", index.map(Value::from));
            put("travel", travel.then_some(json!(true)));
            put("timeoutSeconds", timeout.map(Value::from));
            let wait_ms = ((timeout.unwrap_or(10.0) + 15.0) * 1000.0) as u64;
            ctx.call_with_timeout("online_session", Value::Object(a), wait_ms)
        }
    }
}

fn configure(ctx: &mut Ctx, s: Subsystem, set: &[String]) -> Result<Value, CliError> {
    let spec = spec(s);
    let mut given = Vec::new();
    for kv in set {
        let (k, v) = kv.split_once('=').ok_or_else(|| {
            CliError::usage("online.bad_setting", format!("'{kv}' is not KEY=VALUE"))
        })?;
        let key = spec
            .keys
            .iter()
            .find(|x| x.eq_ignore_ascii_case(k.trim()))
            .ok_or_else(|| {
                CliError::usage(
                    "online.unknown_key",
                    format!("'{}' is not a {} setting", k.trim(), name_of(s)),
                )
                .with_hint(if spec.keys.is_empty() {
                    "this subsystem takes no settings".to_string()
                } else {
                    format!("keys: {}", spec.keys.join(", "))
                })
            })?;
        given.push((*key, v.trim().to_string()));
    }

    let up = ctx.uproject()?;
    let ini_file = ini_path(&up);
    let before = std::fs::read_to_string(&ini_file).unwrap_or_default();
    let mut text = ini::set(
        &before,
        SERVICE_SECTION,
        "DefaultPlatformService",
        spec.service,
    );
    let mut changes = vec![
        json!({ "section": SERVICE_SECTION, "key": "DefaultPlatformService", "value": spec.service }),
    ];
    for (section, key, value) in spec.fixed {
        text = ini::set(&text, section, key, value);
        changes.push(json!({ "section": section, "key": key, "value": value }));
    }
    match s {
        Subsystem::Eos if !given.is_empty() => {
            let mut fields = artifact_fields(&text);
            for (k, v) in &given {
                fields.insert((*k).to_string(), json!(v));
                changes.push(json!({ "section": EOS_SECTION, "key": format!("Artifacts.{k}"), "value": mask(k, v) }));
            }
            let artifact = fields
                .entry("ArtifactName")
                .or_insert_with(|| json!("Default"))
                .as_str()
                .unwrap_or("Default")
                .to_string();
            text = ini::set(&text, EOS_SECTION, "DefaultArtifactName", &artifact);
            text = ini::set(&text, EOS_SECTION, "+Artifacts", &artifact_line(&fields));
        }
        _ => {
            for (k, v) in &given {
                text = ini::set(&text, "OnlineSubsystemSteam", k, v);
                changes.push(
                    json!({ "section": "OnlineSubsystemSteam", "key": k, "value": mask(k, v) }),
                );
            }
        }
    }

    let uproject_text = std::fs::read_to_string(&up)
        .map_err(|e| CliError::usage("project.invalid", format!("{}: {e}", up.display())))?;
    let new_uproject = setup::enable_plugins_in_uproject(&uproject_text, spec.plugins)?;
    let data = json!({
        "subsystem": name_of(s),
        "ini": ini_file,
        "settings": changes,
        "plugins": spec.plugins,
        "uprojectChanged": new_uproject.is_some(),
    });
    if ctx.global.dry_run {
        let mut d = data;
        d["dryRun"] = json!(true);
        return Ok(d);
    }
    let write = |p: &Path, t: &str| {
        if let Some(dir) = p.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        std::fs::write(p, t)
            .map_err(|e| CliError::internal("online.write", format!("{}: {e}", p.display())))
    };
    if text != before {
        write(&ini_file, &text)?;
    }
    if let Some(t) = new_uproject {
        write(&up, &t)?;
    }
    let mut d = data;
    d["missing"] = json!(missing(
        s,
        &text,
        &std::fs::read_to_string(&up).unwrap_or_default()
    ));
    if !d["missing"].as_array().is_some_and(|m| m.is_empty()) {
        d["next"] =
            json!("set the missing keys with --set KEY=VALUE, then: unrealengine-cli online check");
    }
    if d["uprojectChanged"] == true {
        d["restart"] = json!(
            "plugins changed: restart the editor (unrealengine-cli editor close, then editor open)"
        );
    }
    Ok(d)
}

fn plugin_enabled(uproject: &str, name: &str, by_default: bool) -> bool {
    let v: Value = serde_json::from_str(uproject).unwrap_or_default();
    match v["Plugins"]
        .as_array()
        .and_then(|l| l.iter().find(|p| p["Name"] == name))
    {
        Some(p) => p["Enabled"] == true,
        None => by_default,
    }
}

fn missing(s: Subsystem, ini_text: &str, uproject: &str) -> Vec<String> {
    let spec = spec(s);
    let values = current_values(s, ini_text);
    let mut out: Vec<String> = spec
        .required
        .iter()
        .filter(|k| {
            values
                .get(**k)
                .and_then(Value::as_str)
                .is_none_or(str::is_empty)
        })
        .map(|k| k.to_string())
        .collect();
    for p in spec.plugins {
        if !plugin_enabled(uproject, p, spec.default_plugins.contains(p)) {
            out.push(format!("plugin:{p}"));
        }
    }
    out
}

fn check(ctx: &mut Ctx) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    let text = std::fs::read_to_string(ini_path(&up)).unwrap_or_default();
    let uproject = std::fs::read_to_string(&up).unwrap_or_default();
    let service = ini::get(&text, SERVICE_SECTION, "DefaultPlatformService").unwrap_or_default();
    let s = match service.to_ascii_lowercase().as_str() {
        "eos" => Subsystem::Eos,
        "steam" => Subsystem::Steam,
        "null" | "" => Subsystem::Null,
        other => {
            return Ok(json!({ "subsystem": other, "supported": false, "missing": [] }));
        }
    };
    let spec = spec(s);
    let values = current_values(s, &text);
    let settings: Map<String, Value> = spec
        .keys
        .iter()
        .map(|k| {
            let v = values.get(*k).and_then(Value::as_str).map(|v| mask(k, v));
            ((*k).to_string(), v.map_or(Value::Null, Value::from))
        })
        .collect();
    let plugins: Vec<Value> = spec
        .plugins
        .iter()
        .map(|p| json!({ "name": p, "enabled": plugin_enabled(&uproject, p, spec.default_plugins.contains(p)) }))
        .collect();
    let missing = missing(s, &text, &uproject);
    let data = json!({
        "subsystem": name_of(s),
        "configured": !service.is_empty(),
        "plugins": plugins,
        "settings": settings,
        "missing": missing,
    });
    if missing.is_empty() {
        Ok(data)
    } else {
        Err(CliError::editor(
            "online.missing_settings",
            format!("{} is missing: {}", name_of(s), missing.join(", ")),
        )
        .with_hint(format!(
            "unrealengine-cli online configure --subsystem {} --set KEY=VALUE",
            name_of(s)
        ))
        .with_details(data))
    }
}

use std::process::Command;

use clap::{Subcommand, ValueEnum};
use serde_json::{Map, Value, json};

use super::Ctx;
use crate::core::error::CliError;
use crate::engine::{build, editor, locate};

#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Preset {
    Outdoor,
    Night,
    Indoor,
}

#[derive(Debug, Subcommand)]
pub enum LightCmd {
    /// Light the open level: outdoor, night or indoor
    Preset {
        #[arg(value_enum)]
        preset: Preset,
        /// Keep the level's existing sun, sky, fog and post process actors
        #[arg(long)]
        keep: bool,
    },
    /// Build static lighting for the open level (waits for the job)
    Build {
        #[arg(long, value_parser = ["preview", "medium", "high", "production"], default_value = "preview")]
        quality: String,
        #[arg(long, default_value_t = 1800)]
        timeout: u64,
    },
    /// Build World Partition HLODs of a map in a separate editor process (the editor must be closed)
    Hlod {
        /// Map, e.g. /Game/Maps/OpenWorld
        map: String,
        #[arg(long, default_value_t = 3600)]
        timeout: u64,
    },
}

#[derive(Debug, Subcommand)]
pub enum RenderCmd {
    /// Current values of lumen, lumenReflections, virtualShadowMaps, nanite, rayTracing, staticLighting, antiAliasing
    Get,
    /// Change a setting: render set lumen false, render set antiAliasing TSR, render set <URendererSettings property> <value>
    Set { setting: String, value: String },
}

pub fn run(ctx: &mut Ctx, cmd: LightCmd) -> Result<Value, CliError> {
    match cmd {
        LightCmd::Preset { preset, keep } => {
            let name = match preset {
                Preset::Outdoor => "outdoor",
                Preset::Night => "night",
                Preset::Indoor => "indoor",
            };
            ctx.call("light_preset", json!({ "preset": name, "replace": !keep }))
        }
        LightCmd::Build { quality, timeout } => ctx.run_job(
            "light_build",
            json!({ "quality": quality }),
            timeout,
            "lighting",
        ),
        LightCmd::Hlod { map, timeout } => hlod(ctx, &map, timeout),
    }
}

pub fn render(ctx: &mut Ctx, cmd: RenderCmd) -> Result<Value, CliError> {
    let args = match cmd {
        RenderCmd::Get => json!({}),
        RenderCmd::Set { setting, value } => {
            let v = match value.to_ascii_lowercase().as_str() {
                "true" | "on" => json!(true),
                "false" | "off" => json!(false),
                _ => json!(value),
            };
            let mut m = Map::new();
            m.insert(setting, v);
            json!({ "values": m })
        }
    };
    ctx.call("render_settings", args)
}

fn hlod(ctx: &mut Ctx, map: &str, timeout: u64) -> Result<Value, CliError> {
    if !map.starts_with('/') {
        return Err(CliError::usage(
            "args.invalid",
            format!("'{map}' is not a map path like /Game/Maps/OpenWorld"),
        ));
    }
    let up = ctx.uproject()?;
    if let Some(i) = editor::running_for(&up) {
        return Err(CliError::editor(
            "lighting.editor_running",
            format!("the editor (pid {}) has this project open; the HLOD builder saves the map's packages", i.pid),
        )
        .with_hint("save and close it first: unrealengine-cli editor close"));
    }
    let engine = locate::resolve(&up)?;
    let exe = engine.editor_cmd_exe();
    let args = vec![
        up.to_string_lossy().to_string(),
        map.to_string(),
        "-run=WorldPartitionBuilderCommandlet".into(),
        "-Builder=WorldPartitionHLODsBuilder".into(),
        "-AllowCommandletRendering".into(),
        "-SCCProvider=None".into(),
        "-unattended".into(),
        "-nosplash".into(),
    ];
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "executable": exe, "args": args }));
    }
    if !exe.is_file() {
        return Err(CliError::usage(
            "engine.not_found",
            format!("{} not found", exe.display()),
        ));
    }
    let started = std::time::Instant::now();
    let mut child = Command::new(&exe)
        .args(&args)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| {
            CliError::internal(
                "lighting.spawn",
                format!("cannot run {}: {e}", exe.display()),
            )
        })?;
    let out = std::thread::spawn({
        let mut so = child.stdout.take();
        move || {
            let mut s = String::new();
            if let Some(o) = so.as_mut() {
                let _ = std::io::Read::read_to_string(o, &mut s);
            }
            s
        }
    });
    loop {
        if let Some(status) = child
            .try_wait()
            .map_err(|e| CliError::internal("lighting.spawn", e.to_string()))?
        {
            let text = out.join().unwrap_or_default();
            let diagnostics = build::parse_diagnostics(&text);
            let tail: Vec<&str> = {
                let l: Vec<&str> = text.lines().collect();
                l[l.len().saturating_sub(20)..].to_vec()
            };
            let data = json!({
                "map": map,
                "succeeded": status.success(),
                "seconds": started.elapsed().as_secs(),
                "errors": diagnostics.iter().filter(|d| d.severity == "error").count(),
                "logTail": tail,
            });
            return if status.success() {
                Ok(data)
            } else {
                Err(CliError::editor(
                    "lighting.hlod_failed",
                    format!("the HLOD builder exited with {status}"),
                )
                .with_hint("is it a World Partition map? see details.logTail")
                .with_details(data))
            };
        }
        if started.elapsed().as_secs() > timeout {
            let _ = child.kill();
            return Err(CliError::timeout(
                "lighting.timeout",
                format!("the HLOD build did not finish within {timeout}s"),
            ));
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }
}

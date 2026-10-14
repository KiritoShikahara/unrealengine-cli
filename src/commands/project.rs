use std::path::PathBuf;
use std::time::Duration;

use serde_json::{Value, json};

use super::Ctx;
use crate::catalog;
use crate::core::error::CliError;
use crate::core::paths;
use crate::engine::{build, editor, locate};
use crate::{doctor, instances, setup};

impl Ctx {
    pub fn uproject(&self) -> Result<PathBuf, CliError> {
        paths::find_uproject(self.global.project.as_deref(), &self.cwd)
    }
}

pub fn setup_cmd(
    ctx: &mut Ctx,
    build_after: bool,
    add_code_module: bool,
) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    if !ctx.global.dry_run {
        let plan = setup::setup(&up, true, add_code_module)?;
        for c in plan["changes"].as_array().into_iter().flatten() {
            eprintln!(
                "{} {}",
                c["action"].as_str().unwrap_or_default(),
                c["path"].as_str().unwrap_or_default()
            );
        }
    }
    let mut data = setup::setup(&up, ctx.global.dry_run, add_code_module)?;
    if build_after && !ctx.global.dry_run {
        data["build"] = build_cmd(ctx)?;
    }
    Ok(data)
}

pub fn doctor_cmd(ctx: &Ctx) -> Result<Value, CliError> {
    Ok(doctor::run(ctx.uproject().ok().as_deref()))
}

pub fn build_cmd(ctx: &mut Ctx) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    if let Some(i) = editor::running_for(&up) {
        return Err(CliError::usage(
            "build.editor_running",
            format!("the editor (pid {}) has this project open", i.pid),
        )
        .with_hint("close it first: unreal-cli editor close (or use: unreal-cli code apply)"));
    }
    let engine = locate::resolve(&up)?;
    if ctx.global.dry_run {
        return Ok(
            json!({ "dryRun": true, "target": build::editor_target(&up), "engine": engine }),
        );
    }
    build::build_editor(&engine, &up)
}

pub fn open_cmd(
    ctx: &mut Ctx,
    headless: bool,
    null_rhi: bool,
    wait_secs: u64,
) -> Result<Value, CliError> {
    let mode = if null_rhi {
        editor::WindowMode::NullRhi
    } else if headless {
        editor::WindowMode::Headless
    } else {
        editor::WindowMode::Normal
    };
    let up = ctx.uproject()?;
    let engine = locate::resolve(&up)?;
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "editor": engine.editor_exe(), "project": up }));
    }
    editor::open(&engine, &up, mode, Duration::from_secs(wait_secs))
}

pub fn close_cmd(ctx: &mut Ctx, force: bool) -> Result<Value, CliError> {
    let inst = instances::select(ctx.global.instance, ctx.global.project.as_deref(), &ctx.cwd)?;
    ctx.instance = Some(inst.clone());
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "pid": inst.pid }));
    }
    editor::close(&inst, force, ctx.timeout())
}

pub fn status_cmd(ctx: &Ctx) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    let engine = locate::resolve(&up).ok();
    let running = editor::running_for(&up);
    Ok(json!({
        "project": up,
        "engine": engine.as_ref().map(|e| json!({ "id": e.id, "root": e.root, "version": e.version() })),
        "running": running.is_some(),
        "pid": running.as_ref().map(|i| i.pid),
        "pluginInstalled": up.parent().unwrap().join("Plugins").join(setup::PLUGIN_NAME).is_dir(),
    }))
}

/// Tool catalog from the live editor when reachable, else the bundled snapshot.
fn tools(ctx: &mut Ctx) -> (Vec<Value>, &'static str) {
    let quiet = Duration::from_millis(ctx.global.timeout_ms.min(5000));
    let live = instances::select(ctx.global.instance, ctx.global.project.as_deref(), &ctx.cwd)
        .ok()
        .and_then(|i| {
            crate::transport::client::BridgeClient::connect(i.addr(), &i.token, quiet).ok()
        })
        .and_then(|mut c| c.call("tools_list", json!({}), false, 10_000).ok())
        .and_then(|v| v.as_array().cloned());
    match live {
        Some(t) => (t, "editor"),
        None => (catalog::bundled(), "bundled"),
    }
}

pub fn tool_list(ctx: &mut Ctx, group: Option<&str>) -> Result<Value, CliError> {
    let (all, source) = tools(ctx);
    let items: Vec<Value> = all
        .iter()
        .filter(|t| group.is_none_or(|g| t["group"] == g))
        .map(catalog::summary)
        .collect();
    Ok(json!({ "source": source, "count": items.len(), "tools": items }))
}

pub fn tool_find(ctx: &mut Ctx, query: &str, limit: usize) -> Result<Value, CliError> {
    let (all, source) = tools(ctx);
    let hits: Vec<Value> = catalog::find(&all, query, limit)
        .iter()
        .map(catalog::summary)
        .collect();
    Ok(json!({ "source": source, "tools": hits }))
}

pub fn tool_describe(ctx: &mut Ctx, name: &str) -> Result<Value, CliError> {
    let (all, _) = tools(ctx);
    all.into_iter().find(|t| t["name"] == name).ok_or_else(|| {
        let close: Vec<Value> = catalog::find(&catalog::bundled(), &name.replace('_', " "), 5)
            .iter()
            .map(|t| t["name"].clone())
            .collect();
        CliError::usage("tool.unknown", format!("unknown tool '{name}'"))
            .with_hint("search: unreal-cli tool find <words>")
            .with_details(json!({ "suggestions": close }))
    })
}

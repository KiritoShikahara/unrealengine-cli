use serde_json::{Value, json};

use super::{Ctx, parse_args_json};
use crate::core::error::CliError;
use crate::instances;

pub fn ping(ctx: &mut Ctx) -> Result<Value, CliError> {
    let started = std::time::Instant::now();
    let mut data = ctx.call("system_ping", json!({}))?;
    data["rttMs"] = json!(started.elapsed().as_millis() as u64);
    Ok(data)
}

pub fn logs(
    ctx: &mut Ctx,
    since: Option<i64>,
    limit: Option<u32>,
    severity: Option<String>,
    category: Option<String>,
) -> Result<Value, CliError> {
    let mut args = json!({});
    if let Some(v) = since {
        args["since"] = json!(v);
    }
    if let Some(v) = limit {
        args["limit"] = json!(v);
    }
    if let Some(v) = severity {
        args["severity"] = json!(v);
    }
    if let Some(v) = category {
        args["category"] = json!(v);
    }
    ctx.call("editor_logs", args)
}

/// Polls the editor log until a line newer than "now" contains `text`.
pub fn wait_for_log(
    ctx: &mut Ctx,
    text: &str,
    severity: Option<String>,
    category: Option<String>,
    wait_seconds: u64,
) -> Result<Value, CliError> {
    let mut client = ctx.connect()?;
    let timeout = ctx.global.timeout_ms;
    let start = client.call("editor_logs", json!({ "limit": 1 }), false, timeout)?;
    let mut since = start["nextSince"].as_i64().unwrap_or(0);
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(wait_seconds);
    loop {
        let mut args = json!({ "since": since, "limit": 1000 });
        if let Some(s) = &severity {
            args["severity"] = json!(s);
        }
        if let Some(c) = &category {
            args["category"] = json!(c);
        }
        let page = client.call("editor_logs", args, false, timeout)?;
        if let Some(line) = page["lines"]
            .as_array()
            .into_iter()
            .flatten()
            .find(|l| l["text"].as_str().is_some_and(|t| t.contains(text)))
        {
            return Ok(json!({ "matched": line, "nextSince": page["nextSince"] }));
        }
        since = page["nextSince"].as_i64().unwrap_or(since);
        if std::time::Instant::now() >= deadline {
            return Err(CliError::timeout(
                "logs.wait_timeout",
                format!("no log line contained '{text}' within {wait_seconds}s"),
            )
            .with_details(json!({ "nextSince": since })));
        }
        std::thread::sleep(std::time::Duration::from_millis(300));
    }
}

pub fn instances_list() -> Result<Value, CliError> {
    Ok(serde_json::to_value(instances::list()).expect("instances serialize"))
}

pub fn instances_use(pid: u32) -> Result<Value, CliError> {
    if !instances::list().iter().any(|i| i.pid == pid) {
        return Err(CliError::usage(
            "instances.not_found",
            format!("no running editor with pid {pid}"),
        )
        .with_hint("list editors: unreal-cli instances list"));
    }
    instances::set_default(pid)?;
    Ok(json!({ "default": pid }))
}

pub fn tool_call(ctx: &mut Ctx, name: &str, args: Option<&str>) -> Result<Value, CliError> {
    let args = parse_args_json(args)?;
    ctx.call(name, args)
}

pub fn python_exec(ctx: &mut Ctx, code: &str) -> Result<Value, CliError> {
    let code = match code.strip_prefix('@') {
        Some(path) => std::fs::read_to_string(path)
            .map_err(|e| CliError::usage("args.file", format!("cannot read {path}: {e}")))?,
        None => code.to_string(),
    };
    ctx.call("python_exec", json!({ "code": code }))
}

pub fn jobs(
    ctx: &mut Ctx,
    action: &str,
    job_id: Option<&str>,
    since: Option<u32>,
) -> Result<Value, CliError> {
    let mut args = json!({});
    if let Some(id) = job_id {
        args["jobId"] = json!(id);
    }
    if let Some(s) = since {
        args["since"] = json!(s);
    }
    ctx.call(&format!("jobs_{action}"), args)
}

/// Runs `[{"tool": .., "args": {..}}, ...]` in order on one connection; stops at the first failure.
pub fn batch(ctx: &mut Ctx, file: &str) -> Result<Value, CliError> {
    let text = if file == "-" {
        std::io::read_to_string(std::io::stdin())
            .map_err(|e| CliError::usage("args.file", e.to_string()))?
    } else {
        std::fs::read_to_string(file)
            .map_err(|e| CliError::usage("args.file", format!("cannot read {file}: {e}")))?
    };
    let steps: Vec<Value> = serde_json::from_str(&text).map_err(|e| {
        CliError::usage(
            "args.invalid_json",
            format!("batch file must be a JSON array: {e}"),
        )
    })?;
    let mut client = ctx.connect()?;
    let mut results = Vec::new();
    for (i, step) in steps.iter().enumerate() {
        let tool = step["tool"]
            .as_str()
            .ok_or_else(|| CliError::usage("args.invalid", format!("step {i} has no \"tool\"")))?;
        let args = step.get("args").cloned().unwrap_or(json!({}));
        match client.call(tool, args, ctx.global.dry_run, ctx.global.timeout_ms) {
            Ok(data) => results.push(json!({ "tool": tool, "ok": true, "data": data })),
            Err(e) => {
                return Err(e.with_details(json!({ "failedStep": i, "completed": results })));
            }
        }
    }
    Ok(json!({ "steps": results }))
}

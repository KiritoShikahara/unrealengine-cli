//! `mcp serve`: a stdio MCP server (JSON-RPC 2.0, one message per line) exposing tool search and calls.

use std::io::{BufRead, Write};
use std::path::Path;

use serde_json::{Value, json};

use crate::catalog;
use crate::cli::GlobalOpts;
use crate::commands::Ctx;
use crate::core::error::CliError;

fn tool_defs() -> Value {
    json!([
        { "name": "unreal_ping", "description": "Check that Unreal Editor with UnrealCliBridge answers; returns engine and project.",
          "inputSchema": { "type": "object", "properties": {} } },
        { "name": "unreal_find_tools", "description": "Search the 100+ Unreal Editor tools by words (e.g. 'blueprint connect', 'landscape'). Returns names and summaries.",
          "inputSchema": { "type": "object", "properties": { "query": { "type": "string" } }, "required": ["query"] } },
        { "name": "unreal_describe_tool", "description": "Full description and input schema of one Unreal tool.",
          "inputSchema": { "type": "object", "properties": { "name": { "type": "string" } }, "required": ["name"] } },
        { "name": "unreal_call", "description": "Call an Unreal tool by name with its arguments object (see unreal_describe_tool).",
          "inputSchema": { "type": "object", "properties": { "tool": { "type": "string" }, "args": { "type": "object" }, "dryRun": { "type": "boolean" } }, "required": ["tool"] } }
    ])
}

fn run_tool(global: &GlobalOpts, name: &str, args: &Value) -> Result<Value, CliError> {
    let mut ctx = Ctx::new(global.clone());
    match name {
        "unreal_ping" => ctx.call("system_ping", json!({})),
        "unreal_find_tools" => {
            let q = args["query"].as_str().unwrap_or_default();
            crate::commands::project::tool_find(&mut ctx, q, 20)
        }
        "unreal_describe_tool" => crate::commands::project::tool_describe(
            &mut ctx,
            args["name"].as_str().unwrap_or_default(),
        ),
        "unreal_call" => {
            ctx.global.dry_run = args["dryRun"].as_bool().unwrap_or(false);
            let tool = args["tool"]
                .as_str()
                .ok_or_else(|| CliError::usage("args.invalid", "'tool' is required"))?;
            ctx.call(tool, args.get("args").cloned().unwrap_or(json!({})))
        }
        other => Err(CliError::usage(
            "tool.unknown",
            format!("unknown MCP tool '{other}'"),
        )),
    }
}

/// Handles one JSON-RPC message; returns the response (None for notifications).
pub fn handle(global: &GlobalOpts, msg: &Value) -> Option<Value> {
    let id = msg.get("id").cloned();
    let method = msg["method"].as_str().unwrap_or_default();
    let result = match method {
        "initialize" => Ok(json!({
            "protocolVersion": msg["params"]["protocolVersion"].as_str().unwrap_or("2025-06-18"),
            "capabilities": { "tools": {} },
            "serverInfo": { "name": "unrealengine-cli", "version": env!("CARGO_PKG_VERSION") }
        })),
        "ping" => Ok(json!({})),
        "tools/list" => Ok(json!({ "tools": tool_defs() })),
        "tools/call" => {
            let name = msg["params"]["name"].as_str().unwrap_or_default();
            let args = msg["params"].get("arguments").cloned().unwrap_or(json!({}));
            let (is_error, body) = match run_tool(global, name, &args) {
                Ok(v) => (false, v),
                Err(e) => (true, serde_json::to_value(&e).unwrap_or_default()),
            };
            Ok(
                json!({ "content": [{ "type": "text", "text": body.to_string() }], "isError": is_error }),
            )
        }
        _ if id.is_none() => return None,
        other => Err(json!({ "code": -32601, "message": format!("method not found: {other}") })),
    };
    let id = id?;
    Some(match result {
        Ok(r) => json!({ "jsonrpc": "2.0", "id": id, "result": r }),
        Err(e) => json!({ "jsonrpc": "2.0", "id": id, "error": e }),
    })
}

pub fn serve(global: &GlobalOpts) -> Result<Value, CliError> {
    let stdin = std::io::stdin();
    let mut stdout = std::io::stdout();
    for line in stdin.lock().lines() {
        let Ok(line) = line else { break };
        if line.trim().is_empty() {
            continue;
        }
        let reply = match serde_json::from_str::<Value>(&line) {
            Ok(msg) => handle(global, &msg),
            Err(e) => Some(
                json!({ "jsonrpc": "2.0", "id": null, "error": { "code": -32700, "message": e.to_string() } }),
            ),
        };
        if let Some(r) = reply {
            let _ = writeln!(stdout, "{r}");
            let _ = stdout.flush();
        }
    }
    std::process::exit(0);
}

/// Writes the MCP server entry into a client's config file (merging with existing content).
pub fn configure(client: &str, cwd: &Path, dry_run: bool) -> Result<Value, CliError> {
    let entry = json!({ "command": "unrealengine-cli", "args": ["mcp", "serve"] });
    let (path, key) = match client {
        "claude-code" => (cwd.join(".mcp.json"), "mcpServers"),
        "cursor" => (cwd.join(".cursor/mcp.json"), "mcpServers"),
        "vscode" => (cwd.join(".vscode/mcp.json"), "servers"),
        "codex" => {
            let path = crate::core::paths::home().join(".codex/config.toml");
            let text = std::fs::read_to_string(&path).unwrap_or_default();
            if text.contains("[mcp_servers.unrealengine-cli]") {
                return Ok(json!({ "client": client, "file": path, "changed": false }));
            }
            if !dry_run {
                let add = "\n[mcp_servers.unrealengine-cli]\ncommand = \"unrealengine-cli\"\nargs = [\"mcp\", \"serve\"]\n";
                std::fs::create_dir_all(path.parent().unwrap())
                    .and_then(|_| std::fs::write(&path, text + add))
                    .map_err(|e| CliError::internal("io.write", e.to_string()))?;
            }
            return Ok(
                json!({ "client": client, "file": path, "changed": true, "dryRun": dry_run }),
            );
        }
        other => {
            return Err(
                CliError::usage("mcp.unknown_client", format!("unknown client '{other}'"))
                    .with_hint("clients: claude-code, cursor, vscode, codex"),
            );
        }
    };
    let mut v: Value = match std::fs::read_to_string(&path) {
        Ok(t) => serde_json::from_str(&t)
            .map_err(|e| CliError::usage("mcp.bad_config", format!("{}: {e}", path.display())))?,
        Err(_) => json!({}),
    };
    let servers = v
        .as_object_mut()
        .ok_or_else(|| CliError::usage("mcp.bad_config", "config must be a JSON object"))?
        .entry(key)
        .or_insert_with(|| json!({}));
    let changed = servers.get("unrealengine-cli") != Some(&entry);
    servers["unrealengine-cli"] = entry;
    if changed && !dry_run {
        std::fs::create_dir_all(path.parent().unwrap())
            .and_then(|_| std::fs::write(&path, serde_json::to_string_pretty(&v).unwrap() + "\n"))
            .map_err(|e| CliError::internal("io.write", e.to_string()))?;
    }
    let _ = catalog::BUNDLED;
    Ok(json!({ "client": client, "file": path, "changed": changed, "dryRun": dry_run }))
}

pub mod basic;
pub mod blueprint;
pub mod code;
pub mod code_net;
pub mod project;
pub mod server;
pub mod test_runner;
pub mod typed;

use std::path::PathBuf;
use std::time::Duration;

use serde_json::Value;

use crate::cli::GlobalOpts;
use crate::core::error::CliError;
use crate::instances::{self, Instance};
use crate::transport::client::BridgeClient;

/// Per-invocation context shared by all commands.
pub struct Ctx {
    pub global: GlobalOpts,
    pub cwd: PathBuf,
    pub instance: Option<Instance>,
    /// Added to the envelope's warnings (e.g. an outdated plugin in the running editor).
    pub warnings: Vec<String>,
}

/// Warning when the running editor's plugin is not the version this CLI bundles.
pub fn plugin_mismatch_warning(running: &str, bundled: &str) -> Option<String> {
    if running.is_empty() || running == bundled {
        return None;
    }
    Some(format!(
        "the editor runs UnrealCliBridge {running} but this CLI bundles {bundled}; newer tools may be missing. Update: unrealengine-cli setup --build (close the editor first), then reopen it"
    ))
}

impl Ctx {
    pub fn new(global: GlobalOpts) -> Self {
        Self {
            global,
            cwd: std::env::current_dir().unwrap_or_default(),
            instance: None,
            warnings: Vec::new(),
        }
    }

    pub fn timeout(&self) -> Duration {
        Duration::from_millis(self.global.timeout_ms)
    }

    /// Selects an editor instance and opens an authenticated connection.
    pub fn connect(&mut self) -> Result<BridgeClient, CliError> {
        let inst = instances::select(
            self.global.instance,
            self.global.project.as_deref(),
            &self.cwd,
        )?;
        let client = BridgeClient::connect(inst.addr(), &inst.token, self.timeout())?;
        if let Some(w) = plugin_mismatch_warning(
            &client.welcome.plugin_version,
            &crate::setup::bundled_plugin_version(),
        ) && !self.warnings.contains(&w)
        {
            self.warnings.push(w);
        }
        self.instance = Some(inst);
        Ok(client)
    }

    /// Connects and calls one tool, honouring --dry-run and --timeout-ms.
    pub fn call(&mut self, tool: &str, args: Value) -> Result<Value, CliError> {
        let mut client = self.connect()?;
        client.call(tool, args, self.global.dry_run, self.global.timeout_ms)
    }

    /// Like [`Ctx::call`] with an explicit timeout (for tools that block, e.g. PIE).
    pub fn call_with_timeout(
        &mut self,
        tool: &str,
        args: Value,
        timeout_ms: u64,
    ) -> Result<Value, CliError> {
        self.global.timeout_ms = self.global.timeout_ms.max(timeout_ms);
        self.call(tool, args)
    }
}

/// Parses `--args` as inline JSON or `@file`.
pub fn parse_args_json(raw: Option<&str>) -> Result<Value, CliError> {
    let Some(raw) = raw else {
        return Ok(Value::Object(Default::default()));
    };
    let text = match raw.strip_prefix('@') {
        Some(path) => std::fs::read_to_string(path)
            .map_err(|e| CliError::usage("args.file", format!("cannot read {path}: {e}")))?,
        None => raw.to_string(),
    };
    let v: Value = serde_json::from_str(&text).map_err(|e| {
        CliError::usage(
            "args.invalid_json",
            format!("--args is not valid JSON: {e}"),
        )
    })?;
    if !v.is_object() {
        return Err(CliError::usage(
            "args.invalid_json",
            "--args must be a JSON object",
        ));
    }
    Ok(v)
}

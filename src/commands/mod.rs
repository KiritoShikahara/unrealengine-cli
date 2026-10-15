pub mod anim;
pub mod basic;
pub mod blueprint;
pub mod code;
pub mod code_net;
pub mod fx;
pub mod gas;
pub mod light;
pub mod loc;
pub mod online;
pub mod perf;
pub mod project;
pub mod server;
pub mod test_runner;
pub mod typed;
pub mod visual;

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

    /// Starts a long-running tool and waits for its job; errors keep the job's code (or `<prefix>.timeout`).
    pub fn run_job(
        &mut self,
        tool: &str,
        args: Value,
        timeout_secs: u64,
        prefix: &str,
    ) -> Result<Value, CliError> {
        let started = self.call(tool, args)?;
        if self.global.dry_run {
            return Ok(started);
        }
        let job = started["jobId"].as_str().unwrap_or_default().to_string();
        let begin = std::time::Instant::now();
        loop {
            let j = self.call("jobs_get", serde_json::json!({ "jobId": job }))?;
            match j["state"].as_str().unwrap_or_default() {
                "succeeded" => return Ok(j["result"].clone()),
                "failed" => {
                    let e = &j["error"];
                    let mut err = CliError::editor(
                        e["code"].as_str().unwrap_or("job.failed"),
                        e["message"].as_str().unwrap_or("the job failed"),
                    )
                    .with_details(e["details"].clone());
                    if let Some(h) = e["hint"].as_str() {
                        err = err.with_hint(h);
                    }
                    return Err(err);
                }
                "timeout" | "cancelled" => {
                    return Err(CliError::timeout(
                        format!("{prefix}.timeout"),
                        format!("job {job} ended: {}", j["state"]),
                    ));
                }
                _ if begin.elapsed().as_secs() > timeout_secs => {
                    let _ = self.call("jobs_cancel", serde_json::json!({ "jobId": job }));
                    return Err(CliError::timeout(
                        format!("{prefix}.timeout"),
                        format!("{tool} did not finish within {timeout_secs}s"),
                    ));
                }
                _ => std::thread::sleep(std::time::Duration::from_millis(500)),
            }
        }
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

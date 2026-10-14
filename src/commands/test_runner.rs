//! `unreal-cli test`: Automation tests in the running editor (job) or a headless editor process.

use std::path::Path;
use std::process::Command;
use std::time::{Duration, Instant};

use clap::Subcommand;
use serde_json::{Value, json};

use super::Ctx;
use crate::core::error::CliError;
use crate::engine::{editor, locate};

#[derive(Debug, Subcommand)]
pub enum TestCmd {
    /// List tests whose path contains the filter (needs a running editor)
    List { filter: Option<String> },
    /// Run tests: in the running editor, or a headless editor when none is running (or --standalone)
    Run {
        filter: String,
        #[arg(long)]
        standalone: bool,
        /// Seconds before giving up
        #[arg(long, default_value_t = 1800)]
        timeout: u64,
    },
}

pub fn run(ctx: &mut Ctx, cmd: TestCmd) -> Result<Value, CliError> {
    match cmd {
        TestCmd::List { filter } => ctx.call(
            "automation_list",
            json!({ "filter": filter.unwrap_or_default() }),
        ),
        TestCmd::Run {
            filter,
            standalone,
            timeout,
        } => {
            let up = ctx.uproject()?;
            if standalone || editor::running_for(&up).is_none() {
                standalone_run(&up, &filter, Duration::from_secs(timeout))
            } else {
                in_editor(ctx, &filter, Duration::from_secs(timeout))
            }
        }
    }
}

fn in_editor(ctx: &mut Ctx, filter: &str, timeout: Duration) -> Result<Value, CliError> {
    ctx.global.timeout_ms = ctx.global.timeout_ms.max(timeout.as_millis() as u64);
    let started = ctx.call("automation_run", json!({ "filter": filter }))?;
    let job = started["jobId"].as_str().unwrap_or_default().to_string();
    let begin = Instant::now();
    loop {
        std::thread::sleep(Duration::from_millis(500));
        let j = ctx.call("jobs_get", json!({ "jobId": job }))?;
        match j["state"].as_str().unwrap_or_default() {
            "succeeded" => return Ok(j["result"].clone()),
            "failed" => {
                let e = &j["error"];
                return Err(CliError::editor(
                    e["code"].as_str().unwrap_or("automation.failed"),
                    e["message"].as_str().unwrap_or("tests failed"),
                )
                .with_details(e["details"].clone()));
            }
            "timeout" | "cancelled" => {
                return Err(CliError::timeout(
                    "automation.timeout",
                    format!("job {job} ended: {}", j["state"]),
                ));
            }
            _ if begin.elapsed() > timeout => {
                let _ = ctx.call("jobs_cancel", json!({ "jobId": job }));
                return Err(CliError::timeout(
                    "automation.timeout",
                    "tests did not finish in time",
                ));
            }
            _ => {}
        }
    }
}

/// Summarises an Automation report (index.json).
pub fn summarize_report(text: &str) -> Result<Value, CliError> {
    let v: Value = serde_json::from_str(text.trim_start_matches('\u{feff}'))
        .map_err(|e| CliError::internal("automation.bad_report", e.to_string()))?;
    let tests: Vec<Value> = v["tests"]
        .as_array()
        .into_iter()
        .flatten()
        .map(|t| {
            let errors: Vec<Value> = t["entries"]
                .as_array()
                .into_iter()
                .flatten()
                .filter(|e| e["event"]["type"] == "Error")
                .map(|e| e["event"]["message"].clone())
                .collect();
            json!({ "test": t["fullTestPath"], "passed": t["state"] == "Success", "errors": errors })
        })
        .collect();
    let failed = tests.iter().filter(|t| t["passed"] == false).count();
    Ok(json!({ "total": tests.len(), "failed": failed, "tests": tests }))
}

fn standalone_run(up: &Path, filter: &str, timeout: Duration) -> Result<Value, CliError> {
    let engine = locate::resolve(up)?;
    let report = std::env::temp_dir().join(format!("unreal-cli-automation-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&report);
    std::fs::create_dir_all(&report).map_err(|e| CliError::internal("io.write", e.to_string()))?;
    let mut child = Command::new(engine.editor_cmd_exe())
        .arg(up)
        .arg(format!("-ExecCmds=Automation RunTests {filter};Quit"))
        .args([
            "-unattended",
            "-nullrhi",
            "-nosplash",
            "-nosound",
            "-NoUnrealCliBridge",
        ])
        .arg(format!("-ReportExportPath={}", report.display()))
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| CliError::internal("automation.spawn", e.to_string()))?;
    let begin = Instant::now();
    loop {
        if child
            .try_wait()
            .map_err(|e| CliError::internal("automation.wait", e.to_string()))?
            .is_some()
        {
            break;
        }
        if begin.elapsed() > timeout {
            let _ = child.kill();
            return Err(CliError::timeout(
                "automation.timeout",
                "the headless test run did not finish in time",
            ));
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    let text = std::fs::read_to_string(report.join("index.json")).map_err(|_| {
        CliError::editor("automation.no_report", "the editor wrote no test report")
            .with_hint("check that the project builds: unreal-cli editor build")
    })?;
    let mut summary = summarize_report(&text)?;
    summary["mode"] = json!("standalone");
    if summary["total"] == 0 {
        return Err(CliError::editor(
            "automation.no_tests",
            format!("no test matches '{filter}'"),
        ));
    }
    if summary["failed"].as_u64().unwrap_or(0) > 0 {
        return Err(CliError::editor(
            "automation.failed",
            format!(
                "{} of {} test(s) failed",
                summary["failed"], summary["total"]
            ),
        )
        .with_details(summary));
    }
    Ok(summary)
}

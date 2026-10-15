use std::path::{Path, PathBuf};
use std::process::Command;

use clap::Subcommand;
use serde_json::{Value, json};

use super::Ctx;
use crate::core::error::CliError;
use crate::core::paths;
use crate::engine::locate;

#[derive(Debug, Subcommand)]
pub enum PerfCmd {
    /// Measure the running play session: frame / game / render / GPU ms (avg, p95, max), hitches, draw calls, memory
    Measure {
        #[arg(long, default_value_t = 10.0)]
        seconds: f64,
        /// Frames longer than this count as hitches
        #[arg(long)]
        hitch_ms: Option<f64>,
    },
    /// Unreal Insights traces
    Trace {
        #[command(subcommand)]
        action: TraceCmd,
    },
}

#[derive(Debug, Subcommand)]
pub enum TraceCmd {
    /// Start tracing the editor (default file Saved/UnrealCli/Traces/<time>.utrace)
    Start {
        #[arg(long)]
        file: Option<String>,
        /// Trace channels (default cpu,gpu,frame,bookmark)
        #[arg(long)]
        channels: Option<String>,
    },
    Stop,
    /// The most expensive timers of a .utrace (runs UnrealInsights headless)
    Summary {
        file: PathBuf,
        #[arg(long, default_value_t = 20)]
        top: usize,
        #[arg(long, default_value = "GameThread")]
        thread: String,
    },
}

pub fn run(ctx: &mut Ctx, cmd: PerfCmd) -> Result<Value, CliError> {
    match cmd {
        PerfCmd::Measure { seconds, hitch_ms } => {
            let mut a = json!({ "seconds": seconds });
            if let Some(h) = hitch_ms {
                a["hitchMs"] = json!(h);
            }
            ctx.call_with_timeout("perf_measure", a, ((seconds + 30.0) * 1000.0) as u64)
        }
        PerfCmd::Trace { action } => match action {
            TraceCmd::Start { file, channels } => {
                let mut a = json!({ "action": "start" });
                if let Some(f) = file {
                    a["file"] = json!(ctx.cwd.join(f));
                }
                if let Some(c) = channels {
                    a["channels"] = json!(c);
                }
                ctx.call("perf_trace", a)
            }
            TraceCmd::Stop => ctx.call("perf_trace", json!({ "action": "stop" })),
            TraceCmd::Summary { file, top, thread } => {
                summary(ctx, &ctx.cwd.join(file), top, &thread)
            }
        },
    }
}

fn insights_exe(root: &Path) -> PathBuf {
    let bin = root.join("Engine/Binaries");
    if cfg!(windows) {
        bin.join("Win64/UnrealInsights.exe")
    } else if cfg!(target_os = "macos") {
        bin.join("Mac/UnrealInsights.app/Contents/MacOS/UnrealInsights")
    } else {
        bin.join("Linux/UnrealInsights")
    }
}

fn num(s: &str) -> f64 {
    s.trim().parse().unwrap_or(0.0)
}

/// Timers of a `TimingInsights.ExportTimerStatistics` CSV (times in seconds), most inclusive time first.
pub fn timer_summary(csv: &str, top: usize) -> Vec<Value> {
    let mut lines = csv.lines();
    let header: Vec<&str> = lines.next().unwrap_or_default().split(',').collect();
    let col = |name: &str| header.iter().position(|h| h.trim() == name);
    let (Some(n), Some(c), Some(incl), Some(excl)) =
        (col("Name"), col("Count"), col("Incl"), col("Excl"))
    else {
        return Vec::new();
    };
    let (iavg, imax) = (col("I.Avg"), col("I.Max"));
    let ms = |x: f64| (x * 1_000_000.0).round() / 1000.0;
    let mut rows: Vec<(f64, Value)> = lines
        .filter_map(|l| {
            // Names may contain commas; the numeric columns are the last ones.
            let f: Vec<&str> = l.split(',').collect();
            let extra = f.len().checked_sub(header.len())?;
            let at = |i: usize| if i <= n { f[i] } else { f[i + extra] };
            let name = f[n..=n + extra].join(",");
            let total = num(at(incl));
            Some((
                total,
                json!({
                    "name": name,
                    "count": num(at(c)) as u64,
                    "inclusiveMs": ms(total),
                    "exclusiveMs": ms(num(at(excl))),
                    "avgMs": iavg.map(|i| ms(num(at(i)))),
                    "maxMs": imax.map(|i| ms(num(at(i)))),
                }),
            ))
        })
        .collect();
    rows.sort_by(|a, b| b.0.total_cmp(&a.0));
    rows.into_iter().take(top).map(|(_, v)| v).collect()
}

fn summary(ctx: &Ctx, file: &Path, top: usize, thread: &str) -> Result<Value, CliError> {
    if !file.is_file() {
        return Err(
            CliError::usage("perf.no_trace", format!("{} not found", file.display())).with_hint(
                "record one: unrealengine-cli perf trace start, play, then perf trace stop",
            ),
        );
    }
    let up = ctx.uproject()?;
    let engine = locate::resolve(&up)?;
    let exe = insights_exe(&engine.root);
    if !exe.is_file() {
        return Err(CliError::usage(
            "perf.no_insights",
            format!("{} not found", exe.display()),
        ));
    }
    let work = paths::data_dir().join("tmp");
    std::fs::create_dir_all(&work).map_err(|e| CliError::internal("io.write", e.to_string()))?;
    let stamp = std::process::id();
    let csv = work.join(format!("insights-{stamp}.csv"));
    let log = work.join(format!("insights-{stamp}.log"));
    let _ = std::fs::remove_file(&csv);
    let trace = paths::normalize(file);
    let cmd = format!(
        "TimingInsights.ExportTimerStatistics {} -threads={thread}",
        csv.display()
    );
    let mut run = Command::new(&exe);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        // Insights reads -Name="value with spaces"; Rust's own quoting would wrap the whole argument.
        let p = |s: &Path| paths::normalize(s).to_string_lossy().replace('/', "\\");
        run.raw_arg(format!("-OpenTraceFile=\"{}\"", p(&trace)))
            .raw_arg("-AutoQuit -NoUI -log")
            .raw_arg(format!("-ExecOnAnalysisCompleteCmd=\"{cmd}\""))
            .raw_arg(format!("-ABSLOG=\"{}\"", log.display()));
    }
    #[cfg(not(windows))]
    {
        run.arg(format!("-OpenTraceFile={}", trace.display()))
            .args(["-AutoQuit", "-NoUI", "-log"])
            .arg(format!("-ExecOnAnalysisCompleteCmd={cmd}"))
            .arg(format!("-ABSLOG={}", log.display()));
    }
    let status = run
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map_err(|e| {
            CliError::internal("perf.spawn", format!("cannot run {}: {e}", exe.display()))
        })?;
    let text = std::fs::read_to_string(&csv).map_err(|_| {
        let tail = std::fs::read_to_string(&log).unwrap_or_default();
        let tail: Vec<&str> = tail
            .lines()
            .rev()
            .take(15)
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
            .collect();
        CliError::editor(
            "perf.summary_failed",
            format!("UnrealInsights exited ({status}) without writing the timer statistics"),
        )
        .with_details(json!({ "logTail": tail }))
    })?;
    let _ = std::fs::remove_file(&csv);
    let timers = timer_summary(&text, top);
    Ok(json!({ "trace": trace, "thread": thread, "timers": timers }))
}

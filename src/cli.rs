use std::path::PathBuf;
use std::time::Instant;

use clap::{Args, Parser, Subcommand, ValueEnum};

use crate::commands::{Ctx, basic};
use crate::core::envelope::Envelope;
use crate::core::error::CliError;

#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum, Default)]
pub enum OutputFormat {
    #[default]
    Text,
    Json,
}

#[derive(Debug, Clone, Args)]
pub struct GlobalOpts {
    /// Output format
    #[arg(long, global = true, value_enum, default_value_t = OutputFormat::Text)]
    pub output: OutputFormat,
    /// Path to the .uproject (or its folder); default: found from the working directory
    #[arg(long, global = true)]
    pub project: Option<PathBuf>,
    /// Editor process id to connect to
    #[arg(long, global = true)]
    pub instance: Option<u32>,
    /// Request timeout in milliseconds
    #[arg(long = "timeout-ms", global = true, default_value_t = 30_000)]
    pub timeout_ms: u64,
    /// Show what would change without changing anything
    #[arg(long = "dry-run", global = true)]
    pub dry_run: bool,
    /// Verbose logging (repeatable)
    #[arg(short, long, global = true, action = clap::ArgAction::Count)]
    pub verbose: u8,
}

#[derive(Debug, Parser)]
#[command(
    name = "unreal-cli",
    version,
    about = "Drive Unreal Editor from AI agents"
)]
pub struct Cli {
    #[command(flatten)]
    pub global: GlobalOpts,
    #[command(subcommand)]
    pub command: Option<Command>,
}

#[derive(Debug, Subcommand)]
pub enum Command {
    /// Print CLI version information
    Version,
    /// Check that the editor answers
    Ping,
    /// Tail the editor log
    Logs {
        /// Only lines with sequence >= since (use a previous nextSince)
        #[arg(long)]
        since: Option<i64>,
        #[arg(long)]
        limit: Option<u32>,
        /// Minimum severity: error | warning | display | log | verbose
        #[arg(long)]
        severity: Option<String>,
        #[arg(long)]
        category: Option<String>,
    },
    /// Running editors with the bridge
    Instances {
        #[command(subcommand)]
        action: InstancesCmd,
    },
    /// Discover and call any tool
    Tool {
        #[command(subcommand)]
        action: ToolCmd,
    },
    /// Run Python inside the editor
    Python {
        #[command(subcommand)]
        action: PythonCmd,
    },
    /// Long-running editor jobs
    Jobs {
        #[command(subcommand)]
        action: JobsCmd,
    },
    /// Run a JSON array of tool calls in order (stops at the first failure)
    Batch {
        /// File path, or - for stdin
        file: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum InstancesCmd {
    /// List running editors
    List,
    /// Remember an editor for later commands
    Use { pid: u32 },
}

#[derive(Debug, Subcommand)]
pub enum ToolCmd {
    /// Call a tool with JSON arguments
    Call {
        name: String,
        /// JSON object or @file
        #[arg(long)]
        args: Option<String>,
    },
}

#[derive(Debug, Subcommand)]
pub enum PythonCmd {
    /// Execute code (or @file)
    Exec { code: String },
}

#[derive(Debug, Subcommand)]
pub enum JobsCmd {
    List,
    Get {
        job_id: String,
    },
    Logs {
        job_id: String,
        #[arg(long)]
        since: Option<u32>,
    },
    Cancel {
        job_id: String,
    },
}

fn dispatch(
    ctx: &mut Ctx,
    command: Command,
) -> (&'static str, Result<serde_json::Value, CliError>) {
    match command {
        Command::Version => (
            "version",
            Ok(serde_json::json!({ "version": env!("CARGO_PKG_VERSION") })),
        ),
        Command::Ping => ("ping", basic::ping(ctx)),
        Command::Logs {
            since,
            limit,
            severity,
            category,
        } => ("logs", basic::logs(ctx, since, limit, severity, category)),
        Command::Instances { action } => match action {
            InstancesCmd::List => ("instances list", basic::instances_list()),
            InstancesCmd::Use { pid } => ("instances use", basic::instances_use(pid)),
        },
        Command::Tool { action } => match action {
            ToolCmd::Call { name, args } => {
                ("tool call", basic::tool_call(ctx, &name, args.as_deref()))
            }
        },
        Command::Python { action } => match action {
            PythonCmd::Exec { code } => ("python exec", basic::python_exec(ctx, &code)),
        },
        Command::Jobs { action } => match action {
            JobsCmd::List => ("jobs list", basic::jobs(ctx, "list", None, None)),
            JobsCmd::Get { job_id } => ("jobs get", basic::jobs(ctx, "get", Some(&job_id), None)),
            JobsCmd::Logs { job_id, since } => {
                ("jobs logs", basic::jobs(ctx, "logs", Some(&job_id), since))
            }
            JobsCmd::Cancel { job_id } => (
                "jobs cancel",
                basic::jobs(ctx, "cancel", Some(&job_id), None),
            ),
        },
        Command::Batch { file } => ("batch", basic::batch(ctx, &file)),
    }
}

/// Runs a parsed command line and returns the process exit code.
pub fn run(cli: Cli) -> i32 {
    let started = Instant::now();
    let Some(command) = cli.command else {
        use clap::CommandFactory;
        let _ = Cli::command().print_help();
        return 0;
    };
    let format = cli.global.output;
    let mut ctx = Ctx::new(cli.global);
    let (name, result) = dispatch(&mut ctx, command);
    let mut envelope = Envelope::from_result(name, result, started.elapsed());
    envelope.meta.instance = ctx.instance.as_ref().map(|i| i.pid.to_string());
    envelope.print(format);
    envelope.exit_code()
}

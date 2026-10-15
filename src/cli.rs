use std::path::PathBuf;
use std::time::Instant;

use clap::{Args, Parser, Subcommand, ValueEnum};

use crate::commands::{
    Ctx, anim, basic, blueprint, code, fx, gas, light, loc, online, perf, project, server,
    test_runner, typed, visual,
};
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
    name = "unrealengine-cli",
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
        /// Block until a new line contains this text (starts from the current end of the log)
        #[arg(long = "wait-for")]
        wait_for: Option<String>,
        /// Seconds to wait with --wait-for
        #[arg(long = "wait-seconds", default_value_t = 60)]
        wait_seconds: u64,
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
    /// Install the UnrealCliBridge plugin into the project
    Setup {
        /// Build the editor target afterwards
        #[arg(long)]
        build: bool,
        /// Add a minimal C++ module to a Blueprint-only project
        #[arg(long = "add-code-module")]
        add_code_module: bool,
    },
    /// Check engine, build tools, plugin and editor
    Doctor {
        /// Print a Markdown summary to paste into a bug report
        #[arg(long)]
        report: bool,
    },
    /// Editor lifecycle: open, close, build, status
    Editor {
        #[command(subcommand)]
        action: EditorCmd,
    },
    /// Blueprint editing
    Blueprint {
        #[command(subcommand)]
        action: crate::commands::blueprint::BlueprintCmd,
    },
    /// Levels and actors
    Level {
        #[command(subcommand)]
        action: typed::LevelCmd,
    },
    /// Assets: search, create, move, delete, import, references
    Asset {
        #[command(subcommand)]
        action: typed::AssetCmd,
    },
    /// Reflected properties and components
    Prop {
        #[command(subcommand)]
        action: typed::PropCmd,
    },
    /// Play In Editor
    Pie {
        /// World of a multiplayer session: server, client:1, client:2, ... (see `pie worlds`)
        #[arg(long, global = true)]
        world: Option<String>,
        #[command(subcommand)]
        action: typed::PieCmd,
    },
    /// Multiplayer checks: compare replicated values across worlds, simulate lag and loss
    Net {
        #[command(subcommand)]
        action: typed::NetCmd,
    },
    /// Dedicated servers: Server target, build, package, start, list, logs, stop
    Server {
        #[command(subcommand)]
        action: server::ServerCmd,
    },
    /// Online subsystem (Null, EOS, Steam): configure, check, and sessions in PIE
    Online {
        #[command(subcommand)]
        action: online::OnlineCmd,
    },
    /// Gameplay Ability System: enable, AttributeSet C++, effects, abilities, cues, and runtime in PIE
    Gas {
        #[command(subcommand)]
        action: gas::GasCmd,
    },
    /// Gameplay Tags: list, add, rename, remove
    Tags {
        #[command(subcommand)]
        action: gas::TagsCmd,
    },
    /// Screenshot comparison against baselines (Tests/Visual/<name>.png) and image diffs
    Visual {
        #[command(subcommand)]
        action: visual::VisualCmd,
    },
    /// Performance: measure a play session, Unreal Insights traces and their summary
    Perf {
        #[command(subcommand)]
        action: perf::PerfCmd,
    },
    /// Lighting: presets, static lighting builds, HLOD builds
    Light {
        #[command(subcommand)]
        action: light::LightCmd,
    },
    /// Project rendering settings: Lumen, virtual shadow maps, Nanite, anti-aliasing, ...
    Render {
        #[command(subcommand)]
        action: light::RenderCmd,
    },
    /// Animation rigs: IK Rig, IK Retargeter and batch retargeting, Control Rig, Motion Matching
    Anim {
        #[command(subcommand)]
        action: anim::AnimCmd,
    },
    /// Audio: MetaSound sources built node by node
    Audio {
        #[command(subcommand)]
        action: fx::AudioCmd,
    },
    /// Chaos physics: Geometry Collections (destructibles)
    Physics {
        #[command(subcommand)]
        action: fx::PhysicsCmd,
    },
    /// Procedural Content Generation graphs: create, add nodes, connect, generate in the level
    Pcg {
        #[command(subcommand)]
        action: fx::PcgCmd,
    },
    /// Localization: target, gather, export / translate / import PO files, compile
    Loc {
        #[command(subcommand)]
        action: loc::LocCmd,
    },
    /// Fab marketplace: search, your library, add listings to the project
    Fab {
        #[command(subcommand)]
        action: typed::FabCmd,
    },
    /// Automation tests
    Test {
        #[command(subcommand)]
        action: test_runner::TestCmd,
    },
    /// C++ sources: search, read, edit, create classes, apply changes
    Code {
        #[command(subcommand)]
        action: code::CodeCmd,
    },
    /// Agent skills: install, refresh, uninstall, list, lint (no editor needed)
    Skills {
        #[command(subcommand)]
        action: SkillsCmd,
    },
    /// MCP server mode
    Mcp {
        #[command(subcommand)]
        action: McpCmd,
    },
    /// Search engine headers or the Python API stub (read-only)
    Reference {
        #[command(subcommand)]
        action: ReferenceCmd,
    },
    /// Update this binary from GitHub Releases (verified with SHA256SUMS)
    #[command(name = "self")]
    SelfCmd {
        #[command(subcommand)]
        action: SelfAction,
    },
}

#[derive(Debug, Subcommand)]
pub enum SkillsCmd {
    List,
    /// Install into a client: claude-code | codex | cursor | windsurf | vscode
    Install {
        client: String,
        #[arg(long, conflicts_with = "global")]
        local: bool,
        #[arg(long)]
        global: bool,
    },
    /// Re-install into every client directory installed before
    Refresh {
        #[arg(long, conflicts_with = "global")]
        local: bool,
        #[arg(long)]
        global: bool,
    },
    Uninstall {
        client: String,
        #[arg(long, conflicts_with = "global")]
        local: bool,
        #[arg(long)]
        global: bool,
    },
    /// Check bundled skills against the skill contract
    Lint {
        #[arg(long, default_value = "warning")]
        severity: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum McpCmd {
    /// Serve MCP over stdio
    Serve,
    /// Register the server with a client: claude-code | cursor | vscode | codex
    Configure { client: String },
}

#[derive(Debug, Subcommand)]
pub enum ReferenceCmd {
    Search {
        query: String,
        /// cpp | python | all
        #[arg(long, default_value = "all")]
        scope: String,
        #[arg(long)]
        regex: bool,
        #[arg(long, default_value_t = 50)]
        limit: usize,
    },
}

#[derive(Debug, Subcommand)]
pub enum SelfAction {
    Update {
        /// Release tag (default: latest)
        #[arg(long)]
        version: Option<String>,
        /// Only report whether a newer release exists
        #[arg(long)]
        check: bool,
        /// Install the latest release only if newer (used by the daily background update)
        #[arg(long, hide = true)]
        auto: bool,
    },
}

fn command_names() -> std::collections::HashSet<String> {
    use clap::CommandFactory;
    Cli::command()
        .get_subcommands()
        .map(|c| c.get_name().to_string())
        .collect()
}

#[derive(Debug, Subcommand)]
pub enum EditorCmd {
    /// Start the editor and wait until the bridge answers
    Open {
        /// No window, offscreen rendering (-RenderOffscreen)
        #[arg(long)]
        headless: bool,
        /// No rendering at all (-nullrhi); fastest, but spawning mesh actors can crash the editor
        #[arg(long = "null-rhi")]
        null_rhi: bool,
        /// Seconds to wait for the bridge
        #[arg(long, default_value_t = 600)]
        wait: u64,
    },
    /// Quit the editor (refused with unsaved changes unless --force)
    Close {
        #[arg(long)]
        force: bool,
    },
    /// Build the project's editor target (editor must be closed)
    Build,
    /// Engine, plugin and running state of the project
    Status,
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
    /// List tools (live editor if reachable, else the bundled catalog)
    List {
        #[arg(long)]
        group: Option<String>,
    },
    /// Search tools by words in name, group and summary
    Find {
        #[arg(required = true, num_args = 1..)]
        words: Vec<String>,
        #[arg(long, default_value_t = 20)]
        limit: usize,
    },
    /// Full description and input schema of a tool
    Describe { name: String },
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
            severity,
            category,
            wait_for: Some(text),
            wait_seconds,
            ..
        } => (
            "logs",
            basic::wait_for_log(ctx, &text, severity, category, wait_seconds),
        ),
        Command::Logs {
            since,
            limit,
            severity,
            category,
            ..
        } => ("logs", basic::logs(ctx, since, limit, severity, category)),
        Command::Instances { action } => match action {
            InstancesCmd::List => ("instances list", basic::instances_list()),
            InstancesCmd::Use { pid } => ("instances use", basic::instances_use(pid)),
        },
        Command::Tool { action } => match action {
            ToolCmd::Call { name, args } => {
                ("tool call", basic::tool_call(ctx, &name, args.as_deref()))
            }
            ToolCmd::List { group } => ("tool list", project::tool_list(ctx, group.as_deref())),
            ToolCmd::Find { words, limit } => (
                "tool find",
                project::tool_find(ctx, &words.join(" "), limit),
            ),
            ToolCmd::Describe { name } => ("tool describe", project::tool_describe(ctx, &name)),
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
        Command::Setup {
            build,
            add_code_module,
        } => ("setup", project::setup_cmd(ctx, build, add_code_module)),
        Command::Doctor { report } => ("doctor", project::doctor_cmd(ctx, report)),
        Command::Editor { action } => match action {
            EditorCmd::Open {
                headless,
                null_rhi,
                wait,
            } => (
                "editor open",
                project::open_cmd(ctx, headless, null_rhi, wait),
            ),
            EditorCmd::Close { force } => ("editor close", project::close_cmd(ctx, force)),
            EditorCmd::Build => ("editor build", project::build_cmd(ctx)),
            EditorCmd::Status => ("editor status", project::status_cmd(ctx)),
        },
        Command::Blueprint { action } => ("blueprint", blueprint::run(ctx, action)),
        Command::Level { action } => ("level", typed::level(ctx, action)),
        Command::Asset { action } => ("asset", typed::asset(ctx, action)),
        Command::Prop { action } => ("prop", typed::prop(ctx, action)),
        Command::Pie { world, action } => ("pie", typed::pie(ctx, action, world)),
        Command::Fab { action } => ("fab", typed::fab(ctx, action)),
        Command::Net { action } => ("net", typed::net(ctx, action)),
        Command::Server { action } => ("server", server::run(ctx, action)),
        Command::Online { action } => ("online", online::run(ctx, action)),
        Command::Gas { action } => ("gas", gas::run(ctx, action)),
        Command::Visual { action } => ("visual", visual::run(ctx, action)),
        Command::Perf { action } => ("perf", perf::run(ctx, action)),
        Command::Light { action } => ("light", light::run(ctx, action)),
        Command::Anim { action } => ("anim", anim::run(ctx, action)),
        Command::Loc { action } => ("loc", loc::run(ctx, action)),
        Command::Audio { action } => ("audio", fx::audio(ctx, action)),
        Command::Physics { action } => ("physics", fx::physics(ctx, action)),
        Command::Pcg { action } => ("pcg", fx::pcg(ctx, action)),
        Command::Render { action } => ("render", light::render(ctx, action)),
        Command::Tags { action } => ("tags", gas::tags(ctx, action)),
        Command::Test { action } => ("test", test_runner::run(ctx, action)),
        Command::Code { action } => ("code", code::run(ctx, action)),
        Command::Skills { action } => {
            let cwd = ctx.cwd.clone();
            let dry = ctx.global.dry_run;
            let r = match action {
                SkillsCmd::List => Ok(crate::skills::list()),
                SkillsCmd::Install { client, global, .. } => {
                    crate::skills::install(&client, global, &cwd, dry)
                }
                SkillsCmd::Refresh { global, .. } => crate::skills::refresh(global, &cwd, dry),
                SkillsCmd::Uninstall { client, global, .. } => {
                    crate::skills::uninstall(&client, global, &cwd, dry)
                }
                SkillsCmd::Lint { severity } => crate::skills::lint(&severity, &command_names()),
            };
            ("skills", r)
        }
        Command::Mcp { action } => match action {
            McpCmd::Serve => ("mcp serve", crate::mcp::serve(&ctx.global)),
            McpCmd::Configure { client } => (
                "mcp configure",
                crate::mcp::configure(&client, &ctx.cwd, ctx.global.dry_run),
            ),
        },
        Command::Reference { action } => match action {
            ReferenceCmd::Search {
                query,
                scope,
                regex,
                limit,
            } => {
                let up = ctx.uproject().ok();
                let engine = up
                    .as_deref()
                    .and_then(|u| crate::engine::locate::resolve(u).ok());
                let dir = up
                    .as_deref()
                    .and_then(|u| u.parent().map(|p| p.to_path_buf()));
                (
                    "reference search",
                    crate::reference::search(
                        engine.as_ref().map(|e| e.root.as_path()),
                        dir.as_deref(),
                        &query,
                        &scope,
                        regex,
                        limit,
                    ),
                )
            }
        },
        Command::SelfCmd { action } => match action {
            SelfAction::Update { check: true, .. } => ("self update", Ok(crate::update::check())),
            SelfAction::Update { auto: true, .. } => ("self update", crate::update::auto_apply()),
            SelfAction::Update { version, .. } => (
                "self update",
                crate::update::self_update(version.as_deref(), ctx.global.dry_run),
            ),
        },
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
    envelope.warnings.append(&mut ctx.warnings);
    envelope.print(format);
    envelope.exit_code()
}

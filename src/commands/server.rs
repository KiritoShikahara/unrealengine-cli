use std::ffi::OsStr;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use clap::Subcommand;
use serde_json::{Value, json};

use super::Ctx;
use crate::core::error::CliError;
use crate::core::exit_code::ExitCode;
use crate::core::paths;
use crate::engine::locate::Engine;
use crate::engine::{build, editor, locate, spawn};
use crate::servers::{self, ServerRecord};

const CONFIGS: [&str; 5] = ["Debug", "DebugGame", "Development", "Test", "Shipping"];

#[derive(Debug, Subcommand)]
pub enum ServerCmd {
    /// The project's Server target (Source/<Project>Server.Target.cs)
    Target {
        #[command(subcommand)]
        action: TargetCmd,
    },
    /// Build the Server target (needs an engine built from source)
    Build {
        #[arg(long, default_value = "Development", value_parser = CONFIGS)]
        config: String,
    },
    /// Cook and package the dedicated server into a folder outside the project
    Package {
        /// Destination folder (outside the project)
        #[arg(long)]
        out_dir: PathBuf,
        #[arg(long, default_value = "Development", value_parser = CONFIGS)]
        config: String,
    },
    /// Start a dedicated server and wait until it listens. Uses Binaries/<platform>/<Project>Server
    /// when built, else the editor binary in server mode (works with Launcher engines)
    Start {
        /// Map to load (/Game/Maps/Arena)
        #[arg(long)]
        map: Option<String>,
        #[arg(long, default_value_t = 7777)]
        port: u16,
        /// Use the editor binary even when a Server build exists
        #[arg(long)]
        editor: bool,
        /// Seconds to wait for "listening on port"
        #[arg(long, default_value_t = 180)]
        wait: u64,
        /// Extra server arguments after --
        #[arg(last = true)]
        args: Vec<String>,
    },
    /// Servers started by `server start` that are still running
    List,
    /// Tail of a server's log
    Logs {
        pid: u32,
        #[arg(long, default_value_t = 50)]
        tail: usize,
    },
    /// Stop a server started by `server start`
    Stop { pid: u32 },
}

#[derive(Debug, Subcommand)]
pub enum TargetCmd {
    /// Create <Project>Server.Target.cs from the game target
    Add,
}

pub fn run(ctx: &mut Ctx, cmd: ServerCmd) -> Result<Value, CliError> {
    match cmd {
        ServerCmd::Target {
            action: TargetCmd::Add,
        } => target_add(ctx),
        ServerCmd::Build { config } => build_cmd(ctx, &config),
        ServerCmd::Package { out_dir, config } => package(ctx, &out_dir, &config),
        ServerCmd::Start {
            map,
            port,
            editor,
            wait,
            args,
        } => start(ctx, map, port, editor, wait, args),
        ServerCmd::List => Ok(json!({ "servers": servers::list_live() })),
        ServerCmd::Logs { pid, tail } => {
            let r = recorded(pid)?;
            Ok(
                json!({ "pid": pid, "logFile": r.log_file, "lines": servers::log_tail(&r.log_file, tail) }),
            )
        }
        ServerCmd::Stop { pid } => stop(ctx, pid),
    }
}

fn project_name(up: &Path) -> String {
    up.file_stem()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string()
}

fn server_target_path(up: &Path) -> PathBuf {
    up.parent()
        .unwrap_or(Path::new("."))
        .join("Source")
        .join(format!("{}Server.Target.cs", project_name(up)))
}

/// The game target with its class renamed to `<Name>ServerTarget` and its type set to Server.
pub fn server_target_from_game(game: &str, name: &str) -> String {
    game.replace(&format!("{name}Target"), &format!("{name}ServerTarget"))
        .replace("TargetType.Game", "TargetType.Server")
}

fn target_add(ctx: &mut Ctx) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    let name = project_name(&up);
    let path = server_target_path(&up);
    if path.is_file() {
        return Ok(json!({ "path": path, "created": false }));
    }
    let game_path = path.with_file_name(format!("{name}.Target.cs"));
    let no_code = || {
        CliError::usage(
            "server.no_code",
            format!("{} has no game target to derive from", game_path.display()),
        )
        .with_hint("add a C++ module first: unrealengine-cli setup --add-code-module")
    };
    let game = std::fs::read_to_string(&game_path).map_err(|_| no_code())?;
    if !game.contains("TargetType.Game") {
        return Err(no_code());
    }
    let content = server_target_from_game(&game, &name);
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "path": path, "content": content }));
    }
    std::fs::write(&path, &content)
        .map_err(|e| CliError::internal("server.write", format!("{}: {e}", path.display())))?;
    Ok(json!({ "path": path, "created": true }))
}

fn require_source_engine(engine: &Engine) -> Result<(), CliError> {
    if engine
        .root
        .join("Engine/Build/InstalledBuild.txt")
        .is_file()
    {
        return Err(CliError::editor(
            "server.source_engine_required",
            format!(
                "{} is an installed (Launcher) engine; dedicated server builds need an engine built from source",
                engine.root.display()
            ),
        )
        .with_hint("use a source build of Unreal Engine, or test with the editor binary: unrealengine-cli server start"));
    }
    Ok(())
}

fn require_target(up: &Path) -> Result<String, CliError> {
    if !server_target_path(up).is_file() {
        return Err(CliError::editor(
            "server.no_target",
            format!("{} not found", server_target_path(up).display()),
        )
        .with_hint("create it: unrealengine-cli server target add"));
    }
    Ok(format!("{}Server", project_name(up)))
}

fn build_cmd(ctx: &mut Ctx, config: &str) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    let engine = locate::resolve(&up)?;
    require_source_engine(&engine)?;
    let target = require_target(&up)?;
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "target": target, "config": config, "engine": engine }));
    }
    build::build_target(&engine, &up, &target, config)
}

fn package(ctx: &mut Ctx, output: &Path, config: &str) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    let out = if output.is_absolute() {
        output.to_path_buf()
    } else {
        ctx.cwd.join(output)
    };
    let project_dir = lexical(&paths::normalize(up.parent().unwrap_or(Path::new("."))));
    let out_abs = normalize_partial(&lexical(&out));
    let key = |p: &Path| {
        let s = p.to_string_lossy().replace('\\', "/");
        if cfg!(windows) || cfg!(target_os = "macos") {
            PathBuf::from(s.to_lowercase())
        } else {
            PathBuf::from(s)
        }
    };
    if key(&out_abs).starts_with(key(&project_dir)) {
        return Err(CliError::usage(
            "server.unsafe_output",
            format!("{} is inside the project", out.display()),
        )
        .with_hint("package to a folder outside the project, e.g. --output ../ServerBuild"));
    }
    let engine = locate::resolve(&up)?;
    require_source_engine(&engine)?;
    require_target(&up)?;
    let uat = engine.root.join(if cfg!(windows) {
        "Engine/Build/BatchFiles/RunUAT.bat"
    } else {
        "Engine/Build/BatchFiles/RunUAT.sh"
    });
    let platform = build::host_platform();
    let args: Vec<String> = vec![
        "BuildCookRun".into(),
        format!("-project={}", up.display()),
        "-noP4".into(),
        "-server".into(),
        "-noclient".into(),
        format!("-serverconfig={config}"),
        format!("-serverplatform={platform}"),
        format!("-platform={platform}"),
        "-build".into(),
        "-cook".into(),
        "-stage".into(),
        "-pak".into(),
        "-archive".into(),
        format!("-archivedirectory={}", out.display()),
        "-utf8output".into(),
    ];
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "uat": uat, "args": args, "output": out }));
    }
    if !uat.is_file() {
        return Err(CliError::usage(
            "engine.no_build_tools",
            format!("{} not found", uat.display()),
        ));
    }
    let started = Instant::now();
    let res = Command::new(&uat).args(&args).output().map_err(|e| {
        CliError::internal("server.spawn", format!("cannot run {}: {e}", uat.display()))
    })?;
    let text = format!(
        "{}{}",
        String::from_utf8_lossy(&res.stdout),
        String::from_utf8_lossy(&res.stderr)
    );
    let diagnostics = build::parse_diagnostics(&text);
    let tail: Vec<&str> = {
        let l: Vec<&str> = text.lines().collect();
        l[l.len().saturating_sub(15)..].to_vec()
    };
    let data = json!({
        "output": out,
        "succeeded": res.status.success(),
        "seconds": started.elapsed().as_secs(),
        "diagnostics": diagnostics,
        "logTail": tail,
    });
    if res.status.success() {
        Ok(data)
    } else {
        Err(CliError::new(
            ExitCode::Build,
            "server.package_failed",
            "packaging the dedicated server failed",
        )
        .with_hint("fix the first error in details.logTail and run again")
        .with_details(data))
    }
}

/// Resolves `.` and `..` without touching the file system (the folder may not exist yet).
fn lexical(p: &Path) -> PathBuf {
    use std::path::Component;
    let mut out = PathBuf::new();
    for c in p.components() {
        match c {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            other => out.push(other.as_os_str()),
        }
    }
    out
}

/// Like `paths::normalize` for a path that may not exist yet: canonicalizes its deepest existing
/// ancestor (expanding 8.3 short names and links) and appends the rest.
fn normalize_partial(p: &Path) -> PathBuf {
    let mut existing = p.to_path_buf();
    let mut rest = Vec::new();
    while !existing.exists() {
        match (
            existing.file_name().map(|n| n.to_os_string()),
            existing.parent(),
        ) {
            (Some(name), Some(parent)) => {
                rest.push(name);
                existing = parent.to_path_buf();
            }
            _ => return paths::normalize(p),
        }
    }
    let mut out = paths::normalize(&existing);
    for name in rest.iter().rev() {
        out.push(name);
    }
    out
}

fn port_in_use(port: u16) -> bool {
    servers::list_live().iter().any(|r| r.port == port)
        || std::net::UdpSocket::bind(("0.0.0.0", port)).is_err()
}

fn start(
    ctx: &mut Ctx,
    map: Option<String>,
    port: u16,
    force_editor: bool,
    wait: u64,
    extra: Vec<String>,
) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    let engine = locate::resolve(&up)?;
    if port_in_use(port) {
        return Err(CliError::editor(
            "server.port_in_use",
            format!("UDP port {port} is already in use"),
        )
        .with_hint("pick another: --port 7778, or see: unrealengine-cli server list"));
    }
    let name = project_name(&up);
    let built = up
        .parent()
        .unwrap_or(Path::new("."))
        .join("Binaries")
        .join(build::host_platform())
        .join(format!("{name}Server{}", std::env::consts::EXE_SUFFIX));
    let use_built = !force_editor && built.is_file();
    let exe = if use_built {
        built
    } else {
        engine.editor_exe()
    };
    let log_dir = servers::dir().join("logs");
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let log_file = log_dir.join(format!("{name}-{port}-{now}.log"));
    let mut args: Vec<String> = vec![up.to_string_lossy().to_string()];
    if let Some(m) = &map {
        args.push(m.clone());
    }
    if !use_built {
        args.extend(["-server", "-nosplash", "-unattended"].map(String::from));
    }
    args.push(format!("-port={port}"));
    args.push("-log".into());
    args.push(format!("-abslog={}", log_file.display()));
    args.extend(extra);
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "executable": exe, "args": args, "logFile": log_file }));
    }
    if !exe.is_file() {
        return Err(CliError::usage(
            "server.no_executable",
            format!("{} not found", exe.display()),
        ));
    }
    std::fs::create_dir_all(&log_dir)
        .map_err(|e| CliError::internal("server.write", format!("{}: {e}", log_dir.display())))?;
    let os_args: Vec<&OsStr> = args.iter().map(OsStr::new).collect();
    let pid = spawn::spawn_detached(&exe, &os_args)?;
    let record = ServerRecord {
        pid,
        project: up.clone(),
        executable: exe.clone(),
        map: map.clone(),
        port,
        log_file: log_file.clone(),
        started_at: now,
    };
    servers::save(&record)
        .map_err(|e| CliError::internal("server.write", format!("server record: {e}")))?;
    let started = Instant::now();
    loop {
        let log = std::fs::read(&log_file).unwrap_or_default();
        if String::from_utf8_lossy(&log)
            .to_lowercase()
            .contains("listening on port")
        {
            let mut v = serde_json::to_value(&record).unwrap_or_default();
            v["state"] = json!("listening");
            v["seconds"] = json!(started.elapsed().as_secs());
            return Ok(v);
        }
        if !editor::process_alive(pid) {
            servers::remove(pid);
            return Err(CliError::editor(
                "server.exited",
                format!("the server (pid {pid}) exited during startup"),
            )
            .with_hint("read the log tail in details; check the map path and the Server build")
            .with_details(
                json!({ "logFile": log_file, "logTail": servers::log_tail(&log_file, 30) }),
            ));
        }
        if started.elapsed() >= Duration::from_secs(wait) {
            return Err(CliError::timeout(
                "server.start_timeout",
                format!("the server (pid {pid}) did not listen within {wait}s"),
            )
            .with_hint(format!(
                "it is still running: unrealengine-cli server logs {pid}, or stop it: unrealengine-cli server stop {pid}"
            ))
            .with_details(json!({ "pid": pid, "logFile": log_file })));
        }
        std::thread::sleep(Duration::from_millis(500));
    }
}

fn recorded(pid: u32) -> Result<ServerRecord, CliError> {
    servers::load(pid).ok_or_else(|| {
        CliError::usage(
            "server.unknown_pid",
            format!("no server with pid {pid} was started by unrealengine-cli"),
        )
        .with_hint("see running servers: unrealengine-cli server list")
    })
}

fn stop(ctx: &mut Ctx, pid: u32) -> Result<Value, CliError> {
    let r = recorded(pid)?;
    let running = editor::process_alive(pid);
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "pid": pid, "running": running }));
    }
    if running {
        editor::kill_process_tree(pid);
        let deadline = Instant::now() + Duration::from_secs(10);
        while editor::process_alive(pid) && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(100));
        }
        if editor::process_alive(pid) {
            return Err(CliError::editor(
                "server.kill_failed",
                format!("could not end server process {pid}"),
            ));
        }
    }
    servers::remove(pid);
    Ok(json!({ "pid": pid, "port": r.port, "stopped": running }))
}

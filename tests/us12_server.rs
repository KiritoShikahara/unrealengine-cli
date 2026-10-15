mod support;

use std::path::Path;
use std::time::{Duration, Instant};

use assert_cmd::Command;
use serde_json::{Value, json};
use support::mock_bridge::MockBuilder;
use support::write_instance;

const GAME_TARGET: &str = "using UnrealBuildTool;\n\npublic class FooTarget : TargetRules\n{\n\tpublic FooTarget(TargetInfo Target) : base(Target)\n\t{\n\t\tType = TargetType.Game;\n\t\tExtraModuleNames.Add(\"Foo\");\n\t}\n}\n";

/// A project folder with Foo.uproject (and Source/Foo.Target.cs when `code`).
fn project(code: bool) -> tempfile::TempDir {
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(
        dir.path().join("Foo.uproject"),
        r#"{"FileVersion":3,"EngineAssociation":"5.7"}"#,
    )
    .unwrap();
    if code {
        std::fs::create_dir_all(dir.path().join("Source")).unwrap();
        std::fs::write(dir.path().join("Source/Foo.Target.cs"), GAME_TARGET).unwrap();
    }
    dir
}

/// A fake engine root; `installed` adds Engine/Build/InstalledBuild.txt (Launcher engine).
fn engine(installed: bool) -> tempfile::TempDir {
    let dir = tempfile::tempdir().unwrap();
    std::fs::create_dir_all(dir.path().join("Engine/Build")).unwrap();
    if installed {
        std::fs::write(dir.path().join("Engine/Build/InstalledBuild.txt"), "").unwrap();
    }
    dir
}

fn run(home: &Path, cwd: &Path, engine: Option<&Path>, args: &[&str]) -> (i32, Value) {
    let mut cmd = Command::cargo_bin("unrealengine-cli").unwrap();
    cmd.env("UNREALENGINE_CLI_HOME", home)
        .env("UNREALENGINE_CLI_NO_AUTO_UPDATE", "1")
        .current_dir(cwd);
    if let Some(e) = engine {
        cmd.env("UNREALENGINE_CLI_ENGINE_DIR", e);
    }
    let out = cmd.arg("--output").arg("json").args(args).output().unwrap();
    let text = String::from_utf8_lossy(&out.stdout);
    let v = serde_json::from_str(&text).unwrap_or_else(|e| panic!("not an envelope ({e}): {text}"));
    (out.status.code().unwrap_or(-1), v)
}

#[test]
fn target_add_derives_the_server_target_from_the_game_target() {
    let home = tempfile::tempdir().unwrap();
    let p = project(true);
    let target = p.path().join("Source/FooServer.Target.cs");

    let (code, v) = run(
        home.path(),
        p.path(),
        None,
        &["--dry-run", "server", "target", "add"],
    );
    assert_eq!(code, 0, "{v}");
    let content = v["data"]["content"].as_str().unwrap();
    assert!(content.contains("public class FooServerTarget : TargetRules"));
    assert!(content.contains("public FooServerTarget(TargetInfo Target)"));
    assert!(content.contains("Type = TargetType.Server;"));
    assert!(content.contains("ExtraModuleNames.Add(\"Foo\");"));
    assert!(!target.exists(), "dry-run must not write");

    let (code, v) = run(home.path(), p.path(), None, &["server", "target", "add"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["created"], true);
    assert_eq!(std::fs::read_to_string(&target).unwrap(), content);

    let (code, v) = run(home.path(), p.path(), None, &["server", "target", "add"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["created"], false);
}

#[test]
fn target_add_needs_a_code_project() {
    let home = tempfile::tempdir().unwrap();
    let p = project(false);
    let (code, v) = run(home.path(), p.path(), None, &["server", "target", "add"]);
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "server.no_code");
}

#[test]
fn build_on_an_installed_engine_is_refused_quickly() {
    let home = tempfile::tempdir().unwrap();
    let p = project(true);
    let e = engine(true);
    for args in [
        vec!["server", "build"],
        vec!["server", "package", "--out-dir", "../ServerOut"],
    ] {
        let started = Instant::now();
        let (code, v) = run(home.path(), p.path(), Some(e.path()), &args);
        assert_eq!(code, 4, "{args:?}: {v}");
        assert_eq!(v["error"]["code"], "server.source_engine_required");
        assert!(started.elapsed() < Duration::from_secs(5));
    }
}

#[test]
fn build_without_a_server_target_says_so() {
    let home = tempfile::tempdir().unwrap();
    let p = project(true);
    let e = engine(false);
    let (code, v) = run(home.path(), p.path(), Some(e.path()), &["server", "build"]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "server.no_target");
}

#[test]
fn package_refuses_an_output_inside_the_project() {
    let home = tempfile::tempdir().unwrap();
    let p = project(true);
    let e = engine(false);
    let (code, v) = run(
        home.path(),
        p.path(),
        Some(e.path()),
        &["server", "package", "--out-dir", "Content/Server"],
    );
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "server.unsafe_output");
}

#[test]
fn start_refuses_a_port_in_use() {
    let home = tempfile::tempdir().unwrap();
    let p = project(true);
    let e = engine(true);
    let busy = std::net::UdpSocket::bind(("0.0.0.0", 0)).unwrap();
    let port = busy.local_addr().unwrap().port().to_string();
    let (code, v) = run(
        home.path(),
        p.path(),
        Some(e.path()),
        &["server", "start", "--port", &port],
    );
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "server.port_in_use");
}

#[test]
fn start_dry_run_runs_the_editor_as_a_server() {
    let home = tempfile::tempdir().unwrap();
    let p = project(true);
    let e = engine(true);
    let (code, v) = run(
        home.path(),
        p.path(),
        Some(e.path()),
        &[
            "--dry-run",
            "server",
            "start",
            "--map",
            "/Game/Arena",
            "--port",
            "7790",
            "--",
            "-MyFlag",
        ],
    );
    assert_eq!(code, 0, "{v}");
    let args: Vec<&str> = v["data"]["args"]
        .as_array()
        .unwrap()
        .iter()
        .map(|a| a.as_str().unwrap())
        .collect();
    assert!(args[0].ends_with("Foo.uproject"), "{args:?}");
    assert_eq!(args[1], "/Game/Arena");
    for a in ["-server", "-port=7790", "-log", "-MyFlag"] {
        assert!(args.contains(&a), "{a} missing: {args:?}");
    }
    assert!(
        v["data"]["executable"]
            .as_str()
            .unwrap()
            .contains("UnrealEditor")
    );
}

#[test]
fn stop_and_logs_refuse_processes_not_started_by_server_start() {
    let home = tempfile::tempdir().unwrap();
    let pid = std::process::id().to_string();
    for args in [vec!["server", "stop", &pid], vec!["server", "logs", &pid]] {
        let (code, v) = run(home.path(), home.path(), None, &args);
        assert_eq!(code, 2, "{args:?}: {v}");
        assert_eq!(v["error"]["code"], "server.unknown_pid");
    }
}

fn sleeper() -> std::process::Child {
    if cfg!(windows) {
        std::process::Command::new("ping")
            .args(["-n", "60", "127.0.0.1"])
            .stdout(std::process::Stdio::null())
            .spawn()
            .unwrap()
    } else {
        std::process::Command::new("sleep")
            .arg("60")
            .spawn()
            .unwrap()
    }
}

/// A long-running process that is not our child, like a server from `server start` (a child of ours
/// would stay a zombie after being killed): its pid and a "still running?" check.
fn detached_sleeper() -> (u32, Box<dyn FnMut() -> bool>) {
    if cfg!(windows) {
        let mut child = sleeper();
        (
            child.id(),
            Box::new(move || child.try_wait().unwrap().is_none()),
        )
    } else {
        let out = std::process::Command::new("sh")
            .args(["-c", "sleep 60 >/dev/null 2>&1 & echo $!"])
            .output()
            .unwrap();
        let pid: u32 = String::from_utf8_lossy(&out.stdout).trim().parse().unwrap();
        (
            pid,
            Box::new(move || {
                std::process::Command::new("kill")
                    .args(["-0", &pid.to_string()])
                    .status()
                    .unwrap()
                    .success()
            }),
        )
    }
}

fn write_record(home: &Path, pid: u32, log: &Path) {
    let dir = home.join("servers");
    std::fs::create_dir_all(&dir).unwrap();
    let rec = json!({
        "pid": pid, "project": "C:/P/Foo.uproject", "executable": "FooServer",
        "map": "/Game/Arena", "port": 7777, "logFile": log, "startedAt": 1
    });
    std::fs::write(dir.join(format!("{pid}.json")), rec.to_string()).unwrap();
}

#[test]
fn recorded_server_is_listed_logged_and_stopped() {
    let home = tempfile::tempdir().unwrap();
    let (pid, mut running) = detached_sleeper();
    let log = home.path().join("server.log");
    std::fs::write(&log, "line 1\nLogNet: listening on port 7777\nline 3\n").unwrap();
    write_record(home.path(), pid, &log);
    let pid_s = pid.to_string();

    let (code, v) = run(home.path(), home.path(), None, &["server", "list"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["servers"][0]["pid"], pid);

    let (code, v) = run(
        home.path(),
        home.path(),
        None,
        &["server", "logs", &pid_s, "--tail", "2"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(
        v["data"]["lines"],
        json!(["LogNet: listening on port 7777", "line 3"])
    );

    let (code, v) = run(home.path(), home.path(), None, &["server", "stop", &pid_s]);
    assert_eq!(code, 0, "{v}");
    let deadline = Instant::now() + Duration::from_secs(5);
    while running() {
        assert!(Instant::now() < deadline, "server process still running");
        std::thread::sleep(Duration::from_millis(100));
    }
    assert!(!home.path().join(format!("servers/{pid}.json")).exists());
}

#[test]
fn list_drops_servers_that_exited() {
    let home = tempfile::tempdir().unwrap();
    let mut child = sleeper();
    let pid = child.id();
    child.kill().unwrap();
    child.wait().unwrap();
    write_record(home.path(), pid, &home.path().join("x.log"));
    let (code, v) = run(home.path(), home.path(), None, &["server", "list"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["servers"], json!([]));
    assert!(!home.path().join(format!("servers/{pid}.json")).exists());
}

#[test]
fn pie_connect_opens_the_address_in_the_play_world() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("pie_exec", |a| Ok(a.clone()))
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run(
        home.path(),
        home.path(),
        None,
        &["pie", "--world", "client:1", "connect", "127.0.0.1:7777"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(
        v["data"],
        json!({"command": "open 127.0.0.1:7777", "world": "client:1"})
    );
}

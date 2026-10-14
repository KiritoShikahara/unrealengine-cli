mod support;

use std::time::Instant;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::write_instance;

fn percentile(mut v: Vec<u128>, p: f64) -> u128 {
    v.sort();
    v[((v.len() as f64 - 1.0) * p).round() as usize]
}

#[test]
fn single_call_p95_under_300ms_including_process_start() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("system_ping", |_| Ok(json!({"pong": true})))
        .start();
    write_instance(home.path(), &m, "p");
    let mut times = Vec::new();
    for _ in 0..30 {
        let t = Instant::now();
        let out = std::process::Command::new(env!("CARGO_BIN_EXE_unreal-cli"))
            .env("UNREAL_CLI_HOME", home.path())
            .current_dir(home.path())
            .args(["--output", "json", "ping"])
            .output()
            .unwrap();
        assert!(out.status.success());
        times.push(t.elapsed().as_millis());
    }
    let p95 = percentile(times, 0.95);
    eprintln!("ping p95 = {p95} ms");
    assert!(p95 < 300, "p95 {p95} ms");
}

#[test]
fn startup_is_fast() {
    let mut times = Vec::new();
    for _ in 0..20 {
        let t = Instant::now();
        std::process::Command::new(env!("CARGO_BIN_EXE_unreal-cli"))
            .arg("version")
            .output()
            .unwrap();
        times.push(t.elapsed().as_millis());
    }
    let p50 = percentile(times, 0.5);
    eprintln!("startup p50 = {p50} ms");
    // 30 ms is the release-build target (docs/benchmarks); debug test builds get headroom.
    assert!(p50 < 150, "startup p50 {p50} ms");
}

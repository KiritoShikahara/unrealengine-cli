# Benchmarks

Measured 2026-10-14 on Windows 11, release build, UE 5.7.4 (editor started with `--null-rhi`).

| Measurement | Runs | p50 | p95 | Target |
|---|---|---|---|---|
| `unreal-cli version` (process start to exit) | 20 | 4.5 ms | 4.9 ms | 30 ms |
| `unreal-cli ping` against a running editor (process start, connect, handshake, call) | 50 | 16.2 ms | 26.8 ms | 300 ms (SC-002) |

`tests/perf.rs` checks the same paths against a mock bridge on every `cargo test` (debug build, looser limits).
| Fresh project: `setup --add-code-module --build` → `editor open --null-rhi` → `ping` (SC-001) | 1 | 70 s total | - | 10 min |

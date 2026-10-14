mod support;

use std::io::Cursor;
use std::time::Duration;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use unreal_cli::transport::client::BridgeClient;
use unreal_cli::transport::frame::{MAX_FRAME, read_frame, write_frame};

const T: Duration = Duration::from_secs(5);

#[test]
fn frame_round_trips_with_big_endian_length() {
    let mut buf = Vec::new();
    write_frame(&mut buf, &json!({"a": "日本語"})).unwrap();
    let len = u32::from_be_bytes(buf[..4].try_into().unwrap()) as usize;
    assert_eq!(len, buf.len() - 4);
    assert_eq!(
        read_frame(&mut Cursor::new(buf)).unwrap(),
        json!({"a": "日本語"})
    );
}

#[test]
fn oversized_frame_is_rejected() {
    let mut buf = ((MAX_FRAME + 1) as u32).to_be_bytes().to_vec();
    buf.extend_from_slice(b"{}");
    assert!(read_frame(&mut Cursor::new(buf)).is_err());
}

#[test]
fn handshake_returns_editor_details() {
    let m = MockBuilder::new().start();
    let c = BridgeClient::connect(m.addr, &m.token, T).unwrap();
    assert_eq!(c.welcome.engine_version, "5.7.1");
}

#[test]
fn wrong_token_is_rejected_with_connect_exit() {
    let m = MockBuilder::new().start();
    let err = BridgeClient::connect(m.addr, "nope", T).err().unwrap();
    assert_eq!(err.code, "connect.unauthorized");
    assert_eq!(err.exit.as_i32(), 3);
}

#[test]
fn protocol_mismatch_is_reported() {
    let m = MockBuilder::new().protocol(99).start();
    let err = BridgeClient::connect(m.addr, &m.token, T).err().unwrap();
    assert_eq!(err.code, "connect.protocol_mismatch");
}

#[test]
fn call_sends_tool_args_and_dry_run() {
    let m = MockBuilder::new()
        .tool("echo", |args| Ok(args.clone()))
        .start();
    let mut c = BridgeClient::connect(m.addr, &m.token, T).unwrap();
    let data = c.call("echo", json!({"x": 1}), true, 1000).unwrap();
    assert_eq!(data, json!({"x": 1}));
    let req = m.calls.lock().unwrap()[0].clone();
    assert_eq!(req["dryRun"], true);
    assert_eq!(req["timeoutMs"], 1000);
}

#[test]
fn editor_error_maps_to_editor_exit_with_hint() {
    let m = MockBuilder::new()
        .tool("fail", |_| {
            Err(json!({"code":"blueprint.compile_errors","message":"2 errors","hint":"fix"}))
        })
        .start();
    let mut c = BridgeClient::connect(m.addr, &m.token, T).unwrap();
    let err = c.call("fail", json!({}), false, 1000).unwrap_err();
    assert_eq!(err.code, "blueprint.compile_errors");
    assert_eq!(err.hint.as_deref(), Some("fix"));
    assert_eq!(err.exit.as_i32(), 4);
}

#[test]
fn non_loopback_address_is_refused() {
    let err = BridgeClient::connect("10.0.0.1:1".parse().unwrap(), "t", T)
        .err()
        .unwrap();
    assert_eq!(err.code, "connect.not_loopback");
}

#[test]
fn refused_connection_has_hint() {
    let addr = {
        let l = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        l.local_addr().unwrap()
    };
    let err = BridgeClient::connect(addr, "t", T).err().unwrap();
    assert_eq!(err.code, "connect.refused");
    assert!(err.hint.is_some());
}

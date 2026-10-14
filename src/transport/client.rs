use std::io;
use std::net::{SocketAddr, TcpStream};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;

use serde_json::{Value, json};

use super::PROTOCOL_VERSION;
use super::frame::{read_frame, write_frame};
use crate::core::error::CliError;

/// Editor details returned by the `welcome` handshake frame.
#[derive(Debug, Clone, PartialEq)]
pub struct Welcome {
    pub engine_version: String,
    pub plugin_version: String,
    pub project: String,
}

/// One authenticated connection to an UnrealCliBridge instance.
pub struct BridgeClient {
    stream: TcpStream,
    next_id: AtomicU64,
    pub welcome: Welcome,
}

fn io_error(e: io::Error) -> CliError {
    match e.kind() {
        io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut => CliError::timeout(
            "connect.timeout",
            "the editor did not answer in time",
        )
        .with_hint(
            "the editor may be busy (shader compile, modal dialog); retry or raise --timeout-ms",
        ),
        _ => CliError::connect("connect.io", e.to_string())
            .with_hint("check that the editor is still running: unrealengine-cli instances list"),
    }
}

fn remote_error(v: &Value) -> CliError {
    let code = v["code"].as_str().unwrap_or("editor.error");
    let message = v["message"]
        .as_str()
        .unwrap_or("the editor reported an error");
    let mut err = if code.ends_with(".timeout") {
        CliError::timeout(code, message)
    } else if code.starts_with("args.") || code == "tool.unknown" {
        CliError::usage(code, message)
    } else {
        CliError::editor(code, message)
    };
    if let Some(h) = v["hint"].as_str() {
        err = err.with_hint(h);
    }
    if let Some(d) = v.get("details").filter(|d| !d.is_null()) {
        err = err.with_details(d.clone());
    }
    err
}

impl BridgeClient {
    /// Connects, sends `hello` and validates the `welcome` reply.
    pub fn connect(addr: SocketAddr, token: &str, timeout: Duration) -> Result<Self, CliError> {
        if !addr.ip().is_loopback() {
            return Err(CliError::usage(
                "connect.not_loopback",
                format!("refusing to connect to non-loopback address {addr}"),
            ));
        }
        let mut stream = TcpStream::connect_timeout(&addr, timeout).map_err(|e| {
            CliError::connect(
                "connect.refused",
                format!("cannot reach the editor at {addr}: {e}"),
            )
            .with_hint("start the editor: unrealengine-cli editor open")
        })?;
        stream.set_read_timeout(Some(timeout)).map_err(io_error)?;
        stream.set_write_timeout(Some(timeout)).map_err(io_error)?;
        stream.set_nodelay(true).ok();
        let hello = json!({
            "type": "hello",
            "token": token,
            "protocol": PROTOCOL_VERSION,
            "client": concat!("unrealengine-cli/", env!("CARGO_PKG_VERSION")),
        });
        write_frame(&mut stream, &hello).map_err(io_error)?;
        let reply = read_frame(&mut stream).map_err(io_error)?;
        match reply["type"].as_str() {
            Some("welcome") => {
                let protocol = reply["protocol"].as_u64().unwrap_or(0);
                if protocol != u64::from(PROTOCOL_VERSION) {
                    return Err(CliError::connect(
                        "connect.protocol_mismatch",
                        format!("bridge speaks protocol {protocol}, CLI speaks {PROTOCOL_VERSION}"),
                    )
                    .with_hint("update the plugin: unrealengine-cli setup"));
                }
                let s = |k: &str| reply[k].as_str().unwrap_or_default().to_string();
                Ok(Self {
                    stream,
                    next_id: AtomicU64::new(1),
                    welcome: Welcome {
                        engine_version: s("engineVersion"),
                        plugin_version: s("pluginVersion"),
                        project: s("project"),
                    },
                })
            }
            Some("reject") => {
                let mut err = remote_error(&reply["error"]);
                err.exit = crate::core::exit_code::ExitCode::Connect;
                Err(err)
            }
            _ => Err(CliError::connect(
                "connect.bad_handshake",
                "unexpected handshake reply from the editor",
            )),
        }
    }

    /// Calls one tool and returns its `data` payload.
    pub fn call(
        &mut self,
        tool: &str,
        args: Value,
        dry_run: bool,
        timeout_ms: u64,
    ) -> Result<Value, CliError> {
        let id = self.next_id.fetch_add(1, Ordering::Relaxed).to_string();
        let request = json!({
            "type": "call",
            "id": id,
            "tool": tool,
            "args": args,
            "dryRun": dry_run,
            "timeoutMs": timeout_ms,
        });
        write_frame(&mut self.stream, &request).map_err(io_error)?;
        let reply = read_frame(&mut self.stream).map_err(io_error)?;
        if reply["type"] != "result" || reply["id"] != id.as_str() {
            return Err(CliError::connect(
                "connect.bad_reply",
                "reply does not match the request",
            ));
        }
        if reply["ok"].as_bool() == Some(true) {
            Ok(reply.get("data").cloned().unwrap_or(Value::Null))
        } else {
            Err(remote_error(&reply["error"]))
        }
    }
}

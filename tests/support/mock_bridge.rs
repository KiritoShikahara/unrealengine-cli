#![allow(dead_code)]
//! In-process stand-in for UnrealCliBridge speaking protocol v1.

use std::collections::HashMap;
use std::net::{SocketAddr, TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::thread;

use serde_json::{Value, json};
use unreal_cli::transport::frame::{read_frame, write_frame};

pub type Handler = Arc<dyn Fn(&Value) -> Result<Value, Value> + Send + Sync>;

pub struct MockBridge {
    pub addr: SocketAddr,
    pub token: String,
    pub calls: Arc<Mutex<Vec<Value>>>,
}

pub struct MockBuilder {
    token: String,
    protocol: u32,
    handlers: HashMap<String, Handler>,
}

impl MockBuilder {
    pub fn new() -> Self {
        Self {
            token: "test-token".into(),
            protocol: 1,
            handlers: HashMap::new(),
        }
    }

    pub fn protocol(mut self, p: u32) -> Self {
        self.protocol = p;
        self
    }

    /// A tool that answers only after `ms` milliseconds.
    pub fn slow_tool(self, name: &str, ms: u64) -> Self {
        self.tool(name, move |_| {
            std::thread::sleep(std::time::Duration::from_millis(ms));
            Ok(json!({}))
        })
    }

    pub fn tool<F>(mut self, name: &str, f: F) -> Self
    where
        F: Fn(&Value) -> Result<Value, Value> + Send + Sync + 'static,
    {
        self.handlers.insert(name.into(), Arc::new(f));
        self
    }

    pub fn start(self) -> MockBridge {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let addr = listener.local_addr().unwrap();
        let calls = Arc::new(Mutex::new(Vec::new()));
        let handlers = Arc::new(self.handlers);
        let token = self.token.clone();
        let protocol = self.protocol;
        let calls_bg = calls.clone();
        thread::spawn(move || {
            for stream in listener.incoming().flatten() {
                let handlers = handlers.clone();
                let token = token.clone();
                let calls = calls_bg.clone();
                thread::spawn(move || serve(stream, &token, protocol, &handlers, &calls));
            }
        });
        MockBridge {
            addr,
            token: self.token,
            calls,
        }
    }
}

fn serve(
    mut s: TcpStream,
    token: &str,
    protocol: u32,
    handlers: &HashMap<String, Handler>,
    calls: &Mutex<Vec<Value>>,
) {
    let Ok(hello) = read_frame(&mut s) else {
        return;
    };
    if hello["token"] != token {
        let _ = write_frame(
            &mut s,
            &json!({"type":"reject","error":{"code":"connect.unauthorized","message":"bad token"}}),
        );
        return;
    }
    let _ = write_frame(
        &mut s,
        &json!({"type":"welcome","protocol":protocol,"engineVersion":"5.7.1",
                "pluginVersion":"0.1.0","project":"C:/Proj/Mock.uproject"}),
    );
    while let Ok(req) = read_frame(&mut s) {
        calls.lock().unwrap().push(req.clone());
        let id = req["id"].clone();
        let tool = req["tool"].as_str().unwrap_or_default();
        let reply = match handlers.get(tool) {
            Some(h) => match h(&req["args"]) {
                Ok(data) => json!({"type":"result","id":id,"ok":true,"data":data}),
                Err(error) => json!({"type":"result","id":id,"ok":false,"error":error}),
            },
            None => json!({"type":"result","id":id,"ok":false,
                "error":{"code":"tool.unknown","message":format!("unknown tool {tool}")}}),
        };
        if write_frame(&mut s, &reply).is_err() {
            return;
        }
    }
}

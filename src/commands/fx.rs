use std::time::{Duration, Instant};

use clap::Subcommand;
use serde_json::{Value, json};

use super::{Ctx, parse_args_json};
use crate::core::error::CliError;

#[derive(Debug, Subcommand)]
pub enum AudioCmd {
    /// MetaSound sources
    Metasound {
        #[command(subcommand)]
        action: MetasoundCmd,
    },
}

#[derive(Debug, Subcommand)]
pub enum MetasoundCmd {
    /// New MetaSound Source; prints its graph inputs and outputs
    Create {
        path: String,
        #[arg(long)]
        stereo: bool,
        /// Keeps playing until stopped (default: one shot)
        #[arg(long)]
        looping: bool,
    },
    /// Add a node by class (UE.Sine.Audio, UE.ADSR Envelope.Audio, ...); prints its id and pins
    Node {
        path: String,
        class: String,
        #[arg(long)]
        version: Option<u32>,
    },
    /// Connect <node id>:<output> (or graph:<input>) to <node id>:<input> (or graph:<output>)
    Connect {
        path: String,
        from: String,
        to: String,
    },
    /// Set a node input's default value
    Set {
        path: String,
        node: String,
        input: String,
        value: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum PhysicsCmd {
    /// Geometry Collection (destructible) from static meshes; fracture it in the Fracture mode
    Collection {
        path: String,
        /// Static mesh (repeatable)
        #[arg(long = "mesh", required = true)]
        meshes: Vec<String>,
    },
}

#[derive(Debug, Subcommand)]
pub enum PcgCmd {
    /// New PCG graph
    Create { path: String },
    /// Add a node: pcg node <graph> CreatePointsGrid --props '{"grid_extents":[500,500,0]}'
    Node {
        path: String,
        /// Settings type: CreatePointsGrid, SurfaceSampler, StaticMeshSpawner, DensityFilter, ...
        r#type: String,
        /// Properties as JSON (snake_case keys; StaticMeshSpawner takes "mesh")
        #[arg(long)]
        props: Option<String>,
    },
    /// Connect <node>:<pin> to <node>:<pin> (input / output are the graph's own nodes)
    Connect {
        path: String,
        from: String,
        to: String,
    },
    /// Run the graph in the open level in a PCG Volume and wait until it has generated
    Generate {
        graph: String,
        #[arg(long)]
        label: Option<String>,
        /// Volume centre x,y,z
        #[arg(long, value_delimiter = ',', num_args = 3)]
        location: Option<Vec<f64>>,
        /// Half size of the volume
        #[arg(long)]
        extent: Option<f64>,
        #[arg(long, default_value_t = 60)]
        timeout: u64,
    },
}

fn literal(v: &str) -> Value {
    if let Ok(n) = v.parse::<i64>() {
        return json!(n);
    }
    if let Ok(f) = v.parse::<f64>() {
        return json!(f);
    }
    match v.to_ascii_lowercase().as_str() {
        "true" => json!(true),
        "false" => json!(false),
        _ => json!(v),
    }
}

pub fn audio(ctx: &mut Ctx, cmd: AudioCmd) -> Result<Value, CliError> {
    let AudioCmd::Metasound { action } = cmd;
    let (tool, args) = match action {
        MetasoundCmd::Create {
            path,
            stereo,
            looping,
        } => (
            "metasound_create",
            json!({ "path": path, "format": if stereo { "stereo" } else { "mono" }, "oneShot": !looping }),
        ),
        MetasoundCmd::Node {
            path,
            class,
            version,
        } => {
            let mut a = json!({ "path": path, "node": class });
            if let Some(v) = version {
                a["version"] = json!(v);
            }
            ("metasound_add_node", a)
        }
        MetasoundCmd::Connect { path, from, to } => (
            "metasound_connect",
            json!({ "path": path, "from": from, "to": to }),
        ),
        MetasoundCmd::Set {
            path,
            node,
            input,
            value,
        } => (
            "metasound_set_input",
            json!({ "path": path, "node": node, "input": input, "value": literal(&value) }),
        ),
    };
    ctx.call_with_timeout(tool, args, 120_000)
}

pub fn physics(ctx: &mut Ctx, cmd: PhysicsCmd) -> Result<Value, CliError> {
    let PhysicsCmd::Collection { path, meshes } = cmd;
    ctx.call_with_timeout(
        "geometry_collection_create",
        json!({ "path": path, "meshes": meshes }),
        300_000,
    )
}

pub fn pcg(ctx: &mut Ctx, cmd: PcgCmd) -> Result<Value, CliError> {
    match cmd {
        PcgCmd::Create { path } => ctx.call("pcg_create", json!({ "path": path })),
        PcgCmd::Node {
            path,
            r#type,
            props,
        } => {
            let mut a = json!({ "path": path, "type": r#type });
            if props.is_some() {
                a["properties"] = parse_args_json(props.as_deref())?;
            }
            ctx.call("pcg_add_node", a)
        }
        PcgCmd::Connect { path, from, to } => ctx.call(
            "pcg_connect",
            json!({ "path": path, "from": from, "to": to }),
        ),
        PcgCmd::Generate {
            graph,
            label,
            location,
            extent,
            timeout,
        } => {
            let mut a = json!({ "graph": graph });
            if let Some(l) = label {
                a["label"] = json!(l);
            }
            if let Some(l) = location {
                a["location"] = json!(l);
            }
            if let Some(e) = extent {
                a["extent"] = json!(e);
            }
            let started = ctx.call("pcg_generate", a)?;
            if ctx.global.dry_run {
                return Ok(started);
            }
            let label = started["label"].clone();
            let begin = Instant::now();
            // Generation runs over the following frames; wait until the component reports it and the count settles.
            let mut last = json!(null);
            loop {
                std::thread::sleep(Duration::from_millis(500));
                let s = ctx.call("pcg_status", json!({ "label": label }))?;
                if s["generated"] == true
                    && s["generating"] != true
                    && s["instances"] == last["instances"]
                {
                    return Ok(s);
                }
                if begin.elapsed().as_secs() > timeout {
                    return Err(CliError::timeout(
                        "pcg.timeout",
                        format!("the graph did not finish generating within {timeout}s"),
                    )
                    .with_details(s));
                }
                last = s;
            }
        }
    }
}

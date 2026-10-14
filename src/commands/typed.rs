//! Typed subcommands for the most used tool groups. Each maps 1:1 to a bridge tool.

use clap::Subcommand;
use serde_json::{Map, Value, json};

use super::Ctx;
use crate::core::error::CliError;

fn parse_json_value(raw: &str) -> Value {
    serde_json::from_str(raw).unwrap_or_else(|_| Value::String(raw.to_string()))
}

fn vec3(v: &Option<Vec<f64>>, name: &str) -> Result<Value, CliError> {
    match v {
        None => Ok(Value::Null),
        Some(v) if v.len() == 3 => Ok(json!(v)),
        Some(_) => Err(CliError::usage(
            "args.invalid",
            format!("--{name} takes x,y,z"),
        )),
    }
}

struct Args(Map<String, Value>);

impl Args {
    fn new() -> Self {
        Self(Map::new())
    }
    fn put(mut self, k: &str, v: impl Into<Value>) -> Self {
        let v = v.into();
        if !v.is_null() {
            self.0.insert(k.to_string(), v);
        }
        self
    }
    fn flag(self, k: &str, on: bool) -> Self {
        if on { self.put(k, true) } else { self }
    }
    fn done(self) -> Value {
        Value::Object(self.0)
    }
}

#[derive(Debug, Subcommand)]
pub enum LevelCmd {
    /// Create and open a new level
    New {
        path: String,
        #[arg(long)]
        template: Option<String>,
    },
    /// Open a level
    Open { path: String },
    /// Save the current level (--all: every dirty level)
    Save {
        #[arg(long)]
        all: bool,
    },
    /// Open level path and actor counts
    Info,
    /// Actor operations
    Actor {
        #[command(subcommand)]
        action: ActorCmd,
    },
}

#[derive(Debug, Subcommand)]
pub enum ActorCmd {
    List {
        #[arg(long = "class")]
        class_filter: Option<String>,
        #[arg(long = "name")]
        name_filter: Option<String>,
    },
    Find {
        label: String,
    },
    /// Spawn from a class name, Blueprint path or asset path
    Spawn {
        class: String,
        #[arg(long, value_delimiter = ',', allow_negative_numbers = true)]
        location: Option<Vec<f64>>,
        #[arg(long, value_delimiter = ',', allow_negative_numbers = true)]
        rotation: Option<Vec<f64>>,
        #[arg(long)]
        label: Option<String>,
    },
    Destroy {
        #[arg(required = true)]
        labels: Vec<String>,
    },
    Transform {
        label: String,
        #[arg(long, value_delimiter = ',', allow_negative_numbers = true)]
        location: Option<Vec<f64>>,
        #[arg(long, value_delimiter = ',', allow_negative_numbers = true)]
        rotation: Option<Vec<f64>>,
        #[arg(long, value_delimiter = ',', allow_negative_numbers = true)]
        scale: Option<Vec<f64>>,
    },
    Duplicate {
        label: String,
        #[arg(long, value_delimiter = ',', allow_negative_numbers = true)]
        offset: Option<Vec<f64>>,
        #[arg(long = "new-label")]
        new_label: Option<String>,
    },
    Attach {
        label: String,
        parent: String,
        #[arg(long)]
        socket: Option<String>,
    },
}

#[derive(Debug, Subcommand)]
pub enum AssetCmd {
    Search {
        query: Option<String>,
        #[arg(long = "class")]
        class: Option<String>,
        #[arg(long = "path", default_value = "/Game")]
        path_prefix: String,
        #[arg(long)]
        limit: Option<u32>,
    },
    List {
        folder: String,
    },
    Create {
        path: String,
        #[arg(long = "class")]
        class: String,
    },
    Duplicate {
        path: String,
        new_path: String,
    },
    Rename {
        path: String,
        new_path: String,
    },
    Delete {
        path: String,
        #[arg(long)]
        force: bool,
    },
    Import {
        destination: String,
        #[arg(required = true)]
        files: Vec<String>,
        #[arg(long)]
        replace: bool,
    },
    Save {
        path: Option<String>,
        #[arg(long)]
        folder: Option<String>,
    },
    Refs {
        path: String,
    },
    Deps {
        path: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum PropCmd {
    /// Read one property, or list all (asset path, Blueprint path, 'Label' or 'Label.Component')
    Get {
        path: String,
        property: Option<String>,
        #[arg(long)]
        filter: Option<String>,
    },
    /// Set a property; value is JSON (numbers, strings, [x,y,z], {r,g,b,a}) or plain text
    Set {
        path: String,
        property: String,
        value: String,
    },
    /// Array edit: append | insert | remove | clear
    Array {
        path: String,
        property: String,
        op: String,
        #[arg(long)]
        index: Option<i64>,
        #[arg(long)]
        value: Option<String>,
    },
    /// Components of a level actor
    Components {
        label: String,
    },
    AddComponent {
        label: String,
        class: String,
        #[arg(long)]
        name: Option<String>,
    },
    RemoveComponent {
        label: String,
        name: String,
    },
}

#[derive(Debug, Subcommand)]
pub enum PieCmd {
    /// Play for --seconds (or until a log line contains --stop-on-log), then report errors
    Run {
        #[arg(long, default_value_t = 5.0)]
        seconds: f64,
        #[arg(long = "stop-on-log")]
        stop_on_log: Option<String>,
    },
    /// Start a play session and return once the player exists (drive it with input/wait/screenshot)
    Start,
    Stop,
    Status,
    /// Send a key (W, SpaceBar, Gamepad_LeftX, ...) or an Input Action (--action <asset>) for --seconds
    Input {
        /// Key name
        key: Option<String>,
        #[arg(long)]
        action: Option<String>,
        /// Action value (JSON: true, 0.5, [1,0]) or key axis amount
        #[arg(long)]
        value: Option<String>,
        #[arg(long, default_value_t = 0.1)]
        seconds: f64,
    },
    /// Let the game run for --seconds or until a log line contains --until-log
    Wait {
        #[arg(long, default_value_t = 1.0)]
        seconds: f64,
        #[arg(long = "until-log")]
        until_log: Option<String>,
    },
    /// Capture the game viewport to a PNG
    Screenshot {
        #[arg(long)]
        path: Option<String>,
    },
    /// Actors of the play world with location and velocity
    Actors {
        #[arg(long = "class")]
        class_filter: Option<String>,
        #[arg(long = "name")]
        name_filter: Option<String>,
    },
    /// Read an actor's transform and properties (actor "player" = the player's pawn)
    Get {
        actor: String,
        properties: Vec<String>,
        #[arg(long)]
        component: Option<String>,
    },
    /// Call a function or custom event: pie call player "TakeHit 25"
    Call {
        actor: String,
        command: String,
    },
    /// Run a console command in the play world
    Exec {
        command: String,
    },
    /// On-screen UI: user widgets with their children and texts
    Widgets,
    /// Click an on-screen button by widget name
    Click {
        widget: String,
    },
}

pub fn level(ctx: &mut Ctx, cmd: LevelCmd) -> Result<Value, CliError> {
    let (tool, args) = match cmd {
        LevelCmd::New { path, template } => (
            "level_new",
            Args::new()
                .put("path", path)
                .put("template", template)
                .done(),
        ),
        LevelCmd::Open { path } => ("level_open", Args::new().put("path", path).done()),
        LevelCmd::Save { all } => ("level_save", Args::new().flag("all", all).done()),
        LevelCmd::Info => ("level_info", json!({})),
        LevelCmd::Actor { action } => match action {
            ActorCmd::List {
                class_filter,
                name_filter,
            } => (
                "actor_list",
                Args::new()
                    .put("classFilter", class_filter)
                    .put("nameFilter", name_filter)
                    .done(),
            ),
            ActorCmd::Find { label } => ("actor_find", Args::new().put("label", label).done()),
            ActorCmd::Spawn {
                class,
                location,
                rotation,
                label,
            } => (
                "actor_spawn",
                Args::new()
                    .put("actorClass", class)
                    .put("location", vec3(&location, "location")?)
                    .put("rotation", vec3(&rotation, "rotation")?)
                    .put("label", label)
                    .done(),
            ),
            ActorCmd::Destroy { labels } => (
                "actor_destroy",
                Args::new().put("labels", json!(labels)).done(),
            ),
            ActorCmd::Transform {
                label,
                location,
                rotation,
                scale,
            } => (
                "actor_transform",
                Args::new()
                    .put("label", label)
                    .put("location", vec3(&location, "location")?)
                    .put("rotation", vec3(&rotation, "rotation")?)
                    .put("scale", vec3(&scale, "scale")?)
                    .done(),
            ),
            ActorCmd::Duplicate {
                label,
                offset,
                new_label,
            } => (
                "actor_duplicate",
                Args::new()
                    .put("label", label)
                    .put("offset", vec3(&offset, "offset")?)
                    .put("newLabel", new_label)
                    .done(),
            ),
            ActorCmd::Attach {
                label,
                parent,
                socket,
            } => (
                "actor_attach",
                Args::new()
                    .put("label", label)
                    .put("parent", parent)
                    .put("socket", socket)
                    .done(),
            ),
        },
    };
    ctx.call(tool, args)
}

pub fn asset(ctx: &mut Ctx, cmd: AssetCmd) -> Result<Value, CliError> {
    let (tool, args) = match cmd {
        AssetCmd::Search {
            query,
            class,
            path_prefix,
            limit,
        } => (
            "asset_search",
            Args::new()
                .put("query", query)
                .put("class", class)
                .put("pathPrefix", path_prefix)
                .put("limit", limit)
                .done(),
        ),
        AssetCmd::List { folder } => ("asset_list", Args::new().put("folder", folder).done()),
        AssetCmd::Create { path, class } => (
            "asset_create",
            Args::new().put("path", path).put("class", class).done(),
        ),
        AssetCmd::Duplicate { path, new_path } => (
            "asset_duplicate",
            Args::new()
                .put("path", path)
                .put("newPath", new_path)
                .done(),
        ),
        AssetCmd::Rename { path, new_path } => (
            "asset_rename",
            Args::new()
                .put("path", path)
                .put("newPath", new_path)
                .done(),
        ),
        AssetCmd::Delete { path, force } => (
            "asset_delete",
            Args::new().put("path", path).flag("force", force).done(),
        ),
        AssetCmd::Import {
            destination,
            files,
            replace,
        } => {
            let files: Vec<String> = files
                .iter()
                .map(|f| {
                    std::fs::canonicalize(f)
                        .map(|p| {
                            crate::core::paths::normalize(&p)
                                .to_string_lossy()
                                .to_string()
                        })
                        .unwrap_or_else(|_| f.clone())
                })
                .collect();
            (
                "asset_import",
                Args::new()
                    .put("destination", destination)
                    .put("files", json!(files))
                    .flag("replace", replace)
                    .done(),
            )
        }
        AssetCmd::Save { path, folder } => (
            "asset_save",
            Args::new().put("path", path).put("folder", folder).done(),
        ),
        AssetCmd::Refs { path } => ("asset_references", Args::new().put("path", path).done()),
        AssetCmd::Deps { path } => ("asset_dependencies", Args::new().put("path", path).done()),
    };
    ctx.call(tool, args)
}

pub fn prop(ctx: &mut Ctx, cmd: PropCmd) -> Result<Value, CliError> {
    let (tool, args) = match cmd {
        PropCmd::Get {
            path,
            property,
            filter,
        } => (
            "prop_get",
            Args::new()
                .put("path", path)
                .put("property", property)
                .put("filter", filter)
                .done(),
        ),
        PropCmd::Set {
            path,
            property,
            value,
        } => (
            "prop_set",
            Args::new()
                .put("path", path)
                .put("property", property)
                .put("value", parse_json_value(&value))
                .done(),
        ),
        PropCmd::Array {
            path,
            property,
            op,
            index,
            value,
        } => (
            "prop_array_edit",
            Args::new()
                .put("path", path)
                .put("property", property)
                .put("op", op)
                .put("index", index)
                .put("value", value.as_deref().map(parse_json_value))
                .done(),
        ),
        PropCmd::Components { label } => ("component_list", Args::new().put("label", label).done()),
        PropCmd::AddComponent { label, class, name } => (
            "component_add",
            Args::new()
                .put("label", label)
                .put("class", class)
                .put("name", name)
                .done(),
        ),
        PropCmd::RemoveComponent { label, name } => (
            "component_remove",
            Args::new().put("label", label).put("name", name).done(),
        ),
    };
    ctx.call(tool, args)
}

pub fn pie(ctx: &mut Ctx, cmd: PieCmd) -> Result<Value, CliError> {
    match cmd {
        PieCmd::Run {
            seconds,
            stop_on_log,
        } => {
            let args = Args::new()
                .put("durationSeconds", seconds)
                .put("stopOnLog", stop_on_log)
                .done();
            ctx.call_with_timeout("pie_run", args, ((seconds + 90.0) * 1000.0) as u64)
        }
        PieCmd::Start => ctx.call_with_timeout("pie_start", json!({}), 90_000),
        PieCmd::Stop => ctx.call("pie_stop", json!({})),
        PieCmd::Status => ctx.call("pie_status", json!({})),
        PieCmd::Input {
            key,
            action,
            value,
            seconds,
        } => {
            let value = match value {
                Some(v) => Some(serde_json::from_str::<Value>(&v).map_err(|e| {
                    CliError::usage("args.value", format!("--value is not JSON: {e}"))
                })?),
                None => None,
            };
            let args = Args::new()
                .put("key", key)
                .put("action", action)
                .put("value", value)
                .put("seconds", seconds)
                .done();
            ctx.call_with_timeout("pie_input", args, ((seconds + 30.0) * 1000.0) as u64)
        }
        PieCmd::Wait { seconds, until_log } => {
            let args = Args::new()
                .put("seconds", seconds)
                .put("untilLog", until_log)
                .done();
            ctx.call_with_timeout("pie_wait", args, ((seconds + 30.0) * 1000.0) as u64)
        }
        PieCmd::Screenshot { path } => {
            ctx.call("pie_screenshot", Args::new().put("path", path).done())
        }
        PieCmd::Actors {
            class_filter,
            name_filter,
        } => ctx.call(
            "pie_actors",
            Args::new()
                .put("classFilter", class_filter)
                .put("nameFilter", name_filter)
                .done(),
        ),
        PieCmd::Get {
            actor,
            properties,
            component,
        } => ctx.call(
            "pie_get",
            Args::new()
                .put("actor", actor)
                .put("properties", properties)
                .put("component", component)
                .done(),
        ),
        PieCmd::Call { actor, command } => ctx.call(
            "pie_call",
            Args::new()
                .put("actor", actor)
                .put("command", command)
                .done(),
        ),
        PieCmd::Exec { command } => {
            ctx.call("pie_exec", Args::new().put("command", command).done())
        }
        PieCmd::Widgets => ctx.call("pie_widgets", json!({})),
        PieCmd::Click { widget } => ctx.call("pie_click", Args::new().put("widget", widget).done()),
    }
}

#[derive(Debug, Subcommand)]
pub enum FabCmd {
    /// Whether the editor's Fab page is signed in (opens the Fab tab)
    Status,
    /// Search Fab listings
    Search {
        query: String,
        /// Only free listings
        #[arg(long)]
        free: bool,
        #[arg(long)]
        count: Option<u32>,
    },
    /// Your Fab library (owned listings), optionally filtered
    Library {
        query: Option<String>,
        #[arg(long)]
        count: Option<u32>,
    },
    /// Download a listing from your library and add it to the project (waits for the import)
    Add {
        /// Listing uid or fab.com listing URL
        listing: String,
        /// unreal-engine | gltf | glb | fbx (default: the first one offered, in that order)
        #[arg(long)]
        format: Option<String>,
        /// Megascans quality: low | medium | high | raw
        #[arg(long)]
        quality: Option<String>,
        /// Add a free listing to your library first (accepts its license; paid listings are never bought)
        #[arg(long = "acquire-free")]
        acquire_free: bool,
        /// Seconds to wait for the download and import
        #[arg(long, default_value_t = 1800)]
        timeout: u64,
    },
}

pub fn fab(ctx: &mut Ctx, cmd: FabCmd) -> Result<Value, CliError> {
    match cmd {
        FabCmd::Status => ctx.call_with_timeout("fab_status", json!({}), 90_000),
        FabCmd::Search { query, free, count } => {
            let args = Args::new()
                .put("query", query)
                .flag("freeOnly", free)
                .put("count", count)
                .done();
            ctx.call_with_timeout("fab_search", args, 90_000)
        }
        FabCmd::Library { query, count } => {
            let args = Args::new().put("query", query).put("count", count).done();
            ctx.call_with_timeout("fab_library", args, 150_000)
        }
        FabCmd::Add {
            listing,
            format,
            quality,
            acquire_free,
            timeout,
        } => {
            let args = Args::new()
                .put("listing", listing)
                .put("format", format)
                .put("quality", quality)
                .flag("acquireFree", acquire_free)
                .put("timeoutSeconds", timeout)
                .done();
            // The job's deadline is the request timeout; leave room for the page lookup.
            let started = ctx.call_with_timeout("fab_add", args, (timeout + 180) * 1000)?;
            let job = started["jobId"].as_str().unwrap_or_default().to_string();
            let begin = std::time::Instant::now();
            loop {
                let j = ctx.call("jobs_get", json!({ "jobId": job }))?;
                match j["state"].as_str().unwrap_or_default() {
                    "succeeded" => return Ok(j["result"].clone()),
                    "failed" => {
                        let e = &j["error"];
                        return Err(CliError::editor(
                            e["code"].as_str().unwrap_or("fab.import_failed"),
                            e["message"].as_str().unwrap_or("the Fab import failed"),
                        )
                        .with_details(e["details"].clone()));
                    }
                    "timeout" | "cancelled" => {
                        return Err(CliError::timeout(
                            "fab.timeout",
                            format!("job {job} ended: {}", j["state"]),
                        ));
                    }
                    _ if begin.elapsed().as_secs() > timeout + 180 => {
                        let _ = ctx.call("jobs_cancel", json!({ "jobId": job }));
                        return Err(CliError::timeout(
                            "fab.timeout",
                            "the Fab import did not finish in time",
                        ));
                    }
                    _ => std::thread::sleep(std::time::Duration::from_millis(500)),
                }
            }
        }
    }
}

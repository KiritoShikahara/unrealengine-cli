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
    Stop,
    Status,
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
        PieCmd::Stop => ctx.call("pie_stop", json!({})),
        PieCmd::Status => ctx.call("pie_status", json!({})),
    }
}

use clap::Subcommand;
use serde_json::{Map, Value, json};

use super::Ctx;
use crate::core::error::CliError;

#[derive(Debug, Subcommand)]
pub enum BlueprintCmd {
    /// Create a Blueprint asset
    Create {
        path: String,
        #[arg(long, default_value = "Actor")]
        parent: String,
    },
    /// Show variables, graphs and components
    Get { path: String },
    /// Show one graph's nodes, pins and links
    Graph {
        path: String,
        #[arg(long, default_value = "EventGraph")]
        graph: String,
    },
    /// Add a node (Event, CustomEvent, CallFunction, VariableGet, VariableSet, Branch, Sequence, Self, Cast, Macro)
    AddNode {
        path: String,
        #[arg(long = "type")]
        node_type: String,
        #[arg(long)]
        member: Option<String>,
        #[arg(long = "member-parent")]
        member_parent: Option<String>,
        #[arg(long, default_value = "EventGraph")]
        graph: String,
        /// x,y
        #[arg(long, value_delimiter = ',')]
        position: Option<Vec<f64>>,
    },
    /// Remove a node (N<k> or guid)
    RemoveNode {
        path: String,
        node: String,
        #[arg(long, default_value = "EventGraph")]
        graph: String,
    },
    /// Connect <node>.<pin> to <node>.<pin>
    Connect {
        path: String,
        from: String,
        to: String,
        #[arg(long, default_value = "EventGraph")]
        graph: String,
    },
    /// Break a link (or all links of a pin when 'to' is omitted)
    Disconnect {
        path: String,
        from: String,
        to: Option<String>,
        #[arg(long, default_value = "EventGraph")]
        graph: String,
    },
    /// Set an input pin's default value
    SetPin {
        path: String,
        node: String,
        pin: String,
        value: String,
        #[arg(long, default_value = "EventGraph")]
        graph: String,
    },
    /// Add a member variable
    AddVariable {
        path: String,
        name: String,
        #[arg(long = "type")]
        var_type: String,
        #[arg(long)]
        default: Option<String>,
        #[arg(long)]
        instance_editable: bool,
        #[arg(long)]
        category: Option<String>,
    },
    /// Remove a member variable
    RemoveVariable { path: String, name: String },
    /// Add a function graph; --param/--returns take name:type
    AddFunction {
        path: String,
        name: String,
        #[arg(long = "param")]
        inputs: Vec<String>,
        #[arg(long = "returns")]
        outputs: Vec<String>,
        #[arg(long)]
        pure: bool,
    },
    /// Add a component
    AddComponent {
        path: String,
        class: String,
        #[arg(long)]
        name: Option<String>,
        #[arg(long)]
        parent: Option<String>,
    },
    /// Compile and report errors
    Compile { path: String },
    /// Save (refused on compile errors unless --force)
    Save {
        path: String,
        #[arg(long)]
        force: bool,
    },
    /// Find functions / events / macros for add-node
    SearchNodes {
        query: Option<String>,
        #[arg(long = "for-class")]
        for_class: Option<String>,
        #[arg(long)]
        kind: Option<String>,
        #[arg(long)]
        limit: Option<u32>,
    },
    /// Change the parent class
    Reparent { path: String, parent: String },
    /// Implement a Blueprint interface
    ImplementInterface { path: String, interface: String },
}

fn params(pairs: &[String]) -> Result<Vec<Value>, CliError> {
    pairs
        .iter()
        .map(|p| {
            let (n, t) = p.split_once(':').ok_or_else(|| {
                CliError::usage("args.invalid", format!("'{p}' must be name:type"))
            })?;
            Ok(json!({ "name": n, "type": t }))
        })
        .collect()
}

/// Maps a subcommand to (tool, args).
pub fn to_call(cmd: BlueprintCmd) -> Result<(&'static str, Value), CliError> {
    let mut a = Map::new();
    let mut put = |k: &str, v: Value| {
        if !v.is_null() {
            a.insert(k.to_string(), v);
        }
    };
    let tool = match cmd {
        BlueprintCmd::Create { path, parent } => {
            put("path", json!(path));
            put("parent", json!(parent));
            "bp_create"
        }
        BlueprintCmd::Get { path } => {
            put("path", json!(path));
            "bp_get"
        }
        BlueprintCmd::Graph { path, graph } => {
            put("path", json!(path));
            put("graph", json!(graph));
            "bp_graph_get"
        }
        BlueprintCmd::AddNode {
            path,
            node_type,
            member,
            member_parent,
            graph,
            position,
        } => {
            put("path", json!(path));
            put("type", json!(node_type));
            put("memberName", json!(member));
            put("memberParent", json!(member_parent));
            put("graph", json!(graph));
            if let Some(p) = &position
                && p.len() != 2
            {
                return Err(CliError::usage("args.invalid", "--position takes x,y"));
            }
            put("position", json!(position));
            "bp_add_node"
        }
        BlueprintCmd::RemoveNode { path, node, graph } => {
            put("path", json!(path));
            put("node", json!(node));
            put("graph", json!(graph));
            "bp_remove_node"
        }
        BlueprintCmd::Connect {
            path,
            from,
            to,
            graph,
        } => {
            put("path", json!(path));
            put("from", json!(from));
            put("to", json!(to));
            put("graph", json!(graph));
            "bp_connect"
        }
        BlueprintCmd::Disconnect {
            path,
            from,
            to,
            graph,
        } => {
            put("path", json!(path));
            put("from", json!(from));
            put("to", json!(to));
            put("graph", json!(graph));
            "bp_disconnect"
        }
        BlueprintCmd::SetPin {
            path,
            node,
            pin,
            value,
            graph,
        } => {
            put("path", json!(path));
            put("node", json!(node));
            put("pin", json!(pin));
            put("value", json!(value));
            put("graph", json!(graph));
            "bp_set_pin"
        }
        BlueprintCmd::AddVariable {
            path,
            name,
            var_type,
            default,
            instance_editable,
            category,
        } => {
            put("path", json!(path));
            put("name", json!(name));
            put("type", json!(var_type));
            put("defaultValue", json!(default));
            if instance_editable {
                put("instanceEditable", json!(true));
            }
            put("category", json!(category));
            "bp_add_variable"
        }
        BlueprintCmd::RemoveVariable { path, name } => {
            put("path", json!(path));
            put("name", json!(name));
            "bp_remove_variable"
        }
        BlueprintCmd::AddFunction {
            path,
            name,
            inputs,
            outputs,
            pure,
        } => {
            put("path", json!(path));
            put("name", json!(name));
            put("inputs", json!(params(&inputs)?));
            put("outputs", json!(params(&outputs)?));
            if pure {
                put("pure", json!(true));
            }
            "bp_add_function"
        }
        BlueprintCmd::AddComponent {
            path,
            class,
            name,
            parent,
        } => {
            put("path", json!(path));
            put("class", json!(class));
            put("name", json!(name));
            put("parent", json!(parent));
            "bp_add_component"
        }
        BlueprintCmd::Compile { path } => {
            put("path", json!(path));
            "bp_compile"
        }
        BlueprintCmd::Save { path, force } => {
            put("path", json!(path));
            if force {
                put("force", json!(true));
            }
            "bp_save"
        }
        BlueprintCmd::SearchNodes {
            query,
            for_class,
            kind,
            limit,
        } => {
            put("query", json!(query));
            put("forClass", json!(for_class));
            put("kind", json!(kind));
            put("limit", json!(limit));
            "bp_search_nodes"
        }
        BlueprintCmd::Reparent { path, parent } => {
            put("path", json!(path));
            put("parent", json!(parent));
            "bp_reparent"
        }
        BlueprintCmd::ImplementInterface { path, interface } => {
            put("path", json!(path));
            put("interface", json!(interface));
            "bp_implement_interface"
        }
    };
    Ok((tool, Value::Object(a)))
}

pub fn run(ctx: &mut Ctx, cmd: BlueprintCmd) -> Result<Value, CliError> {
    let (tool, args) = to_call(cmd)?;
    ctx.call(tool, args)
}

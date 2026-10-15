use std::path::{Path, PathBuf};
use std::sync::LazyLock;

use clap::Subcommand;
use regex::Regex;
use serde_json::{Map, Value, json};

use super::Ctx;
use super::code::default_module;
use crate::core::error::CliError;
use crate::setup;

const MODULES: [&str; 3] = ["GameplayAbilities", "GameplayTags", "GameplayTasks"];

#[derive(Debug, Subcommand)]
pub enum GasCmd {
    /// Enable the GameplayAbilities plugin and add the GAS modules to a C++ module's dependencies
    Enable {
        #[arg(long)]
        module: Option<String>,
    },
    /// Generate a C++ AttributeSet (accessors, replication with OnRep, defaults); then `code apply`
    AttributeSet {
        /// Class name without the U prefix
        name: String,
        /// Attribute, optionally with its default: Health=100 (repeatable)
        #[arg(long = "attr", required = true)]
        attrs: Vec<String>,
        #[arg(long)]
        module: Option<String>,
        /// No replication (single-player)
        #[arg(long)]
        no_replicate: bool,
    },
    /// Create a Gameplay Effect Blueprint
    Effect {
        path: String,
        /// instant (default), infinite, or seconds
        #[arg(long)]
        duration: Option<String>,
        /// Seconds between periodic executions
        #[arg(long)]
        period: Option<f64>,
        /// Set.Attribute:op:magnitude with op add|multiply|divide|override (repeatable)
        #[arg(long = "modifier")]
        modifiers: Vec<String>,
        #[arg(long, value_parser = ["none", "source", "target"])]
        stacking: Option<String>,
        #[arg(long)]
        stack_limit: Option<u32>,
        /// Tag the target has while the effect is active (repeatable)
        #[arg(long = "grant-tag")]
        grant_tags: Vec<String>,
    },
    /// Create a Gameplay Ability Blueprint (with --effect: commit, apply it to the owner, end)
    Ability {
        path: String,
        #[arg(long)]
        effect: Option<String>,
        #[arg(long)]
        cost: Option<String>,
        #[arg(long)]
        cooldown: Option<String>,
        /// Ability tag (repeatable)
        #[arg(long = "tag")]
        tags: Vec<String>,
    },
    /// Create a Gameplay Cue notify Blueprint for a GameplayCue.* tag
    Cue {
        path: String,
        #[arg(long)]
        tag: String,
        /// Actor notify (stays while active) instead of a static one
        #[arg(long)]
        actor: bool,
    },
    /// Give a Blueprint actor an AbilitySystemComponent that creates these AttributeSets at start
    Component {
        path: String,
        /// AttributeSet class name (repeatable)
        #[arg(long = "attribute-set")]
        attribute_sets: Vec<String>,
    },
    /// Give an ability to a play-world actor's AbilitySystemComponent
    Give {
        actor: String,
        ability: String,
        #[arg(long)]
        level: Option<f64>,
        #[arg(long)]
        world: Option<String>,
    },
    /// Try to activate a given ability
    Activate {
        actor: String,
        ability: String,
        #[arg(long)]
        world: Option<String>,
    },
    /// Apply a Gameplay Effect to the actor
    Apply {
        actor: String,
        effect: String,
        #[arg(long)]
        level: Option<f64>,
        #[arg(long)]
        world: Option<String>,
    },
    /// Attributes, tags, abilities and active effects of the actor
    State {
        actor: String,
        #[arg(long)]
        world: Option<String>,
    },
}

#[derive(Debug, Subcommand)]
pub enum TagsCmd {
    List {
        #[arg(long)]
        filter: Option<String>,
    },
    /// Add to Config/DefaultGameplayTags.ini
    Add {
        tag: String,
        #[arg(long)]
        comment: Option<String>,
    },
    /// Rename (keeps references working with a redirect)
    Rename {
        from: String,
        to: String,
    },
    Remove {
        tag: String,
    },
}

fn put(m: &mut Map<String, Value>, k: &str, v: Option<Value>) {
    if let Some(v) = v {
        m.insert(k.into(), v);
    }
}

fn list(v: Vec<String>) -> Option<Value> {
    (!v.is_empty()).then(|| json!(v))
}

static MODIFIER: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"^(\w+\.\w+):(add|multiply|divide|override):(-?\d+(?:\.\d+)?)$")
        .expect("valid regex")
});

fn modifier(spec: &str) -> Result<Value, CliError> {
    let c = MODIFIER.captures(spec.trim()).ok_or_else(|| {
        CliError::usage(
            "gas.bad_modifier",
            format!("'{spec}' is not Set.Attribute:op:magnitude"),
        )
        .with_hint(
            "e.g. --modifier MyAttributeSet.Health:add:-10 (op: add, multiply, divide, override)",
        )
    })?;
    let magnitude: f64 = c[3].parse().unwrap_or_default();
    Ok(json!({ "attribute": &c[1], "op": &c[2], "magnitude": magnitude }))
}

pub fn run(ctx: &mut Ctx, cmd: GasCmd) -> Result<Value, CliError> {
    let mut a = Map::new();
    let (tool, wait) = match cmd {
        GasCmd::Enable { module } => return enable(ctx, module),
        GasCmd::AttributeSet {
            name,
            attrs,
            module,
            no_replicate,
        } => return attribute_set(ctx, &name, &attrs, module, !no_replicate),
        GasCmd::Effect {
            path,
            duration,
            period,
            modifiers,
            stacking,
            stack_limit,
            grant_tags,
        } => {
            let mods = modifiers
                .iter()
                .map(|m| modifier(m))
                .collect::<Result<Vec<_>, _>>()?;
            a.insert("path".into(), json!(path));
            put(&mut a, "duration", duration.map(Value::from));
            put(&mut a, "period", period.map(Value::from));
            put(&mut a, "modifiers", (!mods.is_empty()).then(|| json!(mods)));
            put(&mut a, "stacking", stacking.map(Value::from));
            put(&mut a, "stackLimit", stack_limit.map(Value::from));
            put(&mut a, "grantedTags", list(grant_tags));
            ("gas_effect_create", 60_000)
        }
        GasCmd::Ability {
            path,
            effect,
            cost,
            cooldown,
            tags,
        } => {
            a.insert("path".into(), json!(path));
            put(&mut a, "effect", effect.map(Value::from));
            put(&mut a, "cost", cost.map(Value::from));
            put(&mut a, "cooldown", cooldown.map(Value::from));
            put(&mut a, "tags", list(tags));
            ("gas_ability_create", 60_000)
        }
        GasCmd::Cue { path, tag, actor } => {
            a.insert("path".into(), json!(path));
            a.insert("tag".into(), json!(tag));
            put(&mut a, "kind", actor.then(|| json!("actor")));
            ("gas_cue_create", 60_000)
        }
        GasCmd::Component {
            path,
            attribute_sets,
        } => {
            a.insert("path".into(), json!(path));
            put(&mut a, "attributeSets", list(attribute_sets));
            ("gas_component_add", 60_000)
        }
        GasCmd::Give {
            actor,
            ability,
            level,
            world,
        } => {
            runtime(&mut a, "give", actor, world);
            a.insert("ability".into(), json!(ability));
            put(&mut a, "level", level.map(Value::from));
            ("gas_runtime", 0)
        }
        GasCmd::Activate {
            actor,
            ability,
            world,
        } => {
            runtime(&mut a, "activate", actor, world);
            a.insert("ability".into(), json!(ability));
            ("gas_runtime", 0)
        }
        GasCmd::Apply {
            actor,
            effect,
            level,
            world,
        } => {
            runtime(&mut a, "apply", actor, world);
            a.insert("effect".into(), json!(effect));
            put(&mut a, "level", level.map(Value::from));
            ("gas_runtime", 0)
        }
        GasCmd::State { actor, world } => {
            runtime(&mut a, "state", actor, world);
            ("gas_runtime", 0)
        }
    };
    if wait > 0 {
        ctx.call_with_timeout(tool, Value::Object(a), wait)
    } else {
        ctx.call(tool, Value::Object(a))
    }
}

fn runtime(a: &mut Map<String, Value>, action: &str, actor: String, world: Option<String>) {
    a.insert("action".into(), json!(action));
    a.insert("actor".into(), json!(actor));
    put(a, "world", world.map(Value::from));
}

pub fn tags(ctx: &mut Ctx, cmd: TagsCmd) -> Result<Value, CliError> {
    let mut a = Map::new();
    let tool = match cmd {
        TagsCmd::List { filter } => {
            put(&mut a, "filter", filter.map(Value::from));
            "tags_list"
        }
        TagsCmd::Add { tag, comment } => {
            a.insert("tag".into(), json!(tag));
            put(&mut a, "comment", comment.map(Value::from));
            "tags_add"
        }
        TagsCmd::Rename { from, to } => {
            a.insert("from".into(), json!(from));
            a.insert("to".into(), json!(to));
            "tags_rename"
        }
        TagsCmd::Remove { tag } => {
            a.insert("tag".into(), json!(tag));
            "tags_remove"
        }
    };
    ctx.call(tool, Value::Object(a))
}

static DEPS: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"PublicDependencyModuleNames\.AddRange\(\s*new\s+string\[\]\s*\{([^}]*)\}")
        .expect("valid regex")
});
static CTOR_BODY: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r":\s*base\(Target\)\s*\n([ \t]*)\{").expect("valid regex"));

/// Adds the GAS modules to a Build.cs; returns the new text and the added module names.
pub fn add_gas_modules(build_cs: &str) -> Result<(String, Vec<&'static str>), CliError> {
    if let Some(c) = DEPS.captures(build_cs) {
        let list = c.get(1).expect("group 1");
        let missing: Vec<&str> = MODULES
            .iter()
            .copied()
            .filter(|m| !list.as_str().contains(&format!("\"{m}\"")))
            .collect();
        if missing.is_empty() {
            return Ok((build_cs.to_string(), missing));
        }
        let existing = list.as_str().trim().trim_end_matches(',').trim();
        let added: Vec<String> = missing.iter().map(|m| format!("\"{m}\"")).collect();
        let joined = if existing.is_empty() {
            added.join(", ")
        } else {
            format!("{existing}, {}", added.join(", "))
        };
        let mut out = build_cs.to_string();
        out.replace_range(list.range(), &format!(" {joined} "));
        return Ok((out, missing));
    }
    let c = CTOR_BODY.captures(build_cs).ok_or_else(|| {
        CliError::usage(
            "gas.build_cs",
            "cannot find the module constructor in the .Build.cs",
        )
        .with_hint("add \"GameplayAbilities\", \"GameplayTags\", \"GameplayTasks\" to PublicDependencyModuleNames by hand")
    })?;
    let whole = c.get(0).expect("match");
    let indent = &c[1];
    let inner = if indent.contains('\t') {
        format!("{indent}\t")
    } else {
        format!("{indent}    ")
    };
    let line = format!(
        "\n{inner}PublicDependencyModuleNames.AddRange(new string[] {{ {} }});",
        MODULES.map(|m| format!("\"{m}\"")).join(", ")
    );
    let mut out = build_cs.to_string();
    out.insert_str(whole.end(), &line);
    Ok((out, MODULES.to_vec()))
}

fn enable(ctx: &mut Ctx, module: Option<String>) -> Result<Value, CliError> {
    let up = ctx.uproject()?;
    let dir = up.parent().unwrap_or(Path::new(".")).to_path_buf();
    let module = match module {
        Some(m) => m,
        None => default_module(&dir)?,
    };
    let build_cs = dir.join(format!("Source/{module}/{module}.Build.cs"));
    let cs_text = std::fs::read_to_string(&build_cs).map_err(|_| {
        CliError::usage(
            "code.no_module",
            format!("{} not found", build_cs.display()),
        )
    })?;
    let (new_cs, added) = add_gas_modules(&cs_text)?;
    let up_text = std::fs::read_to_string(&up)
        .map_err(|e| CliError::usage("project.invalid", format!("{}: {e}", up.display())))?;
    let new_up = setup::enable_plugins_in_uproject(&up_text, &["GameplayAbilities"])?;
    let mut changed = Vec::new();
    if new_up.is_some() {
        changed.push(json!(up));
    }
    if !added.is_empty() {
        changed.push(json!(build_cs));
    }
    let data = json!({
        "plugin": "GameplayAbilities",
        "module": module,
        "modules": MODULES,
        "added": added,
        "changed": changed,
        "dryRun": ctx.global.dry_run,
        "next": if changed.is_empty() { "already enabled" } else { "rebuild and restart: unrealengine-cli code apply" },
    });
    if !ctx.global.dry_run {
        let write = |p: &Path, t: &str| {
            std::fs::write(p, t)
                .map_err(|e| CliError::internal("io.write", format!("{}: {e}", p.display())))
        };
        if let Some(t) = new_up {
            write(&up, &t)?;
        }
        if !added.is_empty() {
            write(&build_cs, &new_cs)?;
        }
    }
    Ok(data)
}

fn ident(s: &str) -> bool {
    s.chars().next().is_some_and(|c| c.is_ascii_alphabetic())
        && s.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
}

/// (header, source) of a UAttributeSet subclass.
pub fn attribute_set_files(
    name: &str,
    api: &str,
    attrs: &[(String, String)],
    replicate: bool,
) -> (String, String) {
    let class = format!("U{name}");
    let mut h = format!(
        "#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"AttributeSet.h\"\n#include \"AbilitySystemComponent.h\"\n#include \"{name}.generated.h\"\n\nUCLASS()\nclass {api} {class} : public UAttributeSet\n{{\n\tGENERATED_BODY()\n\npublic:\n\t{class}();\n"
    );
    if replicate {
        h.push_str("\n\tvirtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;\n");
    }
    for (a, _) in attrs {
        let rep = if replicate {
            format!(", ReplicatedUsing = OnRep_{a}")
        } else {
            String::new()
        };
        h.push_str(&format!(
            "\n\tUPROPERTY(BlueprintReadOnly, Category = \"Attributes\"{rep})\n\tFGameplayAttributeData {a};\n\tGAMEPLAYATTRIBUTE_PROPERTY_GETTER({class}, {a})\n\tGAMEPLAYATTRIBUTE_VALUE_GETTER({a})\n\tGAMEPLAYATTRIBUTE_VALUE_SETTER({a})\n\tGAMEPLAYATTRIBUTE_VALUE_INITTER({a})\n"
        ));
    }
    if replicate {
        h.push_str("\nprotected:\n");
        for (a, _) in attrs {
            h.push_str(&format!(
                "\tUFUNCTION()\n\tvoid OnRep_{a}(const FGameplayAttributeData& Old{a});\n"
            ));
        }
    }
    h.push_str("};\n");

    let mut c = format!("#include \"{name}.h\"\n");
    if replicate {
        c.push_str("#include \"Net/UnrealNetwork.h\"\n");
    }
    c.push_str(&format!("\n{class}::{class}()\n{{\n"));
    for (a, d) in attrs {
        c.push_str(&format!("\tInit{a}({d}f);\n"));
    }
    c.push_str("}\n");
    if replicate {
        c.push_str(&format!("\nvoid {class}::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const\n{{\n\tSuper::GetLifetimeReplicatedProps(OutLifetimeProps);\n"));
        for (a, _) in attrs {
            c.push_str(&format!(
                "\tDOREPLIFETIME_CONDITION_NOTIFY({class}, {a}, COND_None, REPNOTIFY_Always);\n"
            ));
        }
        c.push_str("}\n");
        for (a, _) in attrs {
            c.push_str(&format!("\nvoid {class}::OnRep_{a}(const FGameplayAttributeData& Old{a})\n{{\n\tGAMEPLAYATTRIBUTE_REPNOTIFY({class}, {a}, Old{a});\n}}\n"));
        }
    }
    (h, c)
}

fn attribute_set(
    ctx: &mut Ctx,
    name: &str,
    specs: &[String],
    module: Option<String>,
    replicate: bool,
) -> Result<Value, CliError> {
    if !ident(name) {
        return Err(CliError::usage(
            "args.invalid",
            format!("'{name}' is not a valid class name"),
        ));
    }
    let mut attrs = Vec::new();
    for s in specs {
        let (a, d) = s.split_once('=').unwrap_or((s, "0"));
        let (a, d) = (a.trim(), d.trim());
        if !ident(a) || d.parse::<f64>().is_err() {
            return Err(CliError::usage(
                "args.invalid",
                format!("'{s}' is not Name or Name=number"),
            )
            .with_hint("e.g. --attr Health=100 --attr Mana"));
        }
        let d = if d.contains('.') {
            d.to_string()
        } else {
            format!("{d}.")
        };
        attrs.push((a.to_string(), d));
    }
    let up = ctx.uproject()?;
    let dir = up.parent().unwrap_or(Path::new(".")).to_path_buf();
    let module = match module {
        Some(m) => m,
        None => default_module(&dir)?,
    };
    let header: PathBuf = dir.join(format!("Source/{module}/{name}.h"));
    let source: PathBuf = dir.join(format!("Source/{module}/{name}.cpp"));
    if header.exists() || source.exists() {
        return Err(CliError::usage(
            "code.exists",
            format!("{name}.h or {name}.cpp already exists"),
        ));
    }
    let (h, c) = attribute_set_files(
        name,
        &format!("{}_API", module.to_uppercase()),
        &attrs,
        replicate,
    );
    let mut data = json!({
        "class": format!("U{name}"),
        "header": header,
        "source": source,
        "attributes": attrs.iter().map(|(a, _)| a.as_str()).collect::<Vec<_>>(),
        "replicated": replicate,
        "dryRun": ctx.global.dry_run,
        "next": "needs GAS in the module (unrealengine-cli gas enable), then unrealengine-cli code apply; list the class in the AbilitySystemComponent's DefaultStartingData",
    });
    if ctx.global.dry_run {
        data["headerText"] = json!(h);
        data["sourceText"] = json!(c);
        return Ok(data);
    }
    std::fs::write(&header, h)
        .and_then(|_| std::fs::write(&source, c))
        .map_err(|e| CliError::internal("io.write", e.to_string()))?;
    Ok(data)
}

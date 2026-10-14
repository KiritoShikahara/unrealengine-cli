//! C++ source tools that run without the editor, plus `code apply` (Live Coding or rebuild).

use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use clap::Subcommand;
use regex::RegexBuilder;
use serde_json::{Value, json};

use super::Ctx;
use crate::core::error::CliError;
use crate::engine::{build, editor, locate};

#[derive(Debug, Subcommand)]
pub enum CodeCmd {
    /// Search project C++ sources (Source/ and Plugins/*/Source)
    Search {
        query: String,
        #[arg(long)]
        regex: bool,
        #[arg(long, default_value_t = 100)]
        limit: usize,
    },
    /// Read a source file (project-relative), optionally a line range
    Read {
        path: String,
        #[arg(long)]
        start: Option<usize>,
        #[arg(long)]
        end: Option<usize>,
    },
    /// Replace one unique occurrence of --old with --new
    Edit {
        path: String,
        #[arg(long)]
        old: String,
        #[arg(long)]
        new: String,
    },
    /// Create a UCLASS header/source pair in a module
    ClassCreate {
        name: String,
        #[arg(long, default_value = "Actor")]
        parent: String,
        #[arg(long)]
        module: Option<String>,
    },
    /// Apply changed sources: Live Coding for .cpp only, otherwise close, rebuild and reopen
    Apply {
        /// Only report the decision
        #[arg(long)]
        plan: bool,
        /// Rebuild even with unsaved assets (they are lost)
        #[arg(long)]
        force: bool,
    },
}

fn project_dir(ctx: &Ctx) -> Result<(PathBuf, PathBuf), CliError> {
    let up = ctx.uproject()?;
    let dir = up.parent().unwrap_or(Path::new(".")).to_path_buf();
    Ok((up, dir))
}

fn source_roots(dir: &Path) -> Vec<PathBuf> {
    let mut roots = vec![dir.join("Source")];
    if let Ok(plugins) = std::fs::read_dir(dir.join("Plugins")) {
        for p in plugins.flatten() {
            roots.push(p.path().join("Source"));
        }
    }
    roots.into_iter().filter(|r| r.is_dir()).collect()
}

fn is_source(p: &Path) -> bool {
    matches!(
        p.extension().and_then(|e| e.to_str()),
        Some("h" | "hpp" | "cpp" | "c" | "inl" | "cs")
    )
}

fn walk(dir: &Path, out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(dir) else {
        return;
    };
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            if !matches!(
                p.file_name().and_then(|n| n.to_str()),
                Some("Intermediate" | "Binaries")
            ) {
                walk(&p, out);
            }
        } else if is_source(&p) {
            out.push(p);
        }
    }
}

fn rel(dir: &Path, p: &Path) -> String {
    p.strip_prefix(dir)
        .unwrap_or(p)
        .to_string_lossy()
        .replace('\\', "/")
}

/// Resolves a project-relative path and refuses anything outside the project.
fn inside(dir: &Path, path: &str) -> Result<PathBuf, CliError> {
    let p = dir.join(path);
    let canon_dir = std::fs::canonicalize(dir).unwrap_or_else(|_| dir.to_path_buf());
    let canon = std::fs::canonicalize(&p)
        .map_err(|_| CliError::usage("code.not_found", format!("{path} does not exist")))?;
    if !canon.starts_with(&canon_dir) {
        return Err(CliError::usage(
            "code.outside_project",
            format!("{path} is outside the project"),
        ));
    }
    Ok(p)
}

pub fn run(ctx: &mut Ctx, cmd: CodeCmd) -> Result<Value, CliError> {
    match cmd {
        CodeCmd::Search {
            query,
            regex,
            limit,
        } => search(ctx, &query, regex, limit),
        CodeCmd::Read { path, start, end } => {
            let (_, dir) = project_dir(ctx)?;
            let p = inside(&dir, &path)?;
            let text = std::fs::read_to_string(&p)
                .map_err(|e| CliError::usage("code.unreadable", e.to_string()))?;
            let lines: Vec<&str> = text.lines().collect();
            let s = start.unwrap_or(1).max(1);
            let e = end.unwrap_or(lines.len()).min(lines.len());
            let body: Vec<String> = (s..=e).map(|i| format!("{i}\t{}", lines[i - 1])).collect();
            Ok(json!({ "path": path, "lines": lines.len(), "text": body.join("\n") }))
        }
        CodeCmd::Edit { path, old, new } => {
            let (_, dir) = project_dir(ctx)?;
            let p = inside(&dir, &path)?;
            let text = std::fs::read_to_string(&p)
                .map_err(|e| CliError::usage("code.unreadable", e.to_string()))?;
            let count = text.matches(old.as_str()).count();
            if count != 1 {
                return Err(CliError::usage(
                    "code.edit_ambiguous",
                    format!("--old matches {count} times in {path}; it must match exactly once"),
                )
                .with_hint("include more surrounding text"));
            }
            if !ctx.global.dry_run {
                std::fs::write(&p, text.replacen(old.as_str(), &new, 1))
                    .map_err(|e| CliError::internal("io.write", e.to_string()))?;
            }
            Ok(json!({ "path": path, "dryRun": ctx.global.dry_run, "replaced": 1 }))
        }
        CodeCmd::ClassCreate {
            name,
            parent,
            module,
        } => class_create(ctx, &name, &parent, module.as_deref()),
        CodeCmd::Apply { plan, force } => apply(ctx, plan, force),
    }
}

fn search(ctx: &Ctx, query: &str, regex: bool, limit: usize) -> Result<Value, CliError> {
    let (_, dir) = project_dir(ctx)?;
    let pattern = if regex {
        query.to_string()
    } else {
        regex::escape(query)
    };
    let re = RegexBuilder::new(&pattern)
        .case_insensitive(!regex)
        .build()
        .map_err(|e| CliError::usage("args.invalid", format!("bad regex: {e}")))?;
    let mut files = Vec::new();
    for root in source_roots(&dir) {
        walk(&root, &mut files);
    }
    let mut hits = Vec::new();
    'outer: for f in files {
        let Ok(text) = std::fs::read_to_string(&f) else {
            continue;
        };
        for (i, line) in text.lines().enumerate() {
            if re.is_match(line) {
                hits.push(json!({ "file": rel(&dir, &f), "line": i + 1, "text": line.trim() }));
                if hits.len() >= limit {
                    break 'outer;
                }
            }
        }
    }
    Ok(json!({ "hits": hits, "truncated": hits.len() >= limit }))
}

fn class_create(
    ctx: &Ctx,
    name: &str,
    parent: &str,
    module: Option<&str>,
) -> Result<Value, CliError> {
    let (_, dir) = project_dir(ctx)?;
    let module = match module {
        Some(m) => m.to_string(),
        None => std::fs::read_dir(dir.join("Source"))
            .ok()
            .and_then(|rd| {
                rd.flatten().find(|e| {
                    e.path()
                        .join(format!("{}.Build.cs", e.file_name().to_string_lossy()))
                        .is_file()
                })
            })
            .map(|e| e.file_name().to_string_lossy().to_string())
            .ok_or_else(|| {
                CliError::usage("code.no_module", "no C++ module under Source/")
                    .with_hint("unreal-cli setup --add-code-module")
            })?,
    };
    let (prefix, parent_header) = match parent {
        "Actor" => ("A", "GameFramework/Actor.h"),
        "Pawn" => ("A", "GameFramework/Pawn.h"),
        "Character" => ("A", "GameFramework/Character.h"),
        "ActorComponent" => ("U", "Components/ActorComponent.h"),
        "SceneComponent" => ("U", "Components/SceneComponent.h"),
        "Object" => ("U", "UObject/Object.h"),
        "GameModeBase" => ("A", "GameFramework/GameModeBase.h"),
        other => {
            return Err(CliError::usage(
                "code.bad_parent",
                format!("unsupported parent '{other}'"),
            )
            .with_hint(
                "Actor, Pawn, Character, ActorComponent, SceneComponent, Object, GameModeBase",
            ));
        }
    };
    if !name.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') || name.is_empty() {
        return Err(CliError::usage(
            "args.invalid",
            format!("'{name}' is not a valid class name"),
        ));
    }
    let api = format!("{}_API", module.to_uppercase());
    let header = dir.join(format!("Source/{module}/{name}.h"));
    let source = dir.join(format!("Source/{module}/{name}.cpp"));
    if header.exists() || source.exists() {
        return Err(CliError::usage(
            "code.exists",
            format!("{name}.h or {name}.cpp already exists"),
        ));
    }
    let class = format!("{prefix}{name}");
    let parent_class = format!("{prefix}{parent}");
    let h = format!(
        "#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"{parent_header}\"\n#include \"{name}.generated.h\"\n\nUCLASS()\nclass {api} {class} : public {parent_class}\n{{\n\tGENERATED_BODY()\n\npublic:\n\t{class}();\n}};\n"
    );
    let c = format!("#include \"{name}.h\"\n\n{class}::{class}()\n{{\n}}\n");
    if !ctx.global.dry_run {
        std::fs::write(&header, h)
            .and_then(|_| std::fs::write(&source, c))
            .map_err(|e| CliError::internal("io.write", e.to_string()))?;
    }
    Ok(json!({
        "class": class,
        "header": rel(&dir, &header),
        "source": rel(&dir, &source),
        "dryRun": ctx.global.dry_run,
        "next": "unreal-cli code apply (new headers need a rebuild)",
    }))
}

fn newest_binary(dir: &Path) -> Option<SystemTime> {
    let mut newest: Option<SystemTime> = None;
    let mut bins = vec![dir.join("Binaries")];
    if let Ok(plugins) = std::fs::read_dir(dir.join("Plugins")) {
        bins.extend(plugins.flatten().map(|p| p.path().join("Binaries")));
    }
    fn scan(d: &Path, newest: &mut Option<SystemTime>) {
        let Ok(rd) = std::fs::read_dir(d) else { return };
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                scan(&p, newest);
            } else if matches!(
                p.extension().and_then(|e| e.to_str()),
                Some("dll" | "so" | "dylib")
            ) && let Ok(m) = e.metadata().and_then(|m| m.modified())
            {
                *newest = Some(newest.map_or(m, |n| n.max(m)));
            }
        }
    }
    for b in bins {
        scan(&b, &mut newest);
    }
    newest
}

/// Files changed since the newest built binary.
pub fn changed_files(dir: &Path) -> Vec<PathBuf> {
    let since = newest_binary(dir);
    let mut files = Vec::new();
    for root in source_roots(dir) {
        walk(&root, &mut files);
    }
    for extra in std::fs::read_dir(dir).into_iter().flatten().flatten() {
        let p = extra.path();
        if matches!(p.extension().and_then(|e| e.to_str()), Some("uproject")) {
            files.push(p);
        }
    }
    files
        .into_iter()
        .filter(|f| match (since, f.metadata().and_then(|m| m.modified())) {
            (Some(s), Ok(m)) => m > s,
            (None, _) => true,
            _ => false,
        })
        .collect()
}

/// live when only .cpp/.c changed, rebuild for headers, build rules or descriptors.
pub fn decide(changed: &[PathBuf]) -> &'static str {
    if changed.is_empty() {
        return "none";
    }
    let only_cpp = changed
        .iter()
        .all(|p| matches!(p.extension().and_then(|e| e.to_str()), Some("cpp" | "c")));
    if only_cpp { "live" } else { "rebuild" }
}

fn apply(ctx: &mut Ctx, plan: bool, force: bool) -> Result<Value, CliError> {
    let (up, dir) = project_dir(ctx)?;
    let changed = changed_files(&dir);
    let names: Vec<String> = changed.iter().map(|p| rel(&dir, p)).collect();
    let running = editor::running_for(&up);
    let mut action = decide(&changed);
    if action == "live" && running.is_none() {
        action = "rebuild";
    }
    let reason = match action {
        "none" => "no source changed since the last build",
        "live" => ".cpp files only: Live Coding patches the running editor",
        _ if running.is_none() => "the editor is not running: build directly",
        _ => "headers, build rules or descriptors changed: close, rebuild, reopen",
    };
    if plan || ctx.global.dry_run || action == "none" {
        return Ok(
            json!({ "action": action, "reason": reason, "changedFiles": names, "applied": false }),
        );
    }
    if action == "live" {
        let live = ctx.call_with_timeout("code_live_compile", json!({}), 600_000)?;
        return Ok(
            json!({ "action": action, "reason": reason, "changedFiles": names, "live": live }),
        );
    }
    let engine = locate::resolve(&up)?;
    if let Some(inst) = &running {
        editor::close(inst, force, ctx.timeout()).map_err(|e| {
            if e.code == "editor.unsaved_changes" {
                e.with_hint(
                    "save first (unreal-cli tool call editor_save_all) or rerun with --force",
                )
            } else {
                e
            }
        })?;
    }
    let built = build::build_editor(&engine, &up)?;
    let reopened = if running.is_some() {
        let mode = running
            .as_ref()
            .map(|i| editor::WindowMode::from_instance(&i.mode))
            .unwrap_or(editor::WindowMode::Normal);
        Some(editor::open(&engine, &up, mode, Duration::from_secs(600))?)
    } else {
        None
    };
    Ok(
        json!({ "action": action, "reason": reason, "changedFiles": names, "rebuild": built, "reopened": reopened }),
    )
}

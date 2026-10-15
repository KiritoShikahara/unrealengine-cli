use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Instant;

use clap::Subcommand;
use serde_json::{Value, json};

use super::Ctx;
use crate::core::error::CliError;
use crate::engine::locate;

#[derive(Debug, Subcommand)]
pub enum LocCmd {
    /// Create or update a localization target: native culture, cultures to translate, content paths to gather
    Target {
        #[arg(long, default_value = "Game")]
        target: String,
        #[arg(long)]
        native: Option<String>,
        /// Culture to translate into, e.g. ja (repeatable)
        #[arg(long = "culture")]
        cultures: Vec<String>,
        /// Content path to gather text from, e.g. Content/UI (repeatable; default Content/*)
        #[arg(long = "path")]
        paths: Vec<String>,
    },
    /// Collect the localizable text of the gathered content
    Gather(Step),
    /// Write a PO file per culture (Content/Localization/<target>/<culture>/<target>.po)
    Export(Step),
    /// Read the translated PO files back
    Import(Step),
    /// Build the .locres files the game loads
    Compile(Step),
    /// Show the game text in a culture in the editor (off = native); with --asset/--property, read that text
    Preview {
        culture: String,
        #[arg(long, requires = "property")]
        asset: Option<String>,
        #[arg(long)]
        property: Option<String>,
    },
    /// Fill translations in a culture's PO file: --entry "Source text=Translation" (repeatable)
    Translate {
        culture: String,
        #[arg(long = "entry", required = true)]
        entries: Vec<String>,
        #[arg(long, default_value = "Game")]
        target: String,
    },
}

#[derive(Debug, clap::Args)]
pub struct Step {
    #[arg(long, default_value = "Game")]
    target: String,
    #[arg(long, default_value_t = 1800)]
    timeout: u64,
}

pub fn run(ctx: &mut Ctx, cmd: LocCmd) -> Result<Value, CliError> {
    match cmd {
        LocCmd::Target {
            target,
            native,
            cultures,
            paths,
        } => {
            let mut a = json!({ "target": target, "cultures": cultures });
            if let Some(n) = native {
                a["native"] = json!(n);
            }
            if !paths.is_empty() {
                a["paths"] = json!(paths);
            }
            ctx.call("loc_target", a)
        }
        LocCmd::Gather(s) => step(ctx, "Gather", &s),
        LocCmd::Export(s) => step(ctx, "Export", &s),
        LocCmd::Import(s) => step(ctx, "Import", &s),
        LocCmd::Compile(s) => step(ctx, "Compile", &s),
        LocCmd::Translate {
            culture,
            entries,
            target,
        } => translate(ctx, &target, &culture, &entries),
        LocCmd::Preview {
            culture,
            asset,
            property,
        } => {
            let mut a = json!({ "culture": culture });
            if let (Some(asset), Some(p)) = (asset, property) {
                a["asset"] = json!(asset);
                a["property"] = json!(p);
            }
            ctx.call("loc_preview", a)
        }
    }
}

fn project_dir(ctx: &Ctx) -> Result<(PathBuf, PathBuf), CliError> {
    let up = ctx.uproject()?;
    let dir = up.parent().unwrap_or(Path::new(".")).to_path_buf();
    Ok((up, dir))
}

/// Files under Content/Localization/<target> with an extension, as (culture, path).
fn data_files(dir: &Path, target: &str, ext: &str) -> Vec<(String, PathBuf)> {
    let root = dir.join("Content/Localization").join(target);
    let mut out = Vec::new();
    for e in std::fs::read_dir(&root).into_iter().flatten().flatten() {
        let p = e.path().join(format!("{target}.{ext}"));
        if p.is_file() {
            out.push((e.file_name().to_string_lossy().to_string(), p));
        }
    }
    out.sort();
    out
}

fn step(ctx: &mut Ctx, name: &str, s: &Step) -> Result<Value, CliError> {
    let (up, dir) = project_dir(ctx)?;
    let config = dir
        .join("Config/Localization")
        .join(format!("{}_{name}.ini", s.target));
    if !config.is_file() {
        return Err(
            CliError::usage("loc.no_target", format!("{} not found", config.display())).with_hint(
                format!(
                    "create the target first: unrealengine-cli loc target --target {} --culture ja",
                    s.target
                ),
            ),
        );
    }
    let engine = locate::resolve(&up)?;
    let exe = engine.editor_cmd_exe();
    let args = vec![
        up.to_string_lossy().to_string(),
        "-run=GatherText".to_string(),
        format!("-config={}", config.display()),
        "-SCCProvider=None".into(),
        "-unattended".into(),
        "-nosplash".into(),
        "-nullrhi".into(),
        "-stdout".into(),
    ];
    if ctx.global.dry_run {
        return Ok(json!({ "dryRun": true, "executable": exe, "args": args }));
    }
    let started = Instant::now();
    let mut child = Command::new(&exe)
        .args(&args)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| {
            CliError::internal("loc.spawn", format!("cannot run {}: {e}", exe.display()))
        })?;
    let mut stdout = child.stdout.take();
    let reader = std::thread::spawn(move || {
        let mut buf = Vec::new();
        if let Some(o) = stdout.as_mut() {
            let _ = std::io::Read::read_to_end(o, &mut buf);
        }
        buf
    });
    let status = loop {
        if let Some(st) = child
            .try_wait()
            .map_err(|e| CliError::internal("loc.spawn", e.to_string()))?
        {
            break st;
        }
        if started.elapsed().as_secs() > s.timeout {
            let _ = child.kill();
            return Err(CliError::timeout(
                "loc.timeout",
                format!(
                    "the {} step did not finish within {}s",
                    name.to_lowercase(),
                    s.timeout
                ),
            )
            .with_hint("run it again; large projects need a larger --timeout"));
        }
        std::thread::sleep(std::time::Duration::from_millis(250));
    };
    let stdout_bytes = reader.join().unwrap_or_default();
    let text = String::from_utf8_lossy(&stdout_bytes).to_string();
    let errors: Vec<&str> = text
        .lines()
        .filter(|l| l.contains(": Error: ") && !l.contains("LogSourceControl"))
        .take(20)
        .collect();
    let mut data = json!({
        "step": name.to_lowercase(),
        "target": s.target,
        "succeeded": status.success(),
        "seconds": started.elapsed().as_secs(),
        "errors": errors,
    });
    match name {
        "Export" | "Import" => {
            data["po"] = json!(
                data_files(&dir, &s.target, "po")
                    .into_iter()
                    .map(|(c, p)| json!({ "culture": c, "file": p }))
                    .collect::<Vec<_>>()
            );
        }
        "Compile" => {
            data["locres"] = json!(
                data_files(&dir, &s.target, "locres")
                    .into_iter()
                    .map(|(c, p)| json!({ "culture": c, "file": p }))
                    .collect::<Vec<_>>()
            );
        }
        _ => {}
    }
    if status.success() {
        Ok(data)
    } else {
        let tail: Vec<&str> = text
            .lines()
            .rev()
            .take(20)
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
            .collect();
        data["logTail"] = json!(tail);
        Err(CliError::editor(
            "loc.step_failed",
            format!("the {} step failed ({})", name.to_lowercase(), status),
        )
        .with_hint("see details.errors and details.logTail")
        .with_details(data))
    }
}

fn po_escape(s: &str) -> String {
    s.replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n")
}

/// Sets msgstr for every entry whose msgid equals a source text; returns the updated text and the count.
pub fn fill_po(po: &str, entries: &[(String, String)]) -> (String, Vec<String>) {
    let nl = if po.contains("\r\n") { "\r\n" } else { "\n" };
    let mut lines: Vec<String> = po.lines().map(String::from).collect();
    let mut filled = Vec::new();
    for i in 0..lines.len() {
        let Some(id) = lines[i]
            .strip_prefix("msgid \"")
            .and_then(|r| r.strip_suffix('"'))
            .map(String::from)
        else {
            continue;
        };
        if let Some((src, dst)) = entries.iter().find(|(s, _)| po_escape(s) == id)
            && let Some(j) =
                (i + 1..lines.len().min(i + 4)).find(|&j| lines[j].starts_with("msgstr "))
        {
            lines[j] = format!("msgstr \"{}\"", po_escape(dst));
            filled.push(src.clone());
        }
    }
    let mut out = lines.join(nl);
    out.push_str(nl);
    (out, filled)
}

fn translate(
    ctx: &mut Ctx,
    target: &str,
    culture: &str,
    entries: &[String],
) -> Result<Value, CliError> {
    let (_, dir) = project_dir(ctx)?;
    let file = dir
        .join("Content/Localization")
        .join(target)
        .join(culture)
        .join(format!("{target}.po"));
    let text = std::fs::read_to_string(&file).map_err(|_| {
        CliError::usage("loc.no_po", format!("{} not found", file.display()))
            .with_hint("export first: unrealengine-cli loc gather, then loc export")
    })?;
    let mut pairs = Vec::new();
    for e in entries {
        let (s, t) = e.split_once('=').ok_or_else(|| {
            CliError::usage(
                "args.invalid",
                format!("'{e}' is not \"Source=Translation\""),
            )
        })?;
        pairs.push((s.to_string(), t.to_string()));
    }
    let (new_text, filled) = fill_po(&text, &pairs);
    let missing: Vec<&String> = pairs
        .iter()
        .map(|(s, _)| s)
        .filter(|s| !filled.contains(s))
        .collect();
    if !ctx.global.dry_run {
        std::fs::write(&file, new_text)
            .map_err(|e| CliError::internal("io.write", format!("{}: {e}", file.display())))?;
    }
    Ok(
        json!({ "po": file, "filled": filled, "missing": missing, "next": "unrealengine-cli loc import, then loc compile" }),
    )
}

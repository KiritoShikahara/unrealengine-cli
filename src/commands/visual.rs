use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use clap::{Args, Subcommand, ValueEnum};
use image::{Rgba, RgbaImage};
use serde_json::{Value, json};

use super::Ctx;
use crate::core::error::CliError;

#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Source {
    /// The play session's game viewport (pie start first)
    Pie,
    /// The editor's active viewport
    Editor,
}

#[derive(Debug, Args)]
pub struct Limits {
    /// Per-pixel difference (0-255, largest RGB channel) still counted as equal
    #[arg(long, default_value_t = 8)]
    pub tolerance: u8,
    /// Highest share of differing pixels, in percent, that still passes
    #[arg(long, default_value_t = 0.1)]
    pub threshold: f64,
}

#[derive(Debug, Args)]
pub struct Capture {
    /// Use this PNG instead of taking a screenshot
    #[arg(long)]
    pub image: Option<PathBuf>,
    #[arg(long, value_enum, default_value_t = Source::Pie)]
    pub source: Source,
    /// Play world (server, client:1, ...) for --source pie
    #[arg(long)]
    pub world: Option<String>,
}

#[derive(Debug, Subcommand)]
pub enum VisualCmd {
    /// Save a screenshot (or --image) as the baseline Tests/Visual/<name>.png
    Baseline {
        name: String,
        #[command(flatten)]
        capture: Capture,
    },
    /// Take a screenshot (or --image) and compare it with the baseline; exit 4 (visual.mismatch) over the threshold
    Compare {
        name: String,
        #[command(flatten)]
        capture: Capture,
        #[command(flatten)]
        limits: Limits,
    },
    /// Compare two PNG files
    Diff {
        a: PathBuf,
        b: PathBuf,
        /// Write the difference image here (differing pixels red)
        #[arg(long)]
        out: Option<PathBuf>,
        #[command(flatten)]
        limits: Limits,
    },
}

pub fn run(ctx: &mut Ctx, cmd: VisualCmd) -> Result<Value, CliError> {
    match cmd {
        VisualCmd::Diff { a, b, out, limits } => {
            let a = ctx.cwd.join(a);
            let b = ctx.cwd.join(b);
            let out = out.map(|o| ctx.cwd.join(o));
            judge(diff_files(&a, &b, out.as_deref(), &limits)?)
        }
        VisualCmd::Baseline { name, capture } => {
            let dir = project_dir(ctx)?;
            let base = baseline_path(&dir, &name)?;
            let shot = take(ctx, &dir, &name, &capture, "baseline")?;
            let img = load(&shot)?;
            if let Some(parent) = base.parent() {
                std::fs::create_dir_all(parent)
                    .map_err(|e| CliError::internal("io.write", e.to_string()))?;
            }
            img.save(&base)
                .map_err(|e| CliError::internal("io.write", format!("{}: {e}", base.display())))?;
            Ok(
                json!({ "name": name, "baseline": base, "width": img.width(), "height": img.height() }),
            )
        }
        VisualCmd::Compare {
            name,
            capture,
            limits,
        } => {
            let dir = project_dir(ctx)?;
            let base = baseline_path(&dir, &name)?;
            if !base.is_file() {
                return Err(CliError::usage(
                    "visual.no_baseline",
                    format!("no baseline {}", base.display()),
                )
                .with_hint(format!(
                    "take one first: unrealengine-cli visual baseline {name}"
                )));
            }
            let shot = take(ctx, &dir, &name, &capture, "actual")?;
            let diff = work_dir(&dir).join(format!("{name}.diff.png"));
            let mut data = diff_files(&base, &shot, Some(&diff), &limits)?;
            data["name"] = json!(name);
            judge(data)
        }
    }
}

fn project_dir(ctx: &Ctx) -> Result<PathBuf, CliError> {
    let up = ctx.uproject()?;
    Ok(up.parent().unwrap_or(Path::new(".")).to_path_buf())
}

fn baseline_path(dir: &Path, name: &str) -> Result<PathBuf, CliError> {
    if name.is_empty()
        || !name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "-_.".contains(c))
        || name.starts_with('.')
    {
        return Err(CliError::usage(
            "args.invalid",
            format!("'{name}' is not a valid baseline name (letters, digits, - _ .)"),
        ));
    }
    Ok(dir.join("Tests/Visual").join(format!("{name}.png")))
}

fn work_dir(dir: &Path) -> PathBuf {
    dir.join("Saved/UnrealCli/Visual")
}

/// The image to use: --image, or a fresh screenshot from the play session or the editor viewport.
fn take(
    ctx: &mut Ctx,
    dir: &Path,
    name: &str,
    capture: &Capture,
    kind: &str,
) -> Result<PathBuf, CliError> {
    if let Some(p) = &capture.image {
        return Ok(ctx.cwd.join(p));
    }
    let work = work_dir(dir);
    std::fs::create_dir_all(&work).map_err(|e| CliError::internal("io.write", e.to_string()))?;
    let path = work.join(format!("{name}.{kind}.png"));
    let _ = std::fs::remove_file(&path);
    let mut args = json!({ "path": path });
    let tool = match capture.source {
        Source::Pie => {
            if let Some(w) = &capture.world {
                args["world"] = json!(w);
            }
            "pie_screenshot"
        }
        Source::Editor => "editor_screenshot",
    };
    ctx.call(tool, args)?;
    // Some captures finish a frame later; wait for a readable file.
    let deadline = Instant::now() + Duration::from_secs(10);
    while image::open(&path).is_err() {
        if Instant::now() > deadline {
            return Err(CliError::editor(
                "visual.no_screenshot",
                format!("{tool} did not write {}", path.display()),
            ));
        }
        std::thread::sleep(Duration::from_millis(200));
    }
    Ok(path)
}

fn load(p: &Path) -> Result<RgbaImage, CliError> {
    image::open(p).map(|i| i.to_rgba8()).map_err(|e| {
        CliError::usage(
            "visual.unreadable",
            format!("cannot read {}: {e}", p.display()),
        )
    })
}

/// Compares two images: per-pixel delta is the largest RGB channel difference.
pub fn diff_images(a: &RgbaImage, b: &RgbaImage, tolerance: u8) -> (u64, u8, RgbaImage) {
    let mut out = RgbaImage::new(a.width(), a.height());
    let mut count = 0u64;
    let mut max = 0u8;
    for (x, y, pa) in a.enumerate_pixels() {
        let pb = b.get_pixel(x, y);
        let delta = (0..3).map(|c| pa[c].abs_diff(pb[c])).max().unwrap_or(0);
        max = max.max(delta);
        if delta > tolerance {
            count += 1;
            out.put_pixel(x, y, Rgba([255, 0, 0, 255]));
        } else {
            let g = ((pb[0] as u32 + pb[1] as u32 + pb[2] as u32) / 9) as u8;
            out.put_pixel(x, y, Rgba([g, g, g, 255]));
        }
    }
    (count, max, out)
}

fn diff_files(a: &Path, b: &Path, out: Option<&Path>, limits: &Limits) -> Result<Value, CliError> {
    let (ia, ib) = (load(a)?, load(b)?);
    if ia.dimensions() != ib.dimensions() {
        return Err(CliError::usage(
            "visual.size_mismatch",
            format!(
                "{}x{} vs {}x{}",
                ia.width(),
                ia.height(),
                ib.width(),
                ib.height()
            ),
        )
        .with_hint("compare captures of the same resolution (same viewport / window size)"));
    }
    let (count, max, img) = diff_images(&ia, &ib, limits.tolerance);
    let total = (ia.width() as u64 * ia.height() as u64).max(1);
    let percent = ((count as f64 / total as f64) * 100.0 * 10_000.0).round() / 10_000.0;
    if let Some(o) = out {
        if let Some(parent) = o.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        img.save(o)
            .map_err(|e| CliError::internal("io.write", format!("{}: {e}", o.display())))?;
    }
    Ok(json!({
        "baseline": a,
        "actual": b,
        "width": ia.width(),
        "height": ia.height(),
        "diffPixels": count,
        "diffPercent": percent,
        "maxDelta": max,
        "tolerance": limits.tolerance,
        "threshold": limits.threshold,
        "diffImage": out,
        "ok": percent <= limits.threshold,
    }))
}

fn judge(data: Value) -> Result<Value, CliError> {
    if data["ok"] == true {
        return Ok(data);
    }
    Err(CliError::editor(
        "visual.mismatch",
        format!(
            "{}% of the pixels differ (threshold {}%)",
            data["diffPercent"], data["threshold"]
        ),
    )
    .with_hint("look at details.diffImage (differing pixels are red); if the change is intended, take a new baseline")
    .with_details(data))
}

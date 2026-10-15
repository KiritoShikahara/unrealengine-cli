mod support;

use std::path::{Path, PathBuf};

use image::{Rgba, RgbaImage};
use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};
use unrealengine_cli::commands::perf::timer_summary;

#[test]
fn timer_csv_is_summarised_by_inclusive_time() {
    let csv = "Name,Count,C.Avg,Incl,I.Min,I.Max,I.Avg,I.Med,Excl,E.Min,E.Max,E.Avg,E.Med\n\
        Small,10,10,0.001,0,0.0002,0.0001,0,0.001,0,0,0,0\n\
        Tick, World,5,5,0.5,0,0.2,0.1,0.1,0.25,0,0,0,0\n\
        Big,2,2,2.0,0,1.5,1.0,1.0,0.5,0,0,0,0\n";
    let rows = timer_summary(csv, 2);
    assert_eq!(rows.len(), 2);
    assert_eq!(rows[0]["name"], "Big");
    assert_eq!(rows[0]["inclusiveMs"], 2000.0);
    assert_eq!(rows[0]["maxMs"], 1500.0);
    assert_eq!(rows[1]["name"], "Tick, World");
    assert_eq!(rows[1]["count"], 5);
    assert_eq!(rows[1]["exclusiveMs"], 250.0);
    assert!(timer_summary("bad header\n1,2", 5).is_empty());
}

#[test]
fn perf_commands_reach_their_tools_and_check_the_trace_file() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("perf_measure", |a| Ok(a.clone()))
        .tool("perf_trace", |a| Ok(a.clone()))
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["perf", "measure", "--seconds", "3", "--hitch-ms", "33"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"], json!({"seconds": 3.0, "hitchMs": 33.0}));
    let (code, v) = run_json(home.path(), home.path(), &["perf", "trace", "stop"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"], json!({"action": "stop"}));
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["perf", "trace", "summary", "missing.utrace"],
    );
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "perf.no_trace");
}

fn png(dir: &Path, name: &str, w: u32, h: u32, edit: impl Fn(&mut RgbaImage)) -> PathBuf {
    let mut img = RgbaImage::from_pixel(w, h, Rgba([100, 120, 140, 255]));
    edit(&mut img);
    let p = dir.join(name);
    img.save(&p).unwrap();
    p
}

fn s(p: &Path) -> &str {
    p.to_str().unwrap()
}

#[test]
fn identical_images_differ_by_zero() {
    let d = tempfile::tempdir().unwrap();
    let a = png(d.path(), "a.png", 100, 100, |_| {});
    let b = png(d.path(), "b.png", 100, 100, |_| {});
    let (code, v) = run_json(d.path(), d.path(), &["visual", "diff", s(&a), s(&b)]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["diffPercent"], 0.0);
    assert_eq!(v["data"]["maxDelta"], 0);
    assert_eq!(v["data"]["ok"], true);
}

#[test]
fn one_pixel_counts_and_threshold_decides() {
    let d = tempfile::tempdir().unwrap();
    let a = png(d.path(), "a.png", 100, 100, |_| {});
    let b = png(d.path(), "b.png", 100, 100, |i| {
        i.put_pixel(5, 5, Rgba([255, 0, 0, 255]))
    });
    let diff = d.path().join("diff.png");
    let (code, v) = run_json(
        d.path(),
        d.path(),
        &["visual", "diff", s(&a), s(&b), "--out", s(&diff)],
    );
    assert_eq!(code, 0, "0.01% is under the default 0.1%: {v}");
    assert_eq!(v["data"]["diffPixels"], 1);
    assert_eq!(v["data"]["diffPercent"], 0.01);
    assert_eq!(v["data"]["maxDelta"], 155);
    let out = image::open(&diff).unwrap().to_rgba8();
    assert_eq!(out.get_pixel(5, 5), &Rgba([255, 0, 0, 255]));
    assert_ne!(out.get_pixel(0, 0), &Rgba([255, 0, 0, 255]));

    let (code, v) = run_json(
        d.path(),
        d.path(),
        &["visual", "diff", s(&a), s(&b), "--threshold", "0"],
    );
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "visual.mismatch");
    assert_eq!(v["error"]["details"]["diffPixels"], 1);
}

#[test]
fn small_differences_within_tolerance_pass() {
    let d = tempfile::tempdir().unwrap();
    let a = png(d.path(), "a.png", 10, 10, |_| {});
    let b = png(d.path(), "b.png", 10, 10, |i| {
        for p in i.pixels_mut() {
            p.0[0] += 8;
        }
    });
    let (code, v) = run_json(d.path(), d.path(), &["visual", "diff", s(&a), s(&b)]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["diffPixels"], 0);
    assert_eq!(v["data"]["maxDelta"], 8);
    let (code, _) = run_json(
        d.path(),
        d.path(),
        &["visual", "diff", s(&a), s(&b), "--tolerance", "7"],
    );
    assert_eq!(code, 4);
}

#[test]
fn different_sizes_are_a_usage_error() {
    let d = tempfile::tempdir().unwrap();
    let a = png(d.path(), "a.png", 10, 10, |_| {});
    let b = png(d.path(), "b.png", 12, 10, |_| {});
    let (code, v) = run_json(d.path(), d.path(), &["visual", "diff", s(&a), s(&b)]);
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "visual.size_mismatch");
    let (code, v) = run_json(
        d.path(),
        d.path(),
        &["visual", "diff", s(&a), s(&d.path().join("none.png"))],
    );
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "visual.unreadable");
}

#[test]
fn baseline_and_compare_with_image_files() {
    let d = tempfile::tempdir().unwrap();
    std::fs::write(d.path().join("Foo.uproject"), "{}").unwrap();
    let a = png(d.path(), "a.png", 20, 20, |_| {});
    let dark = png(d.path(), "dark.png", 20, 20, |i| {
        for p in i.pixels_mut() {
            *p = Rgba([0, 0, 0, 255]);
        }
    });
    let (code, v) = run_json(
        d.path(),
        d.path(),
        &["visual", "baseline", "arena", "--image", s(&a)],
    );
    assert_eq!(code, 0, "{v}");
    let base = d.path().join("Tests/Visual/arena.png");
    assert!(base.is_file(), "{v}");
    let (code, v) = run_json(
        d.path(),
        d.path(),
        &["visual", "compare", "arena", "--image", s(&a)],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["diffPercent"], 0.0);
    let (code, v) = run_json(
        d.path(),
        d.path(),
        &["visual", "compare", "arena", "--image", s(&dark)],
    );
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "visual.mismatch");
    assert_eq!(v["error"]["details"]["diffPercent"], 100.0);
    assert!(
        Path::new(v["error"]["details"]["diffImage"].as_str().unwrap()).is_file(),
        "{v}"
    );
    let (code, v) = run_json(
        d.path(),
        d.path(),
        &["visual", "compare", "missing", "--image", s(&a)],
    );
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "visual.no_baseline");
    assert!(v["error"]["hint"].is_string());
}

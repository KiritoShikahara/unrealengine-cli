use std::path::PathBuf;
use std::time::{Duration, SystemTime};

use unrealengine_cli::commands::code::{changed_files, decide};
use unrealengine_cli::engine::editor::WindowMode;

#[test]
fn rebuild_reopens_in_the_recorded_mode() {
    assert_eq!(WindowMode::from_instance("nullrhi"), WindowMode::NullRhi);
    // Without -RenderOffscreen, -nullrhi windows hit a fatal error in GetRestoredDimensions.
    let null_args = unrealengine_cli::engine::editor::mode_args(WindowMode::NullRhi);
    assert!(null_args.contains(&"-nullrhi") && null_args.contains(&"-RenderOffscreen"));
    assert!(unrealengine_cli::engine::editor::mode_args(WindowMode::Normal).is_empty());
    assert_eq!(WindowMode::from_instance("headless"), WindowMode::Headless);
    assert_eq!(WindowMode::from_instance(""), WindowMode::Normal);
}

#[test]
fn decision_follows_file_kinds() {
    let p = |s: &str| PathBuf::from(s);
    assert_eq!(decide(&[]), "none");
    assert_eq!(decide(&[p("Source/G/A.cpp"), p("Source/G/B.c")]), "live");
    assert_eq!(decide(&[p("Source/G/A.cpp"), p("Source/G/A.h")]), "rebuild");
    assert_eq!(decide(&[p("Source/G/G.Build.cs")]), "rebuild");
    assert_eq!(decide(&[p("Game.uproject")]), "rebuild");
}

fn touch(path: &std::path::Path, t: SystemTime) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, "x").unwrap();
    std::fs::File::options()
        .write(true)
        .open(path)
        .unwrap()
        .set_modified(t)
        .unwrap();
}

#[test]
fn only_files_newer_than_binaries_count() {
    let d = tempfile::tempdir().unwrap();
    let old = SystemTime::now() - Duration::from_secs(3600);
    let now = SystemTime::now();
    touch(&d.path().join("Game.uproject"), old);
    touch(&d.path().join("Source/G/Old.cpp"), old);
    touch(
        &d.path().join("Binaries/Win64/UnrealEditor-G.dll"),
        old + Duration::from_secs(60),
    );
    touch(&d.path().join("Source/G/New.cpp"), now);
    let changed = changed_files(d.path());
    assert_eq!(changed.len(), 1, "{changed:?}");
    assert!(changed[0].ends_with("New.cpp"));
    assert_eq!(decide(&changed), "live");
}

#[test]
fn stuck_editors_can_be_terminated_with_their_children() {
    use unrealengine_cli::engine::editor::{kill_process_tree, process_alive};
    let mut child = if cfg!(windows) {
        std::process::Command::new("ping")
            .args(["-n", "60", "127.0.0.1"])
            .stdout(std::process::Stdio::null())
            .spawn()
            .unwrap()
    } else {
        std::process::Command::new("sleep")
            .arg("60")
            .spawn()
            .unwrap()
    };
    let pid = child.id();
    assert!(process_alive(pid));
    kill_process_tree(pid);
    let _ = child.wait();
    assert!(!process_alive(pid));
}

#[test]
fn apply_action_rebuild_flag_and_missing_editor() {
    use unrealengine_cli::commands::code::apply_action;
    assert_eq!(apply_action("live", true, false), "live");
    assert_eq!(apply_action("live", false, false), "rebuild");
    assert_eq!(
        apply_action("none", true, true),
        "rebuild",
        "--rebuild covers deleted files"
    );
    assert_eq!(apply_action("none", true, false), "none");
}

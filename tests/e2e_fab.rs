//! Fab against the live service. Runs only with UNREALENGINE_CLI_E2E_FAB=1 and the TestProject editor open and
//! signed in to Fab. It downloads listings already in your library (nothing is bought or claimed):
//! UNREALENGINE_CLI_E2E_FAB_PACK (an Unreal Engine pack) and UNREALENGINE_CLI_E2E_FAB_FBX (a listing with an FBX
//! download); by default the first library items offering those formats.

use std::path::PathBuf;

use assert_cmd::Command;
use serde_json::Value;

fn enabled() -> bool {
    std::env::var("UNREALENGINE_CLI_E2E_FAB").as_deref() == Ok("1")
}

fn cli(args: &[&str]) -> (i32, Value) {
    let out = Command::cargo_bin("unrealengine-cli")
        .unwrap()
        .current_dir(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("TestProject"))
        .args(["--output", "json", "--timeout-ms", "120000"])
        .args(args)
        .output()
        .unwrap();
    let text = String::from_utf8_lossy(&out.stdout);
    let v: Value =
        serde_json::from_str(&text).unwrap_or_else(|e| panic!("{args:?}: not JSON ({e}): {text}"));
    (out.status.code().unwrap_or(-1), v)
}

fn ok(args: &[&str]) -> Value {
    let (code, v) = cli(args);
    assert_eq!(code, 0, "{args:?} failed: {v}");
    v["data"].clone()
}

/// The listing uid from the environment, else the first library item offering the format.
fn listing(var: &str, library: &Value, format: &str) -> Option<String> {
    if let Ok(v) = std::env::var(var) {
        return Some(v);
    }
    library["items"].as_array()?.iter().find_map(|i| {
        let formats = i["formats"].as_array()?;
        (i["canDownload"] == true && formats.iter().any(|f| f == format))
            .then(|| i["uid"].as_str().unwrap().to_string())
    })
}

#[test]
fn e2e_fab_library_pack_and_fbx() {
    if !enabled() {
        eprintln!("skipped: set UNREALENGINE_CLI_E2E_FAB=1 with a signed-in TestProject editor");
        return;
    }
    let status = ok(&["fab", "status"]);
    assert_eq!(
        status["signedIn"], true,
        "sign in to Fab in the editor first: {status}"
    );
    let library = ok(&["fab", "library", "--count", "100"]);
    assert!(library["count"].as_u64().unwrap() > 0, "{library}");

    for (var, format) in [
        ("UNREALENGINE_CLI_E2E_FAB_PACK", "unreal-engine"),
        ("UNREALENGINE_CLI_E2E_FAB_FBX", "fbx"),
    ] {
        let Some(uid) = listing(var, &library, format) else {
            eprintln!("no library listing offers {format}; set {var}");
            continue;
        };
        let added = ok(&["fab", "add", &uid, "--format", format, "--timeout", "3600"]);
        let paths = added["paths"].as_array().unwrap();
        assert!(!paths.is_empty(), "{format}: nothing was added: {added}");
        for p in paths {
            let found = ok(&["asset", "list", p.as_str().unwrap()]);
            assert!(
                found.to_string().contains('/'),
                "{format}: {p} is empty: {found}"
            );
        }
    }
    // The Fab page opened for the commands is closed again: a second status call opens and closes its own page.
    assert_eq!(ok(&["fab", "status"])["signedIn"], true);
}

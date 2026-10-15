mod support;

use std::path::Path;

use serde_json::{Value, json};
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

fn project() -> tempfile::TempDir {
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(
        dir.path().join("Foo.uproject"),
        "{\n\t\"FileVersion\": 3,\n\t\"EngineAssociation\": \"5.7\",\n\t\"Plugins\": [\n\t\t{ \"Name\": \"Niagara\", \"Enabled\": true }\n\t]\n}\n",
    )
    .unwrap();
    std::fs::create_dir_all(dir.path().join("Config")).unwrap();
    std::fs::write(
        dir.path().join("Config/DefaultEngine.ini"),
        "[/Script/EngineSettings.GameMapsSettings]\r\nGameDefaultMap=/Game/Main\r\n",
    )
    .unwrap();
    dir
}

fn ini(p: &Path) -> String {
    std::fs::read_to_string(p.join("Config/DefaultEngine.ini")).unwrap()
}

fn plugins(p: &Path) -> Vec<(String, bool)> {
    let v: Value =
        serde_json::from_str(&std::fs::read_to_string(p.join("Foo.uproject")).unwrap()).unwrap();
    v["Plugins"]
        .as_array()
        .unwrap()
        .iter()
        .map(|p| {
            (
                p["Name"].as_str().unwrap().to_string(),
                p["Enabled"].as_bool().unwrap(),
            )
        })
        .collect()
}

#[test]
fn configure_null_writes_the_platform_service_and_plugins() {
    let home = tempfile::tempdir().unwrap();
    let p = project();
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &["online", "configure", "--subsystem", "null"],
    );
    assert_eq!(code, 0, "{v}");
    let text = ini(p.path());
    assert!(text.contains("GameDefaultMap=/Game/Main\r\n"), "{text}");
    assert!(
        text.contains("[OnlineSubsystem]\r\nDefaultPlatformService=Null\r\n"),
        "CRLF kept: {text:?}"
    );
    let names = plugins(p.path());
    assert!(names.contains(&("Niagara".into(), true)));
    assert!(names.contains(&("OnlineSubsystemNull".into(), true)));
    assert!(names.contains(&("OnlineSubsystemUtils".into(), true)));

    let (code, v) = run_json(home.path(), p.path(), &["online", "check"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["subsystem"], "null");
    assert_eq!(v["data"]["missing"], json!([]));
}

#[test]
fn configure_dry_run_changes_nothing_and_hides_secrets() {
    let home = tempfile::tempdir().unwrap();
    let p = project();
    let before = ini(p.path());
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &[
            "--dry-run",
            "online",
            "configure",
            "--subsystem",
            "eos",
            "--set",
            "ProductId=prod123",
            "--set",
            "ClientSecret=TOPSECRET",
        ],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(ini(p.path()), before);
    let text = v.to_string();
    assert!(!text.contains("TOPSECRET"), "secret leaked: {text}");
    assert!(text.contains("prod123"));
    assert!(text.contains("OnlineSubsystemEOS"));
}

#[test]
fn eos_check_lists_missing_keys_without_secret_values() {
    let home = tempfile::tempdir().unwrap();
    let p = project();
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &[
            "online",
            "configure",
            "--subsystem",
            "eos",
            "--set",
            "ProductId=prod123",
            "--set",
            "ClientSecret=TOPSECRET",
        ],
    );
    assert_eq!(code, 0, "{v}");
    assert!(!v.to_string().contains("TOPSECRET"));
    let text = ini(p.path());
    assert!(text.contains("DefaultPlatformService=EOS"), "{text}");
    assert!(text.contains("ProductId=\"prod123\""), "{text}");
    assert!(text.contains("ClientSecret=\"TOPSECRET\""), "{text}");

    let (code, v) = run_json(home.path(), p.path(), &["online", "check"]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "online.missing_settings");
    let missing = &v["error"]["details"]["missing"];
    for k in ["SandboxId", "DeploymentId", "ClientId"] {
        assert!(missing.as_array().unwrap().contains(&json!(k)), "{v}");
    }
    assert!(!missing.as_array().unwrap().contains(&json!("ProductId")));
    assert_eq!(v["error"]["details"]["settings"]["ClientSecret"], "***");
    assert!(!v.to_string().contains("TOPSECRET"));

    // Later keys merge into the same artifact instead of replacing it.
    let (code, _) = run_json(
        home.path(),
        p.path(),
        &[
            "online",
            "configure",
            "--subsystem",
            "eos",
            "--set",
            "SandboxId=s",
            "--set",
            "DeploymentId=d",
            "--set",
            "ClientId=c",
        ],
    );
    assert_eq!(code, 0);
    let (code, v) = run_json(home.path(), p.path(), &["online", "check"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["settings"]["ProductId"], "prod123");
    assert_eq!(ini(p.path()).matches("Artifacts=").count(), 1);
}

#[test]
fn steam_needs_an_app_id() {
    let home = tempfile::tempdir().unwrap();
    let p = project();
    run_json(
        home.path(),
        p.path(),
        &["online", "configure", "--subsystem", "steam"],
    );
    let (code, v) = run_json(home.path(), p.path(), &["online", "check"]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["details"]["missing"], json!(["SteamDevAppId"]));
    let (code, _) = run_json(
        home.path(),
        p.path(),
        &[
            "online",
            "configure",
            "--subsystem",
            "steam",
            "--set",
            "SteamDevAppId=480",
        ],
    );
    assert_eq!(code, 0);
    let (code, v) = run_json(home.path(), p.path(), &["online", "check"]);
    assert_eq!(code, 0, "{v}");
    assert!(ini(p.path()).contains("[OnlineSubsystemSteam]"));
}

#[test]
fn unknown_keys_are_rejected() {
    let home = tempfile::tempdir().unwrap();
    let p = project();
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &[
            "online",
            "configure",
            "--subsystem",
            "steam",
            "--set",
            "ProductId=x",
        ],
    );
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "online.unknown_key");
}

#[test]
fn session_reaches_the_tool() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("online_session", |a| Ok(a.clone()))
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &[
            "online",
            "session",
            "find",
            "--world",
            "standalone:2",
            "--timeout",
            "3",
        ],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(
        v["data"],
        json!({"action": "find", "world": "standalone:2", "timeoutSeconds": 3.0})
    );
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &[
            "online",
            "session",
            "create",
            "--name",
            "Lobby",
            "--max-players",
            "8",
            "--online",
        ],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(
        v["data"],
        json!({"action": "create", "sessionName": "Lobby", "maxPlayers": 8, "lan": false})
    );
}

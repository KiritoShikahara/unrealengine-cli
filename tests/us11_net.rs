mod support;

use serde_json::json;
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

fn echo(tools: &[&str]) -> support::mock_bridge::MockBridge {
    let mut b = MockBuilder::new();
    for t in tools {
        b = b.tool(t, |a| Ok(a.clone()));
    }
    b.start()
}

#[test]
fn pie_start_takes_players_and_net_mode() {
    let home = tempfile::tempdir().unwrap();
    let m = echo(&["pie_start", "pie_worlds"]);
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, serde_json::Value)> = vec![
        (vec!["pie", "start"], json!({})),
        (
            vec!["pie", "start", "--players", "2", "--net-mode", "listen"],
            json!({"players": 2, "netMode": "listen"}),
        ),
        (
            vec!["pie", "start", "--players", "3", "--net-mode", "client"],
            json!({"players": 3, "netMode": "client"}),
        ),
        (vec!["pie", "worlds"], json!({})),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

#[test]
fn bad_net_mode_is_a_usage_error() {
    let home = tempfile::tempdir().unwrap();
    let (code, _) = run_json(
        home.path(),
        home.path(),
        &["pie", "start", "--net-mode", "lan"],
    );
    assert_eq!(code, 2);
}

#[test]
fn world_option_reaches_play_tools() {
    let home = tempfile::tempdir().unwrap();
    let m = echo(&[
        "pie_actors",
        "pie_input",
        "pie_get",
        "pie_screenshot",
        "pie_widgets",
        "pie_exec",
    ]);
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, serde_json::Value)> = vec![
        (
            vec!["pie", "actors", "--world", "client:1"],
            json!({"world": "client:1"}),
        ),
        (
            vec!["pie", "--world", "server", "actors", "--class", "BP_Coin"],
            json!({"world": "server", "classFilter": "BP_Coin"}),
        ),
        (
            vec![
                "pie",
                "input",
                "W",
                "--world",
                "client:2",
                "--seconds",
                "0.5",
            ],
            json!({"world": "client:2", "key": "W", "seconds": 0.5}),
        ),
        (
            vec!["pie", "get", "player", "Health", "--world", "server"],
            json!({"world": "server", "actor": "player", "properties": ["Health"]}),
        ),
        (vec!["pie", "screenshot"], json!({})),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

#[test]
fn blueprint_replication_commands_map_to_tools() {
    let home = tempfile::tempdir().unwrap();
    let m = echo(&["bp_set_replication", "bp_set_rpc"]);
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, serde_json::Value)> = vec![
        (
            vec!["blueprint", "replicate", "/Game/BP_A", "Health"],
            json!({"path": "/Game/BP_A", "variable": "Health", "mode": "replicated"}),
        ),
        (
            vec![
                "blueprint",
                "replicate",
                "/Game/BP_A",
                "Health",
                "--notify",
                "--condition",
                "OwnerOnly",
            ],
            json!({"path": "/Game/BP_A", "variable": "Health", "mode": "notify", "condition": "OwnerOnly"}),
        ),
        (
            vec!["blueprint", "replicate", "/Game/BP_A", "Health", "--off"],
            json!({"path": "/Game/BP_A", "variable": "Health", "mode": "none"}),
        ),
        (
            vec![
                "blueprint",
                "rpc",
                "/Game/BP_A",
                "ServerFire",
                "--type",
                "server",
                "--reliable",
            ],
            json!({"path": "/Game/BP_A", "event": "ServerFire", "type": "server", "reliable": true}),
        ),
        (
            vec![
                "blueprint",
                "rpc",
                "/Game/BP_A",
                "Boom",
                "--type",
                "multicast",
            ],
            json!({"path": "/Game/BP_A", "event": "Boom", "type": "multicast"}),
        ),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

const HEADER: &str = r#"#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NetActor.generated.h"

UCLASS()
class GAME_API ANetActor : public AActor
{
	GENERATED_BODY()

public:
	ANetActor();
};
"#;

const SOURCE: &str = r#"#include "NetActor.h"

ANetActor::ANetActor()
{
	bReplicates = true;
}
"#;

fn cpp_project() -> tempfile::TempDir {
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(dir.path().join("Game.uproject"), "{}").unwrap();
    let m = dir.path().join("Source/Game");
    std::fs::create_dir_all(m.join("Public")).unwrap();
    std::fs::create_dir_all(m.join("Private")).unwrap();
    std::fs::write(m.join("Public/NetActor.h"), HEADER).unwrap();
    std::fs::write(m.join("Private/NetActor.cpp"), SOURCE).unwrap();
    dir
}

fn read(dir: &tempfile::TempDir, rel: &str) -> String {
    std::fs::read_to_string(dir.path().join(rel)).unwrap()
}

#[test]
fn code_replicate_adds_property_lifetime_props_and_onrep() {
    let home = tempfile::tempdir().unwrap();
    let proj = cpp_project();
    let h = "Source/Game/Public/NetActor.h";
    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &["code", "replicate", h, "Health", "float", "--notify"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["class"], "ANetActor");
    let header = read(&proj, h);
    assert!(
        header.contains("UPROPERTY(ReplicatedUsing = OnRep_Health)\n\tfloat Health;"),
        "{header}"
    );
    assert!(
        header.contains("UFUNCTION()\n\tvoid OnRep_Health();"),
        "{header}"
    );
    assert!(header.contains("virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;"), "{header}");
    assert!(
        header.trim_end().ends_with("};"),
        "the class still closes: {header}"
    );
    let source = read(&proj, "Source/Game/Private/NetActor.cpp");
    assert!(
        source.contains("#include \"NetActor.h\"\n#include \"Net/UnrealNetwork.h\""),
        "{source}"
    );
    assert!(source.contains("void ANetActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const\n{\n\tSuper::GetLifetimeReplicatedProps(OutLifetimeProps);\n\tDOREPLIFETIME(ANetActor, Health);\n}"), "{source}");
    assert!(
        source.contains("void ANetActor::OnRep_Health()\n{\n}"),
        "{source}"
    );

    // A second property joins the existing GetLifetimeReplicatedProps; the declaration is not repeated.
    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &[
            "code",
            "replicate",
            h,
            "Ammo",
            "int32",
            "--condition",
            "OwnerOnly",
        ],
    );
    assert_eq!(code, 0, "{v}");
    let header = read(&proj, h);
    assert_eq!(
        header.matches("GetLifetimeReplicatedProps").count(),
        1,
        "{header}"
    );
    assert!(
        header.contains("UPROPERTY(Replicated)\n\tint32 Ammo;"),
        "{header}"
    );
    let source = read(&proj, "Source/Game/Private/NetActor.cpp");
    assert!(source.contains("\tDOREPLIFETIME(ANetActor, Health);\n\tDOREPLIFETIME_CONDITION(ANetActor, Ammo, COND_OwnerOnly);\n}"), "{source}");
    assert_eq!(source.matches("Net/UnrealNetwork.h").count(), 1);

    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &["code", "replicate", h, "Health", "float"],
    );
    assert_ne!(code, 0);
    assert_eq!(v["error"]["code"], "code.exists");
}

#[test]
fn code_rpc_adds_declaration_and_implementation() {
    let home = tempfile::tempdir().unwrap();
    let proj = cpp_project();
    let h = "Source/Game/Public/NetActor.h";
    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &[
            "code",
            "rpc",
            h,
            "ServerFire",
            "--type",
            "server",
            "--reliable",
            "--validate",
            "--param",
            "Power:float",
        ],
    );
    assert_eq!(code, 0, "{v}");
    let header = read(&proj, h);
    assert!(
        header.contains(
            "UFUNCTION(Server, Reliable, WithValidation)\n\tvoid ServerFire(float Power);"
        ),
        "{header}"
    );
    let source = read(&proj, "Source/Game/Private/NetActor.cpp");
    assert!(
        source.contains("void ANetActor::ServerFire_Implementation(float Power)\n{\n}"),
        "{source}"
    );
    assert!(
        source.contains("bool ANetActor::ServerFire_Validate(float Power)\n{\n\treturn true;\n}"),
        "{source}"
    );

    run_json(
        home.path(),
        proj.path(),
        &["code", "rpc", h, "MulticastBoom", "--type", "multicast"],
    );
    let header = read(&proj, h);
    assert!(
        header.contains("UFUNCTION(NetMulticast, Unreliable)\n\tvoid MulticastBoom();"),
        "{header}"
    );
}

#[test]
fn code_net_dry_run_changes_nothing() {
    let home = tempfile::tempdir().unwrap();
    let proj = cpp_project();
    let h = "Source/Game/Public/NetActor.h";
    let (code, v) = run_json(
        home.path(),
        proj.path(),
        &["--dry-run", "code", "replicate", h, "Health", "float"],
    );
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["dryRun"], true);
    assert_eq!(read(&proj, h), HEADER);
    assert_eq!(read(&proj, "Source/Game/Private/NetActor.cpp"), SOURCE);
}

#[test]
fn net_commands_map_to_tools() {
    let home = tempfile::tempdir().unwrap();
    let m = echo(&["net_assert", "net_emulate"]);
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, serde_json::Value)> = vec![
        (
            vec!["net", "assert", "BP_Box", "Count"],
            json!({"actor": "BP_Box", "property": "Count"}),
        ),
        (
            vec![
                "net",
                "assert",
                "BP_Box",
                "Count",
                "--expect",
                "5",
                "--timeout",
                "2",
            ],
            json!({"actor": "BP_Box", "property": "Count", "expect": "5", "timeoutSeconds": 2.0}),
        ),
        (
            vec![
                "net",
                "emulate",
                "--latency",
                "200",
                "--jitter",
                "20",
                "--loss",
                "5",
            ],
            json!({"latencyMs": 200, "jitterMs": 20, "lossPercent": 5}),
        ),
        (vec!["net", "emulate", "--off"], json!({"off": true})),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

#[test]
fn net_mismatch_exits_4() {
    let home = tempfile::tempdir().unwrap();
    let m = MockBuilder::new()
        .tool("net_assert", |_| {
            Err(json!({"code": "net.mismatch", "message": "Count differs", "details": [{"id": "server", "value": "5"}, {"id": "client:1", "value": "0"}]}))
        })
        .start();
    write_instance(home.path(), &m, "p");
    let (code, v) = run_json(
        home.path(),
        home.path(),
        &["net", "assert", "BP_Box", "Count"],
    );
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "net.mismatch");
    assert_eq!(v["error"]["details"][1]["value"], "0");
}

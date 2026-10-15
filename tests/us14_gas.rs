mod support;

use std::path::Path;

use serde_json::{Value, json};
use support::mock_bridge::MockBuilder;
use support::{run_json, write_instance};

const BUILD_CS: &str = "using UnrealBuildTool;\n\npublic class Foo : ModuleRules\n{\n    public Foo(ReadOnlyTargetRules Target) : base(Target)\n    {\n        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n        PublicDependencyModuleNames.AddRange(new string[] { \"Core\", \"CoreUObject\", \"Engine\" });\n    }\n}\n";

fn project(build_cs: &str) -> tempfile::TempDir {
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(
        dir.path().join("Foo.uproject"),
        "{\n\t\"FileVersion\": 3,\n\t\"EngineAssociation\": \"5.7\"\n}\n",
    )
    .unwrap();
    std::fs::create_dir_all(dir.path().join("Source/Foo")).unwrap();
    std::fs::write(dir.path().join("Source/Foo/Foo.Build.cs"), build_cs).unwrap();
    dir
}

fn read(p: &Path, rel: &str) -> String {
    std::fs::read_to_string(p.join(rel)).unwrap()
}

#[test]
fn enable_adds_the_plugin_and_module_dependencies_once() {
    let home = tempfile::tempdir().unwrap();
    let p = project(BUILD_CS);
    let (code, v) = run_json(home.path(), p.path(), &["--dry-run", "gas", "enable"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(
        read(p.path(), "Source/Foo/Foo.Build.cs"),
        BUILD_CS,
        "dry-run wrote"
    );
    assert_eq!(
        v["data"]["modules"],
        json!(["GameplayAbilities", "GameplayTags", "GameplayTasks"])
    );

    let (code, v) = run_json(home.path(), p.path(), &["gas", "enable"]);
    assert_eq!(code, 0, "{v}");
    let cs = read(p.path(), "Source/Foo/Foo.Build.cs");
    assert!(
        cs.contains("{ \"Core\", \"CoreUObject\", \"Engine\", \"GameplayAbilities\", \"GameplayTags\", \"GameplayTasks\" }"),
        "{cs}"
    );
    let up: Value = serde_json::from_str(&read(p.path(), "Foo.uproject")).unwrap();
    assert_eq!(
        up["Plugins"],
        json!([{"Name": "GameplayAbilities", "Enabled": true}])
    );

    let (code, v) = run_json(home.path(), p.path(), &["gas", "enable"]);
    assert_eq!(code, 0, "{v}");
    assert_eq!(v["data"]["changed"], json!([]));
    assert_eq!(read(p.path(), "Source/Foo/Foo.Build.cs"), cs);
}

#[test]
fn enable_adds_a_dependency_list_when_the_module_has_none() {
    let home = tempfile::tempdir().unwrap();
    let p = project(
        "using UnrealBuildTool;\n\npublic class Foo : ModuleRules\n{\n\tpublic Foo(ReadOnlyTargetRules Target) : base(Target)\n\t{\n\t}\n}\n",
    );
    let (code, v) = run_json(home.path(), p.path(), &["gas", "enable"]);
    assert_eq!(code, 0, "{v}");
    let cs = read(p.path(), "Source/Foo/Foo.Build.cs");
    assert!(
        cs.contains("base(Target)\n\t{\n\t\tPublicDependencyModuleNames.AddRange(new string[] { \"GameplayAbilities\", \"GameplayTags\", \"GameplayTasks\" });\n\t}"),
        "{cs}"
    );
}

#[test]
fn attribute_set_generates_accessors_replication_and_defaults() {
    let home = tempfile::tempdir().unwrap();
    let p = project(BUILD_CS);
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &[
            "gas",
            "attribute-set",
            "HeroAttributes",
            "--attr",
            "Health=100",
            "--attr",
            "Mana",
        ],
    );
    assert_eq!(code, 0, "{v}");
    let h = read(p.path(), "Source/Foo/HeroAttributes.h");
    let c = read(p.path(), "Source/Foo/HeroAttributes.cpp");
    for want in [
        "#include \"AttributeSet.h\"",
        "class FOO_API UHeroAttributes : public UAttributeSet",
        "UPROPERTY(BlueprintReadOnly, Category = \"Attributes\", ReplicatedUsing = OnRep_Health)",
        "FGameplayAttributeData Health;",
        "GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UHeroAttributes, Health)",
        "GAMEPLAYATTRIBUTE_VALUE_INITTER(Mana)",
        "void OnRep_Mana(const FGameplayAttributeData& OldMana);",
    ] {
        assert!(h.contains(want), "{want} missing:\n{h}");
    }
    for want in [
        "InitHealth(100.f);",
        "InitMana(0.f);",
        "DOREPLIFETIME_CONDITION_NOTIFY(UHeroAttributes, Health, COND_None, REPNOTIFY_Always);",
        "GAMEPLAYATTRIBUTE_REPNOTIFY(UHeroAttributes, Mana, OldMana);",
    ] {
        assert!(c.contains(want), "{want} missing:\n{c}");
    }
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &["gas", "attribute-set", "HeroAttributes", "--attr", "Health"],
    );
    assert_eq!(code, 2, "{v}");
    assert_eq!(v["error"]["code"], "code.exists");
}

#[test]
fn attribute_set_without_replication_and_bad_names() {
    let home = tempfile::tempdir().unwrap();
    let p = project(BUILD_CS);
    let (code, v) = run_json(
        home.path(),
        p.path(),
        &[
            "--dry-run",
            "gas",
            "attribute-set",
            "Stats",
            "--attr",
            "Speed=1.5",
            "--no-replicate",
        ],
    );
    assert_eq!(code, 0, "{v}");
    let h = v["data"]["headerText"].as_str().unwrap();
    let c = v["data"]["sourceText"].as_str().unwrap();
    assert!(
        h.contains("UPROPERTY(BlueprintReadOnly, Category = \"Attributes\")\n"),
        "{h}"
    );
    assert!(
        !h.contains("OnRep_") && !c.contains("DOREPLIFETIME"),
        "{h}\n{c}"
    );
    assert!(c.contains("InitSpeed(1.5f);"), "{c}");
    assert!(!p.path().join("Source/Foo/Stats.h").exists());

    for bad in ["1Health", "He alth", "Health=abc"] {
        let (code, v) = run_json(
            home.path(),
            p.path(),
            &["gas", "attribute-set", "Stats", "--attr", bad],
        );
        assert_eq!(code, 2, "{bad}: {v}");
    }
}

fn echo(tools: &[&str]) -> support::mock_bridge::MockBridge {
    let mut b = MockBuilder::new();
    for t in tools {
        b = b.tool(t, |a| Ok(a.clone()));
    }
    b.start()
}

#[test]
fn asset_and_runtime_commands_reach_their_tools() {
    let home = tempfile::tempdir().unwrap();
    let m = echo(&[
        "gas_effect_create",
        "gas_ability_create",
        "gas_cue_create",
        "gas_runtime",
        "gas_component_add",
        "tags_list",
        "tags_add",
        "tags_rename",
        "tags_remove",
    ]);
    write_instance(home.path(), &m, "p");
    let cases: Vec<(Vec<&str>, Value)> = vec![
        (
            vec![
                "gas",
                "effect",
                "/Game/GE_Hit",
                "--duration",
                "3",
                "--period",
                "1",
                "--modifier",
                "HeroAttributes.Health:add:-10",
                "--modifier",
                "HeroAttributes.Mana:multiply:0.5",
                "--stacking",
                "target",
                "--stack-limit",
                "3",
                "--grant-tag",
                "State.Hit",
            ],
            json!({"path": "/Game/GE_Hit", "duration": "3", "period": 1.0,
                "modifiers": [
                    {"attribute": "HeroAttributes.Health", "op": "add", "magnitude": -10.0},
                    {"attribute": "HeroAttributes.Mana", "op": "multiply", "magnitude": 0.5}],
                "stacking": "target", "stackLimit": 3, "grantedTags": ["State.Hit"]}),
        ),
        (
            vec!["gas", "effect", "/Game/GE_Plain"],
            json!({"path": "/Game/GE_Plain"}),
        ),
        (
            vec![
                "gas",
                "ability",
                "/Game/GA_Fire",
                "--effect",
                "/Game/GE_Hit",
                "--cooldown",
                "/Game/GE_Cd",
                "--tag",
                "Ability.Fire",
            ],
            json!({"path": "/Game/GA_Fire", "effect": "/Game/GE_Hit", "cooldown": "/Game/GE_Cd", "tags": ["Ability.Fire"]}),
        ),
        (
            vec![
                "gas",
                "cue",
                "/Game/GC_Hit",
                "--tag",
                "GameplayCue.Hit",
                "--actor",
            ],
            json!({"path": "/Game/GC_Hit", "tag": "GameplayCue.Hit", "kind": "actor"}),
        ),
        (
            vec![
                "gas",
                "give",
                "player",
                "/Game/GA_Fire",
                "--level",
                "2",
                "--world",
                "server",
            ],
            json!({"action": "give", "actor": "player", "ability": "/Game/GA_Fire", "level": 2.0, "world": "server"}),
        ),
        (
            vec!["gas", "activate", "player", "/Game/GA_Fire"],
            json!({"action": "activate", "actor": "player", "ability": "/Game/GA_Fire"}),
        ),
        (
            vec!["gas", "apply", "Dummy", "/Game/GE_Hit"],
            json!({"action": "apply", "actor": "Dummy", "effect": "/Game/GE_Hit"}),
        ),
        (
            vec![
                "gas",
                "component",
                "/Game/BP_Hero",
                "--attribute-set",
                "HeroAttributes",
            ],
            json!({"path": "/Game/BP_Hero", "attributeSets": ["HeroAttributes"]}),
        ),
        (
            vec!["gas", "state", "Dummy"],
            json!({"action": "state", "actor": "Dummy"}),
        ),
        (
            vec!["tags", "list", "--filter", "State"],
            json!({"filter": "State"}),
        ),
        (
            vec!["tags", "add", "State.Hit", "--comment", "hit"],
            json!({"tag": "State.Hit", "comment": "hit"}),
        ),
        (
            vec!["tags", "rename", "State.Hit", "State.Damaged"],
            json!({"from": "State.Hit", "to": "State.Damaged"}),
        ),
        (
            vec!["tags", "remove", "State.Hit"],
            json!({"tag": "State.Hit"}),
        ),
    ];
    for (args, expected) in cases {
        let (code, v) = run_json(home.path(), home.path(), &args);
        assert_eq!(code, 0, "{args:?}: {v}");
        assert_eq!(v["data"], expected, "{args:?}");
    }
}

#[test]
fn malformed_modifiers_are_usage_errors() {
    let home = tempfile::tempdir().unwrap();
    for bad in [
        "Health:add:-10",
        "Set.Health:pow:2",
        "Set.Health:add:x",
        "Set.Health",
    ] {
        let (code, v) = run_json(
            home.path(),
            home.path(),
            &["gas", "effect", "/Game/GE", "--modifier", bad],
        );
        assert_eq!(code, 2, "{bad}: {v}");
        assert_eq!(v["error"]["code"], "gas.bad_modifier", "{bad}");
    }
}

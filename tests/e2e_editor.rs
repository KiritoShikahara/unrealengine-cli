//! End-to-end tests against a running TestProject editor.
//! Run with `scripts/e2e.sh` (sets UNREALENGINE_CLI_E2E=1, opens the editor, runs single-threaded).

use std::path::PathBuf;
use std::sync::OnceLock;

use assert_cmd::Command;
use serde_json::{Value, json};

fn enabled() -> bool {
    if std::env::var("UNREALENGINE_CLI_E2E").as_deref() == Ok("1") {
        return true;
    }
    eprintln!(
        "skipped: set UNREALENGINE_CLI_E2E=1 with the TestProject editor running (scripts/e2e.sh)"
    );
    false
}

fn project() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("TestProject")
}

fn run_id() -> &'static str {
    static ID: OnceLock<String> = OnceLock::new();
    ID.get_or_init(|| {
        let n = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs();
        format!("R{n}")
    })
}

fn root(name: &str) -> String {
    format!("/Game/E2E/{}/{name}", run_id())
}

fn cli(args: &[&str]) -> (i32, Value) {
    let out = Command::cargo_bin("unrealengine-cli")
        .unwrap()
        .current_dir(project())
        .env("UNREALENGINE_CLI_KEEP_ARGS", "1")
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

fn call(tool: &str, args: Value) -> Value {
    ok(&["tool", "call", tool, "--args", &args.to_string()])
}

fn fails(tool: &str, args: Value) -> (i32, String) {
    let (code, v) = cli(&["tool", "call", tool, "--args", &args.to_string()]);
    assert_ne!(code, 0, "{tool} should fail: {v}");
    (
        code,
        v["error"]["code"].as_str().unwrap_or_default().to_string(),
    )
}

#[test]
fn e2e_01_connect_and_catalog() {
    if !enabled() {
        return;
    }
    let ping = ok(&["ping"]);
    assert_eq!(ping["pong"], true);
    let list = ok(&["tool", "list"]);
    assert_eq!(list["source"], "editor");
    assert!(list["count"].as_u64().unwrap() >= 100, "{}", list["count"]);
    let bundled: Vec<Value> = serde_json::from_str(include_str!("../tools/catalog.json")).unwrap();
    let live: Vec<String> = list["tools"]
        .as_array()
        .unwrap()
        .iter()
        .map(|t| t["name"].as_str().unwrap().to_string())
        .collect();
    let mut bundled_names: Vec<String> = bundled
        .iter()
        .map(|t| t["name"].as_str().unwrap().to_string())
        .collect();
    bundled_names.sort();
    let mut live_sorted = live.clone();
    live_sorted.sort();
    assert_eq!(
        bundled_names, live_sorted,
        "tools/catalog.json is stale: run scripts/gen-catalog.sh"
    );
}

#[test]
fn e2e_02_level_actor_property_undo() {
    if !enabled() {
        return;
    }
    let level = root("L_Test");
    ok(&["level", "new", &level]);
    let a = ok(&[
        "level",
        "actor",
        "spawn",
        "PointLight",
        "--location",
        "100,200,300",
        "--label",
        "E2E_Cube",
    ]);
    assert_eq!(a["label"], "E2E_Cube");
    assert_eq!(a["location"]["x"], 100.0);
    ok(&[
        "level",
        "actor",
        "transform",
        "E2E_Cube",
        "--scale",
        "2,2,2",
    ]);
    let dup = ok(&[
        "level",
        "actor",
        "duplicate",
        "E2E_Cube",
        "--offset",
        "0,500,0",
        "--new-label",
        "E2E_Cube2",
    ]);
    assert_eq!(dup["label"], "E2E_Cube2");
    ok(&["level", "actor", "attach", "E2E_Cube2", "E2E_Cube"]);
    let list = ok(&["level", "actor", "list", "--name", "E2E_"]);
    assert_eq!(list.as_array().unwrap().len(), 2);

    ok(&[
        "prop",
        "set",
        "E2E_Cube.LightComponent0",
        "Intensity",
        "1234",
    ]);
    let mesh = ok(&["prop", "get", "E2E_Cube.LightComponent0", "Intensity"]);
    assert_eq!(mesh["value"], 1234.0, "{mesh}");
    ok(&["prop", "set", "E2E_Cube", "Tags", "[\"Enemy\"]"]);
    ok(&[
        "prop", "array", "E2E_Cube", "Tags", "append", "--value", "Boss",
    ]);
    let tags = ok(&["prop", "get", "E2E_Cube", "Tags"]);
    assert_eq!(tags["value"], json!(["Enemy", "Boss"]));
    let (code, err) = fails("prop_get", json!({"path": "E2E_Cube", "property": "Tagz"}));
    assert_eq!((code, err.as_str()), (4, "prop.not_found"));

    let comp = ok(&[
        "prop",
        "add-component",
        "E2E_Cube",
        "PointLightComponent",
        "--name",
        "Lamp",
    ]);
    assert_eq!(comp["class"], "PointLightComponent");
    let comps = ok(&["prop", "components", "E2E_Cube"]);
    assert!(
        comps
            .as_array()
            .unwrap()
            .iter()
            .any(|c| c["class"] == "PointLightComponent")
    );

    ok(&["level", "actor", "destroy", "E2E_Cube2"]);
    call("editor_undo", json!({}));
    assert_eq!(
        ok(&["level", "actor", "list", "--name", "E2E_Cube2"])
            .as_array()
            .unwrap()
            .len(),
        1,
        "undo restores the actor"
    );
    let (code, err) = fails("actor_find", json!({"label": "Nope_Nothing"}));
    assert_eq!((code, err.as_str()), (4, "actor.not_found"));
    ok(&["level", "save"]);
}

#[test]
fn e2e_03_assets() {
    if !enabled() {
        return;
    }
    let m = root("M_Base");
    ok(&["asset", "create", &m, "--class", "Material"]);
    let (_, err) = fails("asset_create", json!({"path": m, "class": "Material"}));
    assert_eq!(err, "asset.exists");
    let copy = root("M_Copy");
    ok(&["asset", "duplicate", &m, &copy]);
    let moved = root("Moved/M_Moved");
    ok(&["asset", "rename", &copy, &moved]);
    let found = ok(&[
        "asset",
        "search",
        "M_Moved",
        "--path",
        &format!("/Game/E2E/{}", run_id()),
    ]);
    assert_eq!(found.as_array().unwrap().len(), 1);
    let mi = root("MI_Child");
    call("mi_create", json!({"path": mi, "parent": m}));
    ok(&[
        "asset",
        "save",
        "--folder",
        &format!("/Game/E2E/{}", run_id()),
    ]);
    let refs = ok(&["asset", "refs", &m]);
    assert!(
        refs["referencers"]
            .as_array()
            .unwrap()
            .iter()
            .any(|r| r.as_str().unwrap().ends_with("MI_Child"))
    );
    let (_, err) = fails("asset_delete", json!({"path": m}));
    assert_eq!(err, "asset.referenced");
    ok(&["asset", "delete", &moved]);
}

#[test]
fn e2e_04_material_graph() {
    if !enabled() {
        return;
    }
    let m = root("M_Graph");
    call("material_create", json!({"path": m}));
    let color = call(
        "material_node_add",
        json!({"path": m, "type": "VectorParameter", "properties": {"parameter_name": "Tint"}}),
    );
    let rough = call(
        "material_node_add",
        json!({"path": m, "type": "Constant", "properties": {"r": 0.4}}),
    );
    call(
        "material_connect",
        json!({"path": m, "from": color["name"], "property": "BaseColor"}),
    );
    call(
        "material_connect",
        json!({"path": m, "from": rough["name"], "property": "Roughness"}),
    );
    let info = call("material_recompile", json!({"path": m}));
    assert_eq!(info["expressions"], 2);
    let mi = root("MI_Graph");
    call("mi_create", json!({"path": mi, "parent": m}));
    call(
        "mi_set_param",
        json!({"path": mi, "name": "Tint", "value": [1, 0, 0, 1]}),
    );
    let (_, err) = fails(
        "mi_set_param",
        json!({"path": mi, "name": "Missing", "value": 1.0}),
    );
    assert_eq!(err, "material.param_not_found");
}

#[test]
fn e2e_05_input_and_data() {
    if !enabled() {
        return;
    }
    let ia = root("IA_Jump");
    call("input_action_create", json!({"path": ia}));
    let imc = root("IMC_Default");
    call("mapping_context_create", json!({"path": imc}));
    let m = call(
        "mapping_add",
        json!({"context": imc, "action": ia, "key": "SpaceBar"}),
    );
    assert_eq!(m["mappings"], 1);
    let (_, err) = fails(
        "mapping_add",
        json!({"context": imc, "action": ia, "key": "NotAKey"}),
    );
    assert_eq!(err, "input.bad_key");

    let st = root("S_Item");
    let s = call(
        "struct_create",
        json!({"path": st, "fields": [{"name": "Damage", "type": "int"}, {"name": "Label", "type": "string"}]}),
    );
    assert_eq!(s["fields"], json!(["Damage", "Label"]));
    let en = root("E_Rarity");
    let e = call(
        "enum_create",
        json!({"path": en, "values": ["Common", "Rare", "Epic"]}),
    );
    assert_eq!(e["values"], json!(["Common", "Rare", "Epic"]));
    let dt = root("DT_Items");
    call("datatable_create", json!({"path": dt, "rowStruct": st}));
    let rows = call("datatable_rows_get", json!({"path": dt}));
    assert_eq!(rows["rows"], json!([]));
    let da = root("DA_Settings");
    let d = call(
        "data_asset_create",
        json!({"path": da, "class": "PrimaryDataAsset"}),
    );
    assert_eq!(d["class"], "PrimaryDataAsset");
}

#[test]
fn e2e_06_umg() {
    if !enabled() {
        return;
    }
    let w = root("WBP_Menu");
    let c = call("widget_create", json!({"path": w}));
    assert_eq!(c["root"], "CanvasPanel");
    call(
        "widget_add",
        json!({"path": w, "class": "VerticalBox", "name": "Column"}),
    );
    call(
        "widget_add",
        json!({"path": w, "class": "TextBlock", "name": "Title", "parent": "Column"}),
    );
    call(
        "widget_add",
        json!({"path": w, "class": "Button", "name": "StartButton", "parent": "Column"}),
    );
    call(
        "widget_set_prop",
        json!({"path": w, "widget": "Title", "property": "Text", "value": "Hello"}),
    );
    call(
        "widget_bind",
        json!({"path": w, "widget": "StartButton", "event": "OnClicked"}),
    );
    let tree = call("widget_tree_get", json!({"path": w}));
    assert_eq!(tree["widgets"].as_array().unwrap().len(), 4);
    let (_, err) = fails(
        "widget_add",
        json!({"path": w, "class": "TextBlock", "name": "Title"}),
    );
    assert_eq!(err, "umg.name_taken");
    let compiled = ok(&["blueprint", "compile", &w]);
    assert_eq!(compiled["succeeded"], true, "{compiled}");
}

#[test]
fn e2e_07_animation() {
    if !enabled() {
        return;
    }
    let skel = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP_Skeleton";
    let idle = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Idle";
    let walk = "/Engine/Tutorial/SubEditors/TutorialAssets/Character/Tutorial_Walk_Fwd";
    let abp = root("ABP_Hero");
    call("animbp_create", json!({"path": abp, "skeleton": skel}));
    call(
        "statemachine_add",
        json!({"path": abp, "name": "Locomotion"}),
    );
    let s = call(
        "state_add",
        json!({"path": abp, "machine": "Locomotion", "name": "Idle", "sequence": idle}),
    );
    assert_eq!(s["entry"], true);
    call(
        "state_add",
        json!({"path": abp, "machine": "Locomotion", "name": "Walk"}),
    );
    call(
        "sequence_player_add",
        json!({"path": abp, "machine": "Locomotion", "state": "Walk", "sequence": walk}),
    );
    call(
        "transition_add",
        json!({"path": abp, "machine": "Locomotion", "from": "Idle", "to": "Walk", "automatic": true}),
    );
    let compiled = ok(&["blueprint", "compile", &abp]);
    assert_eq!(compiled["errors"], 0, "{compiled}");
    let mont = call(
        "montage_create",
        json!({"path": root("AM_Idle"), "sequence": idle}),
    );
    assert!(mont["length"].as_f64().unwrap() > 0.0);
    call(
        "blendspace_create",
        json!({"path": root("BS_Move"), "skeleton": skel}),
    );
}

#[test]
fn e2e_08_niagara_sequencer_audio_landscape() {
    if !enabled() {
        return;
    }
    let ns = root("NS_Sparks");
    call("niagara_system_create", json!({"path": ns}));
    let e = call(
        "emitter_add",
        json!({"system": ns, "emitter": "/Niagara/DefaultAssets/Templates/Emitters/Fountain"}),
    );
    assert_eq!(e["emitters"].as_array().unwrap().len(), 1);
    let emitter = e["emitters"][0].as_str().unwrap().to_string();
    let module = call(
        "niagara_module_add",
        json!({"system": ns, "emitter": emitter, "stage": "ParticleUpdate", "module": "/Niagara/Modules/Update/Forces/Drag"}),
    );
    assert_eq!(module["stage"], "ParticleUpdate");
    let (_, err) = fails(
        "niagara_module_add",
        json!({"system": ns, "emitter": "Nope", "stage": "ParticleUpdate", "module": "/Niagara/Modules/Update/Forces/Drag"}),
    );
    assert_eq!(err, "niagara.emitter_not_found");
    let set = call(
        "niagara_input_set",
        json!({"system": ns, "emitter": emitter, "stage": "ParticleUpdate", "module": module["module"], "input": "Drag", "value": 2.5}),
    );
    assert_eq!(set["type"], "float", "{set}");
    let (_, err) = fails(
        "niagara_input_set",
        json!({"system": ns, "emitter": emitter, "stage": "ParticleUpdate", "module": module["module"], "input": "Nope", "value": 1}),
    );
    assert_eq!(err, "niagara.input_not_found");
    let listed = call("niagara_emitters_list", json!({"system": ns}));
    assert_eq!(listed["emitters"], e["emitters"]);

    ok(&["level", "new", &root("L_Seq")]);
    ok(&[
        "level",
        "actor",
        "spawn",
        "CameraActor",
        "--label",
        "E2E_Cam",
    ]);
    let seq = root("LS_Intro");
    call("sequence_create", json!({"path": seq, "endFrame": 90}));
    let t = call(
        "sequence_track_add",
        json!({"path": seq, "track": "Transform", "actor": "E2E_Cam"}),
    );
    assert!(
        t["channels"]
            .as_array()
            .unwrap()
            .iter()
            .any(|c| c == "Location.X"),
        "{t}"
    );
    call(
        "sequence_key_add",
        json!({"path": seq, "actor": "E2E_Cam", "channel": "Location.X", "frame": 30, "value": 500.0}),
    );
    call("sequence_track_add", json!({"path": seq, "track": "Fade"}));
    call("soundcue_create", json!({"path": root("SC_Empty")}));

    let l = call(
        "landscape_create",
        json!({"components": 1, "label": "E2E_Land"}),
    );
    assert_eq!(l["size"], 64);
    let s = call(
        "landscape_sculpt",
        json!({"label": "E2E_Land", "center": [3200, 3200], "radius": 1000, "height": 300}),
    );
    assert!(s["vertices"].as_u64().unwrap() > 0);
    let layer = call(
        "landscape_layer_add",
        json!({"label": "E2E_Land", "layer": "Grass", "path": root("LI_Grass")}),
    );
    assert_eq!(layer["layer"], "Grass");
    let p = call(
        "landscape_paint",
        json!({"label": "E2E_Land", "layer": "Grass", "center": [3200, 3200], "radius": 1000, "strength": 1.0}),
    );
    assert!(p["vertices"].as_u64().unwrap() > 0, "{p}");
    assert!(p["centerWeight"].as_u64().unwrap() > 0, "{p}");
    let (_, err) = fails(
        "landscape_paint",
        json!({"label": "E2E_Land", "layer": "Nope", "center": [0, 0], "radius": 100}),
    );
    assert_eq!(err, "landscape.layer_not_found");
    let info = call("landscape_info", json!({}));
    assert!(
        info.as_array()
            .unwrap()
            .iter()
            .any(|x| x["label"] == "E2E_Land")
    );
}

#[test]
fn e2e_09_blueprint_pie_and_tests() {
    if !enabled() {
        return;
    }
    let bp = root("BP_Hello");
    // PointLight parent: under -nullrhi, spawning actors with mesh primitives crashes the engine.
    ok(&["blueprint", "create", &bp, "--parent", "PointLight"]);
    let ev = ok(&[
        "blueprint",
        "add-node",
        &bp,
        "--type",
        "Event",
        "--member",
        "BeginPlay",
    ]);
    let pr = ok(&[
        "blueprint",
        "add-node",
        &bp,
        "--type",
        "CallFunction",
        "--member",
        "PrintString",
    ]);
    let from = format!("{}.then", ev["id"].as_str().unwrap());
    let to = format!("{}.execute", pr["id"].as_str().unwrap());
    ok(&["blueprint", "connect", &bp, &from, &to]);
    ok(&[
        "blueprint",
        "set-pin",
        &bp,
        pr["id"].as_str().unwrap(),
        "InString",
        "E2E-HELLO-MARKER",
    ]);
    assert_eq!(ok(&["blueprint", "compile", &bp])["succeeded"], true);
    ok(&["blueprint", "save", &bp]);
    ok(&["level", "new", &root("L_Pie")]);
    if std::env::var("UNREALENGINE_CLI_E2E_RENDER").as_deref() == Ok("1") {
        let spawned = call("actor_spawn", json!({"actorClass": bp}));
        assert_eq!(spawned["class"], "BP_Hello_C", "{spawned}");
        ok(&["level", "save"]);
        let pie = ok(&[
            "pie",
            "run",
            "--seconds",
            "30",
            "--stop-on-log",
            "E2E-HELLO-MARKER",
        ]);
        assert_eq!(pie["endedReason"], "log", "{pie}");
        assert_eq!(pie["ran"], true);
        assert_eq!(ok(&["pie", "status"])["playing"], false);
    } else {
        // Spawning Blueprint actors under -nullrhi would crash the engine; the tool refuses instead.
        let (code, err) = fails("actor_spawn", json!({"actorClass": bp}));
        assert_eq!((code, err.as_str()), (4, "actor.unsafe_without_renderer"));
        eprintln!(
            "PIE skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1); -nullrhi crashes on play"
        );
    }

    let tests = ok(&["test", "list", "UnrealCliBridge.System"]);
    assert!(tests["count"].as_u64().unwrap() >= 3);
    let run = ok(&["test", "run", "UnrealCliBridge.System.Ping"]);
    assert_eq!(run["failed"], 0, "{run}");

    let job = call(
        "automation_run",
        json!({"filter": "UnrealCliBridge.System.Ping"}),
    );
    let id = job["jobId"].as_str().unwrap().to_string();
    for _ in 0..60 {
        let j = ok(&["jobs", "get", &id]);
        if j["state"] != "running" && j["state"] != "queued" {
            assert_eq!(j["state"], "succeeded", "{j}");
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }
    let logs = ok(&["jobs", "logs", &id]);
    assert!(
        logs["lines"]
            .as_array()
            .unwrap()
            .iter()
            .any(|l| l.as_str().unwrap().starts_with("PASS"))
    );
}

#[test]
fn e2e_10_python_and_dry_run() {
    if !enabled() {
        return;
    }
    let out = ok(&[
        "python",
        "exec",
        "import unreal; print(unreal.SystemLibrary.get_engine_version())",
    ]);
    assert!(out["output"].as_str().unwrap().starts_with("5."));
    let (code, v) = cli(&["python", "exec", "raise RuntimeError('e2e-boom')"]);
    assert_eq!(code, 4);
    assert!(v["error"]["message"].as_str().unwrap().contains("e2e-boom"));
    let path = root("BP_NeverCreated");
    let plan = ok(&["--dry-run", "blueprint", "create", &path]);
    assert_eq!(plan["dryRun"], true);
    let (_, v) = cli(&["blueprint", "get", &path]);
    assert_eq!(v["error"]["code"], "blueprint.not_found");
    let status = ok(&["code", "apply", "--plan"]);
    assert!(["none", "live", "rebuild"].contains(&status["action"].as_str().unwrap()));
}

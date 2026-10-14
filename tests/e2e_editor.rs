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
    let (_, err) = fails(
        "data_asset_create",
        json!({"path": da, "class": "PrimaryDataAsset"}),
    );
    assert_eq!(err, "data.abstract_class");
    let settings = root("BP_SettingsData");
    ok(&[
        "blueprint",
        "create",
        &settings,
        "--parent",
        "PrimaryDataAsset",
    ]);
    ok(&["blueprint", "save", &settings]);
    let d = call("data_asset_create", json!({"path": da, "class": settings}));
    assert_eq!(d["class"], "BP_SettingsData_C");
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
    let link = call(
        "niagara_input_set",
        json!({"system": ns, "emitter": emitter, "stage": "ParticleUpdate", "module": module["module"], "input": "Drag", "link": "Particles.NormalizedAge"}),
    );
    assert_eq!(link["mode"], "link", "{link}");
    let dynamic = call(
        "niagara_input_set",
        json!({"system": ns, "emitter": emitter, "stage": "ParticleUpdate", "module": module["module"], "input": "Drag", "dynamicInput": "/Niagara/DynamicInputs/UniformRange/UniformRangedFloat"}),
    );
    assert_eq!(dynamic["mode"], "dynamicInput", "{dynamic}");
    assert!(!dynamic["node"].as_str().unwrap().is_empty(), "{dynamic}");
    let back = call(
        "niagara_input_set",
        json!({"system": ns, "emitter": emitter, "stage": "ParticleUpdate", "module": module["module"], "input": "Drag", "value": 1.5}),
    );
    assert_eq!(back["mode"], "value", "{back}");
    let (_, err) = fails(
        "niagara_input_set",
        json!({"system": ns, "emitter": emitter, "stage": "ParticleUpdate", "module": module["module"], "input": "Drag"}),
    );
    assert_eq!(err, "niagara.no_value");
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

fn render_enabled() -> bool {
    enabled() && std::env::var("UNREALENGINE_CLI_E2E_RENDER").as_deref() == Ok("1")
}

fn dist(a: &Value, b: &Value) -> f64 {
    let d = |k: &str| a[k].as_f64().unwrap() - b[k].as_f64().unwrap();
    (d("x").powi(2) + d("y").powi(2) + d("z").powi(2)).sqrt()
}

#[test]
fn e2e_11_play_session_input_and_observation() {
    if !enabled() {
        return;
    }
    let (_, err) = fails("pie_input", json!({"key": "W"}));
    assert_eq!(err, "pie.not_running");
    if !render_enabled() {
        eprintln!(
            "play-session E2E skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)"
        );
        return;
    }
    ok(&[
        "level",
        "new",
        &root("L_Play"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let ia = root("IA_PlayJump");
    call(
        "input_action_create",
        json!({"path": ia, "valueType": "Boolean"}),
    );
    let started = call("pie_start", json!({}));
    assert!(!started["pawn"].as_str().unwrap().is_empty(), "{started}");
    let actors = call("pie_actors", json!({}));
    assert!(
        actors
            .as_array()
            .unwrap()
            .iter()
            .any(|a| a["player"] == true),
        "{actors}"
    );
    let before = call("pie_get", json!({"actor": "player"}));
    call("pie_input", json!({"key": "W", "seconds": 1.0}));
    let after = call("pie_get", json!({"actor": "player"}));
    assert!(
        dist(&before["location"], &after["location"]) > 50.0,
        "W did not move the pawn: {before} -> {after}"
    );
    let injected = call(
        "pie_input",
        json!({"action": ia, "value": true, "seconds": 0.2}),
    );
    assert_eq!(injected["seconds"], 0.2);
    let (_, err) = fails("pie_input", json!({"key": "NotAKey"}));
    assert_eq!(err, "pie.input_invalid");
    let waited = call("pie_wait", json!({"seconds": 0.2}));
    assert_eq!(waited["endedReason"], "duration");
    let shot = call("pie_screenshot", json!({}));
    assert!(shot["width"].as_u64().unwrap() > 0, "{shot}");
    assert_eq!(
        shot["blank"], false,
        "lit template level rendered blank: {shot}"
    );
    assert!(std::path::Path::new(shot["path"].as_str().unwrap()).is_file());
    call(
        "pie_call",
        json!({"actor": "player", "command": "SetActorHiddenInGame true"}),
    );
    let hidden = call(
        "pie_get",
        json!({"actor": "player", "properties": ["bHidden"]}),
    );
    assert_eq!(hidden["properties"]["bHidden"], "True", "{hidden}");
    let (_, err) = fails(
        "pie_call",
        json!({"actor": "player", "command": "NoSuchFunction"}),
    );
    assert_eq!(err, "pie.function_not_found");
    call("pie_exec", json!({"command": "slomo 1"}));
    ok(&["pie", "stop"]);
    assert_eq!(ok(&["pie", "status"])["playing"], false);
}

fn node(bp: &str, ty: &str, member: Option<&str>, extra: Value) -> Value {
    let mut args = json!({"path": bp, "type": ty});
    if let Some(m) = member {
        args["memberName"] = json!(m);
    }
    for (k, v) in extra.as_object().unwrap() {
        args[k] = v.clone();
    }
    call("bp_add_node", args)
}

fn pin_link(bp: &str, from: &Value, from_pin: &str, to: &Value, to_pin: &str) {
    let f = format!("{}.{from_pin}", from["id"].as_str().unwrap());
    let t = format!("{}.{to_pin}", to["id"].as_str().unwrap());
    ok(&["blueprint", "connect", bp, &f, &t]);
}

#[test]
fn e2e_12_blueprint_gameplay_nodes() {
    if !enabled() {
        return;
    }
    let bp = root("BP_Pickup");
    ok(&["blueprint", "create", &bp, "--parent", "Actor"]);
    call(
        "bp_add_component",
        json!({"path": bp, "class": "SphereComponent", "name": "Trigger"}),
    );
    ok(&["blueprint", "compile", &bp]);
    // Overlap event -> call an event dispatcher with an int payload.
    let overlap = ok(&[
        "blueprint",
        "add-node",
        &bp,
        "--type",
        "ComponentEvent",
        "--member",
        "Trigger",
        "--delegate",
        "OnComponentBeginOverlap",
    ]);
    call(
        "bp_add_event_dispatcher",
        json!({"path": bp, "name": "OnPicked", "inputs": [{"name": "Amount", "type": "int"}]}),
    );
    ok(&["blueprint", "compile", &bp]);
    let fire = node(&bp, "CallDispatcher", Some("OnPicked"), json!({}));
    pin_link(&bp, &overlap, "then", &fire, "execute");
    let bind = node(&bp, "BindDispatcher", Some("OnPicked"), json!({}));
    let handler = node(
        &bp,
        "CustomEvent",
        Some("HandlePicked"),
        json!({"delegate": "OnPicked"}),
    );
    assert!(
        handler["pins"]
            .as_array()
            .unwrap()
            .iter()
            .any(|p| p["name"] == "Amount"),
        "{handler}"
    );
    pin_link(&bp, &handler, "OutputDelegate", &bind, "Delegate");
    // Timeline driving a vector built with MakeStruct.
    let tl = call(
        "bp_add_timeline",
        json!({"path": bp, "name": "Bob", "length": 2.0, "loop": true, "autoPlay": true,
               "tracks": [{"name": "Height", "keys": [[0, 0], [1, 50], [2, 0]]}]}),
    );
    assert!(
        tl["pins"]
            .as_array()
            .unwrap()
            .iter()
            .any(|p| p["name"] == "Height"),
        "{tl}"
    );
    let make = node(&bp, "MakeStruct", Some("Vector"), json!({}));
    pin_link(&bp, &tl, "Height", &make, "Z");
    node(&bp, "BreakStruct", Some("HitResult"), json!({}));
    node(&bp, "SwitchEnum", Some("EMovementMode"), json!({}));
    for class in ["K2Node_SwitchInteger", "K2Node_Select", "K2Node_MakeArray"] {
        node(&bp, "Node", Some(class), json!({}));
    }
    let comp = node(&bp, "VariableGet", Some("Trigger"), json!({}));
    assert!(!comp["pins"].as_array().unwrap().is_empty(), "{comp}");
    // Input, spawning and UI nodes.
    let ia = root("IA_Interact");
    call(
        "input_action_create",
        json!({"path": ia, "valueType": "Boolean"}),
    );
    let input = node(&bp, "InputAction", Some(&ia), json!({}));
    assert!(
        input["pins"]
            .as_array()
            .unwrap()
            .iter()
            .any(|p| p["name"] == "Triggered"),
        "{input}"
    );
    let spawn = node(&bp, "SpawnActor", Some("PointLight"), json!({}));
    assert!(
        spawn["pins"]
            .as_array()
            .unwrap()
            .iter()
            .any(|p| p["name"] == "SpawnTransform"),
        "{spawn}"
    );
    let widget = root("WBP_Score");
    call("widget_create", json!({"path": widget}));
    let cw = node(&bp, "CreateWidget", Some(&widget), json!({}));
    assert!(
        cw["pins"]
            .as_array()
            .unwrap()
            .iter()
            .any(|p| p["name"] == "OwningPlayer"),
        "{cw}"
    );
    let (_, err) = fails(
        "bp_add_node",
        json!({"path": bp, "type": "ComponentEvent", "memberName": "Trigger", "delegate": "Nope"}),
    );
    assert_eq!(err, "blueprint.event_not_found");
    let (_, err) = fails(
        "bp_add_node",
        json!({"path": bp, "type": "Node", "memberName": "NotANode"}),
    );
    assert_eq!(err, "blueprint.node_class_not_found");
    let compiled = ok(&["blueprint", "compile", &bp]);
    assert_eq!(compiled["errors"], 0, "{compiled}");
    ok(&["blueprint", "save", &bp]);
}

#[test]
fn e2e_13_ai_behavior_tree_and_navigation() {
    if !enabled() {
        return;
    }
    let bb = root("BB_Enemy");
    let made = call(
        "blackboard_create",
        json!({"path": bb, "keys": [
            {"name": "Target", "type": "Object", "baseClass": "Actor"},
            {"name": "Destination", "type": "Vector"},
            {"name": "CanSee", "type": "Bool"}]}),
    );
    for key in ["Target", "Destination", "CanSee"] {
        assert!(
            made["keys"].as_array().unwrap().iter().any(|k| k == key),
            "{made}"
        );
    }
    call(
        "blackboard_add_key",
        json!({"path": bb, "name": "Alert", "type": "Float"}),
    );
    let (_, err) = fails(
        "blackboard_add_key",
        json!({"path": bb, "name": "X", "type": "Banana"}),
    );
    assert_eq!(err, "args.invalid");
    let bt = root("BT_Enemy");
    call(
        "behavior_tree_create",
        json!({"path": bt, "blackboard": bb}),
    );
    let sel = call(
        "bt_add_node",
        json!({"path": bt, "parent": "root", "kind": "composite", "class": "Selector"}),
    );
    let chase = call(
        "bt_add_node",
        json!({"path": bt, "parent": sel["id"], "kind": "composite", "class": "Sequence"}),
    );
    call(
        "bt_add_node",
        json!({"path": bt, "parent": chase["id"], "kind": "decorator", "class": "Blackboard",
               "properties": {"BlackboardKey": "CanSee"}}),
    );
    call(
        "bt_add_node",
        json!({"path": bt, "parent": chase["id"], "kind": "task", "class": "MoveTo",
               "properties": {"BlackboardKey": "Target", "AcceptableRadius": 120}}),
    );
    call(
        "bt_add_node",
        json!({"path": bt, "parent": sel["id"], "kind": "task", "class": "Wait",
               "properties": {"WaitTime": 2.5}}),
    );
    let (_, err) = fails(
        "bt_add_node",
        json!({"path": bt, "parent": "root", "kind": "task", "class": "Wait"}),
    );
    assert_eq!(err, "ai.invalid_parent");
    let (_, err) = fails(
        "bt_add_node",
        json!({"path": bt, "parent": sel["id"], "kind": "task", "class": "MoveTo",
               "properties": {"BlackboardKey": "NoSuchKey"}}),
    );
    assert_eq!(err, "args.invalid");
    let tree = call("bt_get", json!({"path": bt}));
    assert_eq!(tree["runnable"], true, "{tree}");
    let top = &tree["children"][0];
    assert_eq!(top["class"], "BTComposite_Selector", "{tree}");
    assert_eq!(
        top["children"][0]["class"], "BTComposite_Sequence",
        "{tree}"
    );
    assert_eq!(
        top["children"][0]["attached"][0]["kind"], "decorator",
        "{tree}"
    );
    assert_eq!(
        top["children"][0]["children"][0]["class"], "BTTask_MoveTo",
        "{tree}"
    );
    assert_eq!(top["children"][1]["class"], "BTTask_Wait", "{tree}");
    ok(&["asset", "save", &bt]);

    if !render_enabled() {
        eprintln!(
            "navigation E2E skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)"
        );
        return;
    }
    ok(&[
        "level",
        "new",
        &root("L_Nav"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    call(
        "nav_bounds_add",
        json!({"location": [0, 0, 0], "extent": [3000, 3000, 500]}),
    );
    let mut path = Value::Null;
    for _ in 0..40 {
        path = call(
            "nav_path",
            json!({"from": [0, 0, 100], "to": [800, 600, 100]}),
        );
        if path["valid"] == true {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(500));
    }
    assert_eq!(path["valid"], true, "{path}");
    assert!(path["length"].as_f64().unwrap() > 900.0, "{path}");
}

#[test]
fn e2e_14_umg_layout_and_animation() {
    if !enabled() {
        return;
    }
    let w = root("WBP_Hud");
    call("widget_create", json!({"path": w}));
    call(
        "widget_add",
        json!({"path": w, "class": "TextBlock", "name": "Score"}),
    );
    let laid = call(
        "widget_layout",
        json!({"path": w, "widget": "Score", "anchor": "TopRight",
               "position": [-220, 20], "size": [200, 40], "alignment": [0, 0], "zOrder": 2}),
    );
    let layout = laid["layout"].as_str().unwrap();
    assert!(layout.contains("Minimum=(X=1.000000,Y=0.000000)"), "{laid}");
    assert!(layout.contains("Left=-220"), "{laid}");
    call(
        "widget_add",
        json!({"path": w, "class": "VerticalBox", "name": "Menu"}),
    );
    call(
        "widget_layout",
        json!({"path": w, "widget": "Menu", "anchor": "Center", "alignment": [0.5, 0.5], "autoSize": true}),
    );
    call(
        "widget_add",
        json!({"path": w, "class": "Button", "name": "Start", "parent": "Menu"}),
    );
    let boxed = call(
        "widget_layout",
        json!({"path": w, "widget": "Start", "padding": [8, 4, 8, 4], "hAlign": "Fill", "fill": 1}),
    );
    assert!(
        boxed["layout"].as_str().unwrap().contains("HAlign_Fill"),
        "{boxed}"
    );
    let (_, err) = fails(
        "widget_layout",
        json!({"path": w, "widget": "Start", "anchor": "Center"}),
    );
    assert_eq!(err, "args.invalid");
    let fade = call(
        "widget_animation_add",
        json!({"path": w, "name": "FadeIn", "widget": "Score", "track": "opacity",
               "keys": [[0, 0], [0.5, 1]]}),
    );
    assert_eq!(fade["seconds"], 0.5, "{fade}");
    call(
        "widget_animation_add",
        json!({"path": w, "name": "FadeIn", "widget": "Score", "track": "translation",
               "keys": [[0, [0, -40]], [0.75, [0, 0]]]}),
    );
    let (_, err) = fails(
        "widget_animation_add",
        json!({"path": w, "name": "FadeIn", "widget": "Score", "track": "scale", "keys": [[0, 1]]}),
    );
    assert_eq!(err, "args.invalid");
    ok(&["blueprint", "compile", &w]);
    let construct = node(&w, "Event", Some("Construct"), json!({}));
    let anim = node(&w, "VariableGet", Some("FadeIn"), json!({}));
    let play = node(&w, "CallFunction", Some("PlayAnimation"), json!({}));
    pin_link(&w, &construct, "then", &play, "execute");
    pin_link(&w, &anim, "FadeIn", &play, "InAnimation");
    let compiled = ok(&["blueprint", "compile", &w]);
    assert_eq!(compiled["errors"], 0, "{compiled}");
    ok(&["blueprint", "save", &w]);
}

#[test]
fn e2e_15_blueprint_component_properties() {
    if !enabled() {
        return;
    }
    let bp = root("BP_Crate");
    ok(&["blueprint", "create", &bp, "--parent", "Character"]);
    let added = call(
        "bp_add_component",
        json!({"path": bp, "class": "StaticMeshComponent", "name": "Body"}),
    );
    assert_eq!(added["name"], "Body");
    let comp = format!("{bp}.Body");
    let set = ok(&[
        "prop",
        "set",
        &comp,
        "StaticMesh",
        "/Engine/BasicShapes/Cube",
    ]);
    assert_eq!(set["value"], "/Engine/BasicShapes/Cube.Cube", "{set}");
    // Inherited components are reachable by name too.
    let speed = ok(&[
        "prop",
        "set",
        &format!("{bp}.CharacterMovement"),
        "MaxWalkSpeed",
        "900",
    ]);
    assert_eq!(speed["value"], 900.0, "{speed}");
    let (_, v) = cli(&["prop", "get", &format!("{bp}.Nope"), "X"]);
    assert_eq!(v["error"]["code"], "component.not_found", "{v}");
    assert!(v["error"]["hint"].as_str().unwrap().contains("Body"), "{v}");
    let compiled = ok(&["blueprint", "compile", &bp]);
    assert_eq!(compiled["errors"], 0, "{compiled}");
}

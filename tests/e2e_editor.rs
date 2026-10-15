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
    // A first line ending in .py must not turn the code into a file path (issue #2).
    let out = ok(&[
        "python",
        "exec",
        "# run: unrealengine-cli python exec @Scripts/x.py\nprint('first-line-ok')",
    ]);
    assert!(
        out["output"].as_str().unwrap().contains("first-line-ok"),
        "{out}"
    );
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
    // Arrays of asset references take asset paths.
    let mat = root("M_Crate");
    call("material_create", json!({"path": mat}));
    let mats = ok(&[
        "prop",
        "set",
        &comp,
        "OverrideMaterials",
        &format!("[\"{mat}\"]"),
    ]);
    assert!(mats["value"].to_string().contains("M_Crate"), "{mats}");
    // Physics flags of a Blueprint template live in its BodyInstance.
    let phys = ok(&["prop", "set", &comp, "SimulatePhysics", "true"]);
    assert_eq!(phys["value"], true, "{phys}");
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
    // Relative file paths given to the editor are under the project, not the engine's Binaries folder.
    let probe = project().join("Saved/E2EImport");
    std::fs::create_dir_all(&probe).unwrap();
    std::fs::write(probe.join("note.txt"), "e2e").unwrap();
    let (_, err) = fails(
        "asset_import",
        json!({"files": ["Saved/E2EImport/missing.png"], "destination": root("Imports")}),
    );
    assert_eq!(err, "args.invalid");
    // Packaging output must be given and must stay out of the engine install.
    let (_, err) = fails("package_project", json!({"outputDir": "", "check": true}));
    assert_eq!(err, "args.invalid");
    let (_, err) = fails(
        "package_project",
        json!({"outputDir": "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64", "check": true}),
    );
    assert_eq!(err, "package.bad_output_dir");
    // Any engine install is refused, not only the running editor's (both 5.7 and 5.8 are installed here).
    for engine in ["UE_5.7", "UE_5.8"] {
        let (_, err) = fails(
            "package_project",
            json!({"outputDir": format!("C:/Program Files/Epic Games/{engine}/Engine/Binaries/Win64"), "check": true}),
        );
        assert_eq!(err, "package.bad_output_dir", "{engine}");
    }
    let (_, err) = fails(
        "package_project",
        json!({"outputDir": "Content/Build", "check": true}),
    );
    assert_eq!(err, "package.bad_output_dir");
    let checked = call(
        "package_project",
        json!({"outputDir": "Build/Windows", "check": true}),
    );
    assert_eq!(checked["started"], false);
    assert!(
        std::path::Path::new(checked["outputDir"].as_str().unwrap()).ends_with(
            std::path::Path::new("TestProject")
                .join("Build")
                .join("Windows")
        ),
        "{checked}"
    );
}

#[test]
fn e2e_16_level_blueprint() {
    if !enabled() {
        return;
    }
    let map = root("L_LevelScript");
    ok(&["level", "new", &map]);
    let got = ok(&["blueprint", "get", &map]);
    assert!(got.to_string().contains("EventGraph"), "{got}");
    let begin = node(&map, "Event", Some("BeginPlay"), json!({}));
    let say = node(&map, "CallFunction", Some("PrintString"), json!({}));
    pin_link(&map, &begin, "then", &say, "execute");
    ok(&[
        "blueprint",
        "set-pin",
        &map,
        say["id"].as_str().unwrap(),
        "InString",
        "LEVEL_BP_MARKER",
    ]);
    let compiled = ok(&["blueprint", "compile", &map]);
    assert_eq!(compiled["errors"], 0, "{compiled}");
    ok(&["blueprint", "save", &map]);
    ok(&["level", "open", &map]);
    let graph = ok(&["blueprint", "graph", &map]);
    assert!(
        graph.to_string().contains("LEVEL_BP_MARKER"),
        "saved with the map: {graph}"
    );
    if render_enabled() {
        let pie = ok(&[
            "pie",
            "run",
            "--seconds",
            "20",
            "--stop-on-log",
            "LEVEL_BP_MARKER",
        ]);
        assert_eq!(pie["endedReason"], "log", "{pie}");
    }
}

#[test]
fn e2e_17_menu_ui_clicks_in_play() {
    if !render_enabled() {
        return;
    }
    let w = root("WBP_Title");
    call("widget_create", json!({"path": w}));
    call(
        "widget_add",
        json!({"path": w, "class": "TextBlock", "name": "Status"}),
    );
    call(
        "widget_set_prop",
        json!({"path": w, "widget": "Status", "property": "Text", "value": "Press Start"}),
    );
    call(
        "widget_layout",
        json!({"path": w, "widget": "Status", "anchor": "Center", "alignment": [0.5, 0.5], "position": [0, -60], "autoSize": true}),
    );
    call(
        "widget_add",
        json!({"path": w, "class": "Button", "name": "Start"}),
    );
    call(
        "widget_layout",
        json!({"path": w, "widget": "Start", "anchor": "Center", "alignment": [0.5, 0.5], "size": [200, 60]}),
    );
    call(
        "widget_bind",
        json!({"path": w, "widget": "Start", "event": "OnClicked"}),
    );
    let graph = ok(&["blueprint", "graph", &w]);
    let clicked = graph["nodes"]
        .as_array()
        .unwrap()
        .iter()
        .find(|n| n["class"] == "K2Node_ComponentBoundEvent")
        .cloned()
        .unwrap_or_else(|| panic!("bound event missing: {graph}"));
    let status = node(&w, "VariableGet", Some("Status"), json!({}));
    let set_text = node(
        &w,
        "CallFunction",
        Some("SetText"),
        json!({"memberParent": "TextBlock"}),
    );
    pin_link(&w, &clicked, "then", &set_text, "execute");
    pin_link(&w, &status, "Status", &set_text, "self");
    ok(&[
        "blueprint",
        "set-pin",
        &w,
        set_text["id"].as_str().unwrap(),
        "InText",
        "STARTED",
    ]);
    let say = node(&w, "CallFunction", Some("PrintString"), json!({}));
    pin_link(&w, &set_text, "then", &say, "execute");
    ok(&[
        "blueprint",
        "set-pin",
        &w,
        say["id"].as_str().unwrap(),
        "InString",
        "MENU_STARTED",
    ]);
    assert_eq!(ok(&["blueprint", "compile", &w])["errors"], 0);
    ok(&["blueprint", "save", &w]);

    let map = root("L_Menu");
    ok(&[
        "level",
        "new",
        &map,
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let begin = node(&map, "Event", Some("BeginPlay"), json!({}));
    let pc = node(&map, "CallFunction", Some("GetPlayerController"), json!({}));
    let create = node(&map, "CreateWidget", Some(&w), json!({}));
    pin_link(&map, &begin, "then", &create, "execute");
    pin_link(&map, &pc, "ReturnValue", &create, "OwningPlayer");
    let show = node(
        &map,
        "CallFunction",
        Some("AddToViewport"),
        json!({"memberParent": "UserWidget"}),
    );
    pin_link(&map, &create, "then", &show, "execute");
    pin_link(&map, &create, "ReturnValue", &show, "self");
    assert_eq!(ok(&["blueprint", "compile", &map])["errors"], 0);
    ok(&["blueprint", "save", &map]);

    call("pie_start", json!({}));
    let text_of = |screen: &Value| -> String {
        screen
            .as_array()
            .unwrap()
            .iter()
            .flat_map(|u| u["widgets"].as_array().unwrap().clone())
            .find(|c| c["name"] == "Status")
            .map(|c| c["text"].as_str().unwrap_or("").to_string())
            .unwrap_or_default()
    };
    let before = call("pie_widgets", json!({}));
    assert_eq!(text_of(&before), "Press Start", "{before}");
    let (_, err) = fails("pie_click", json!({"widget": "Nope"}));
    assert_eq!(err, "pie.widget_not_found");
    let click = call("pie_click", json!({"widget": "Start"}));
    assert!(
        click["log"].to_string().contains("MENU_STARTED"),
        "handler output: {click}"
    );
    let after = call("pie_widgets", json!({}));
    assert_eq!(text_of(&after), "STARTED", "{after}");
    ok(&["pie", "stop"]);
}

#[test]
fn e2e_18_blueprint_interfaces() {
    if !enabled() {
        return;
    }
    let bpi = root("BPI_Interact");
    ok(&["blueprint", "create", &bpi, "--interface"]);
    call(
        "bp_add_function",
        json!({"path": bpi, "name": "Interact", "inputs": [{"name": "Who", "type": "Actor"}]}),
    );
    assert_eq!(ok(&["blueprint", "compile", &bpi])["errors"], 0);
    ok(&["blueprint", "save", &bpi]);

    let door = root("BP_Door");
    ok(&["blueprint", "create", &door, "--parent", "Actor"]);
    call(
        "bp_implement_interface",
        json!({"path": door, "interface": bpi}),
    );
    ok(&["blueprint", "compile", &door]);
    let ev = node(&door, "Event", Some("Interact"), json!({}));
    assert!(
        ev["pins"]
            .as_array()
            .unwrap()
            .iter()
            .any(|p| p["name"] == "Who"),
        "{ev}"
    );
    let say = node(&door, "CallFunction", Some("PrintString"), json!({}));
    pin_link(&door, &ev, "then", &say, "execute");
    ok(&[
        "blueprint",
        "set-pin",
        &door,
        say["id"].as_str().unwrap(),
        "InString",
        "DOOR_INTERACTED",
    ]);
    assert_eq!(ok(&["blueprint", "compile", &door])["errors"], 0);
    ok(&["blueprint", "save", &door]);

    // A caller that does not implement the interface sends it as a message.
    let map = root("L_Interact");
    ok(&[
        "level",
        "new",
        &map,
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let begin = node(&map, "Event", Some("BeginPlay"), json!({}));
    let find = node(&map, "CallFunction", Some("GetActorOfClass"), json!({}));
    ok(&[
        "blueprint",
        "set-pin",
        &map,
        find["id"].as_str().unwrap(),
        "ActorClass",
        &door,
    ]);
    let msg = node(
        &map,
        "CallFunction",
        Some("Interact"),
        json!({"memberParent": bpi}),
    );
    assert_eq!(msg["class"], "K2Node_Message", "{msg}");
    pin_link(&map, &begin, "then", &find, "execute");
    pin_link(&map, &find, "then", &msg, "execute");
    pin_link(&map, &find, "ReturnValue", &msg, "self");
    assert_eq!(ok(&["blueprint", "compile", &map])["errors"], 0);
    ok(&["blueprint", "save", &map]);
    if render_enabled() {
        call("actor_spawn", json!({"actorClass": door, "label": "Door1"}));
        ok(&["level", "save"]);
        let pie = ok(&[
            "pie",
            "run",
            "--seconds",
            "20",
            "--stop-on-log",
            "DOOR_INTERACTED",
        ]);
        assert_eq!(pie["endedReason"], "log", "{pie}");
    }
}

#[test]
fn e2e_19_save_game_round_trip() {
    if !enabled() {
        return;
    }
    let save = root("BP_ProgressSave");
    ok(&["blueprint", "create", &save, "--parent", "SaveGame"]);
    call(
        "bp_add_variable",
        json!({"path": save, "name": "BestScore", "type": "int"}),
    );
    assert_eq!(ok(&["blueprint", "compile", &save])["errors"], 0);
    ok(&["blueprint", "save", &save]);

    let map = root("L_Save");
    ok(&["level", "new", &map]);
    let g = |ty: &str, m: Option<&str>, extra: Value| node(&map, ty, m, extra);
    let begin = g("Event", Some("BeginPlay"), json!({}));
    let create = g("CallFunction", Some("CreateSaveGameObject"), json!({}));
    ok(&[
        "blueprint",
        "set-pin",
        &map,
        create["id"].as_str().unwrap(),
        "SaveGameClass",
        &save,
    ]);
    let cast = g("Cast", Some(&save), json!({}));
    let set = g(
        "VariableSet",
        Some("BestScore"),
        json!({"memberParent": save}),
    );
    let write = g("CallFunction", Some("SaveGameToSlot"), json!({}));
    let read = g("CallFunction", Some("LoadGameFromSlot"), json!({}));
    let cast2 = g("Cast", Some(&save), json!({}));
    let get = g(
        "VariableGet",
        Some("BestScore"),
        json!({"memberParent": save}),
    );
    let text = g("CallFunction", Some("Conv_IntToString"), json!({}));
    let say = g("CallFunction", Some("PrintString"), json!({}));
    pin_link(&map, &begin, "then", &create, "execute");
    pin_link(&map, &create, "then", &cast, "execute");
    pin_link(&map, &create, "ReturnValue", &cast, "Object");
    pin_link(&map, &cast, "then", &set, "execute");
    let cast_out = cast["pins"]
        .as_array()
        .unwrap()
        .iter()
        .find(|p| p["name"].as_str().unwrap().starts_with("As"))
        .unwrap()["name"]
        .as_str()
        .unwrap()
        .to_string();
    pin_link(&map, &cast, &cast_out, &set, "self");
    ok(&[
        "blueprint",
        "set-pin",
        &map,
        set["id"].as_str().unwrap(),
        "BestScore",
        "4242",
    ]);
    pin_link(&map, &set, "then", &write, "execute");
    pin_link(&map, &create, "ReturnValue", &write, "SaveGameObject");
    ok(&[
        "blueprint",
        "set-pin",
        &map,
        write["id"].as_str().unwrap(),
        "SlotName",
        "E2ESlot",
    ]);
    pin_link(&map, &write, "then", &read, "execute");
    ok(&[
        "blueprint",
        "set-pin",
        &map,
        read["id"].as_str().unwrap(),
        "SlotName",
        "E2ESlot",
    ]);
    pin_link(&map, &read, "then", &cast2, "execute");
    pin_link(&map, &read, "ReturnValue", &cast2, "Object");
    pin_link(&map, &cast2, &cast_out, &get, "self");
    pin_link(&map, &get, "BestScore", &text, "InInt");
    pin_link(&map, &cast2, "then", &say, "execute");
    pin_link(&map, &text, "ReturnValue", &say, "InString");
    let compiled = ok(&["blueprint", "compile", &map]);
    assert_eq!(compiled["errors"], 0, "{compiled}");
    ok(&["blueprint", "save", &map]);
    if render_enabled() {
        let pie = ok(&["pie", "run", "--seconds", "20", "--stop-on-log", "4242"]);
        assert_eq!(pie["endedReason"], "log", "loaded value printed: {pie}");
    }
}

#[test]
fn e2e_20_parallel_clients() {
    if !enabled() {
        return;
    }
    // Several agents at once: every call reaches the editor and the instance file survives.
    let handles: Vec<_> = (0..24)
        .map(|i| {
            std::thread::spawn(move || {
                if i % 3 == 0 {
                    cli(&["ping"]).0
                } else {
                    cli(&["tool", "call", "actor_list", "--args", "{\"limit\":2}"]).0
                }
            })
        })
        .collect();
    let codes: Vec<i32> = handles.into_iter().map(|h| h.join().unwrap()).collect();
    assert!(codes.iter().all(|c| *c == 0), "exit codes: {codes:?}");
    assert_eq!(cli(&["ping"]).0, 0);
}

#[test]
fn e2e_21_multiplayer_pie() {
    if !enabled() {
        return;
    }
    let (_, err) = fails("pie_actors", json!({"world": "client:1"}));
    assert_eq!(err, "pie.not_running");
    if !render_enabled() {
        eprintln!(
            "multiplayer E2E skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)"
        );
        return;
    }
    ok(&[
        "level",
        "new",
        &root("L_Net"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let count_pcs = |world: &str| {
        call(
            "pie_actors",
            json!({"world": world, "classFilter": "PlayerController"}),
        )
        .as_array()
        .unwrap()
        .len()
    };

    // Listen server with 2 players: the host's world and one client world.
    call("pie_start", json!({"players": 2, "netMode": "listen"}));
    let worlds = call("pie_worlds", json!({}));
    let ids: Vec<&str> = worlds
        .as_array()
        .unwrap()
        .iter()
        .map(|w| w["id"].as_str().unwrap())
        .collect();
    assert_eq!(ids, ["server", "client:1"], "{worlds}");
    assert_eq!(worlds[0]["netMode"], "ListenServer", "{worlds}");
    assert_eq!(worlds[1]["netMode"], "Client", "{worlds}");
    assert_eq!(count_pcs("server"), 2, "the server sees every player");
    assert_eq!(
        count_pcs("client:1"),
        1,
        "a client sees only its own controller"
    );
    let own = call("pie_get", json!({"world": "client:1", "actor": "player"}));
    assert_eq!(own["role"], "AutonomousProxy", "{own}");
    let (_, err) = fails("pie_actors", json!({"world": "client:5"}));
    assert_eq!(err, "pie.world_not_found");

    // Input sent to client 1 moves only client 1's pawn.
    let client_before = call("pie_get", json!({"world": "client:1", "actor": "player"}));
    let host_before = call("pie_get", json!({"world": "server", "actor": "player"}));
    call(
        "pie_input",
        json!({"world": "client:1", "key": "W", "seconds": 1.0}),
    );
    let client_after = call("pie_get", json!({"world": "client:1", "actor": "player"}));
    let host_after = call("pie_get", json!({"world": "server", "actor": "player"}));
    assert!(
        dist(&client_before["location"], &client_after["location"]) > 50.0,
        "W did not move client 1's pawn: {client_before} -> {client_after}"
    );
    assert!(
        dist(&host_before["location"], &host_after["location"]) < 1.0,
        "the host's pawn moved: {host_before} -> {host_after}"
    );
    call("pie_stop", json!({}));

    // Client mode: a dedicated server in the editor process and 2 client worlds.
    call("pie_start", json!({"players": 2, "netMode": "client"}));
    let worlds = call("pie_worlds", json!({}));
    let modes: Vec<&str> = worlds
        .as_array()
        .unwrap()
        .iter()
        .map(|w| w["netMode"].as_str().unwrap())
        .collect();
    assert_eq!(modes, ["DedicatedServer", "Client", "Client"], "{worlds}");
    assert_eq!(count_pcs("server"), 2);
    call("pie_stop", json!({}));
}

#[test]
fn e2e_22_replication_and_rpc() {
    if !enabled() {
        return;
    }
    // A player controller with a RepNotify variable and a reliable Server RPC that sets it.
    let pc = root("BP_NetPC");
    ok(&["blueprint", "create", &pc, "--parent", "PlayerController"]);
    ok(&["blueprint", "add-variable", &pc, "Score", "--type", "int"]);
    let rep = ok(&["blueprint", "replicate", &pc, "Score", "--notify"]);
    assert_eq!(rep["notifyFunction"], "OnRep_Score", "{rep}");
    let graph = call("bp_graph_get", json!({"path": pc, "graph": "OnRep_Score"}));
    let entry = graph["nodes"]
        .as_array()
        .unwrap()
        .iter()
        .find(|n| n["class"] == "K2Node_FunctionEntry")
        .expect("OnRep_Score has an entry node")
        .clone();
    let print = node(
        &pc,
        "CallFunction",
        Some("PrintString"),
        json!({"graph": "OnRep_Score"}),
    );
    let f = format!("{}.then", entry["id"].as_str().unwrap());
    let t = format!("{}.execute", print["id"].as_str().unwrap());
    ok(&[
        "blueprint",
        "connect",
        &pc,
        &f,
        &t,
        "--graph",
        "OnRep_Score",
    ]);
    call(
        "bp_set_pin",
        json!({"path": pc, "graph": "OnRep_Score", "node": print["id"], "pin": "InString", "value": "e2e OnRep Score"}),
    );
    let event = node(&pc, "CustomEvent", Some("ServerSetScore"), json!({}));
    let rpc = ok(&[
        "blueprint",
        "rpc",
        &pc,
        "ServerSetScore",
        "--type",
        "server",
        "--reliable",
    ]);
    assert_eq!(rpc["type"], "server", "{rpc}");
    assert_eq!(rpc["reliable"], true, "{rpc}");
    // A custom event added since the last compile can be called right away.
    let caller = node(&pc, "CallFunction", Some("ServerSetScore"), json!({}));
    assert!(caller["id"].is_string(), "{caller}");
    let set = node(&pc, "VariableSet", Some("Score"), json!({}));
    pin_link(&pc, &event, "then", &set, "execute");
    call(
        "bp_set_pin",
        json!({"path": pc, "node": set["id"], "pin": "Score", "value": "7"}),
    );
    let compiled = ok(&["blueprint", "compile", &pc]);
    assert_eq!(compiled["errors"], 0, "{compiled}");
    ok(&["blueprint", "save", &pc]);
    let (_, err) = fails(
        "bp_set_rpc",
        json!({"path": pc, "event": "NoSuchEvent", "type": "server"}),
    );
    assert_eq!(err, "blueprint.event_not_found");

    if !render_enabled() {
        eprintln!(
            "replication PIE skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)"
        );
        return;
    }
    let gm = root("BP_NetGM");
    ok(&["blueprint", "create", &gm, "--parent", "GameModeBase"]);
    let pc_class = format!("{pc}.BP_NetPC_C");
    ok(&["prop", "set", &gm, "PlayerControllerClass", &pc_class]);
    ok(&["blueprint", "compile", &gm]);
    ok(&["blueprint", "save", &gm]);
    ok(&[
        "level",
        "new",
        &root("L_Rep"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let gm_class = format!("{gm}.BP_NetGM_C");
    ok(&["prop", "set", "WorldSettings", "DefaultGameMode", &gm_class]);
    call("pie_start", json!({"players": 2, "netMode": "listen"}));
    // Client 1 calls the Server RPC on its own controller; the server sets Score, which replicates back.
    // Replication can finish within the call, so read the log from before it.
    let since = ok(&["logs", "--limit", "1"])["nextSince"]
        .as_i64()
        .unwrap()
        .to_string();
    call(
        "pie_call",
        json!({"world": "client:1", "actor": "controller", "command": "ServerSetScore"}),
    );
    call("pie_wait", json!({"seconds": 1}));
    let logs = ok(&[
        "logs",
        "--since",
        &since,
        "--category",
        "LogBlueprintUserMessages",
    ]);
    assert!(
        logs["lines"].as_array().unwrap().iter().any(|l| l["text"]
            .as_str()
            .unwrap()
            .contains("Client 1: e2e OnRep Score")),
        "OnRep_Score did not run on client 1: {logs}"
    );
    let client = call(
        "pie_get",
        json!({"world": "client:1", "actor": "controller", "properties": ["Score"]}),
    );
    assert_eq!(client["properties"]["Score"], "7", "{client}");
    let host = call(
        "pie_get",
        json!({"world": "server", "actor": "controller", "properties": ["Score"]}),
    );
    assert_eq!(
        host["properties"]["Score"], "0",
        "the host's controller changed: {host}"
    );
    call("pie_stop", json!({}));
}

#[test]
fn e2e_23_net_assert_and_emulation() {
    if !render_enabled() {
        eprintln!("net E2E skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)");
        return;
    }
    // A replicated level actor with a replicated Count that two events set on the server.
    let bx = root("BP_NetBox");
    ok(&["blueprint", "create", &bx, "--parent", "Actor"]);
    ok(&["prop", "set", &bx, "bReplicates", "true"]);
    ok(&["prop", "set", &bx, "bAlwaysRelevant", "true"]);
    ok(&["blueprint", "add-variable", &bx, "Count", "--type", "int"]);
    ok(&["blueprint", "replicate", &bx, "Count"]);
    for (event, value) in [("Bump", "5"), ("Bump9", "9")] {
        let e = node(&bx, "CustomEvent", Some(event), json!({}));
        let set = node(&bx, "VariableSet", Some("Count"), json!({}));
        pin_link(&bx, &e, "then", &set, "execute");
        call(
            "bp_set_pin",
            json!({"path": bx, "node": set["id"], "pin": "Count", "value": value}),
        );
    }
    let compiled = ok(&["blueprint", "compile", &bx]);
    assert_eq!(compiled["errors"], 0, "{compiled}");
    ok(&["blueprint", "save", &bx]);
    ok(&[
        "level",
        "new",
        &root("L_Sync"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let spawned = call("actor_spawn", json!({"actorClass": bx}));
    let label = spawned["label"].as_str().unwrap().to_string();

    call("pie_start", json!({"players": 2, "netMode": "listen"}));
    let same = ok(&["net", "assert", &label, "Count"]);
    assert_eq!(same["worlds"].as_array().unwrap().len(), 2, "{same}");

    call(
        "pie_call",
        json!({"world": "server", "actor": label, "command": "Bump"}),
    );
    let five = ok(&[
        "net",
        "assert",
        &label,
        "Count",
        "--expect",
        "5",
        "--timeout",
        "3",
    ]);
    assert!(
        five["waitedSeconds"].as_f64().unwrap() < 1.0,
        "SC-002: {five}"
    );
    assert_eq!(five["worlds"][1]["value"], "5", "{five}");

    let (code, v) = cli(&[
        "net",
        "assert",
        &label,
        "Count",
        "--expect",
        "6",
        "--timeout",
        "1",
    ]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "net.mismatch");
    assert_eq!(v["error"]["details"]["worlds"][0]["value"], "5", "{v}");

    let lag = ok(&["net", "emulate", "--latency", "400"]);
    assert_eq!(lag["latencyMs"], 400, "{lag}");
    call(
        "pie_call",
        json!({"world": "server", "actor": label, "command": "Bump9"}),
    );
    let nine = ok(&[
        "net",
        "assert",
        &label,
        "Count",
        "--expect",
        "9",
        "--timeout",
        "5",
    ]);
    assert!(
        nine["waitedSeconds"].as_f64().unwrap() >= 0.3,
        "the simulated latency had no effect: {nine}"
    );
    ok(&["net", "emulate", "--off"]);
    call("pie_stop", json!({}));
}

#[test]
fn e2e_24_online_null() {
    if !render_enabled() {
        eprintln!("online E2E skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)");
        return;
    }
    // Two standalone players: the first hosts a LAN session through the Null subsystem, the second finds and joins it.
    call("pie_start", json!({"players": 2, "netMode": "standalone"}));
    let created = ok(&[
        "online",
        "session",
        "create",
        "--world",
        "standalone",
        "--max-players",
        "3",
    ]);
    assert_eq!(created["subsystem"], "NULL", "{created}");
    assert_eq!(created["maxPlayers"], 3, "{created}");
    let found = ok(&["online", "session", "find", "--world", "standalone:2"]);
    let results = found["results"].as_array().unwrap();
    assert!(
        results.iter().any(|r| r["id"] == created["id"]),
        "the hosted session was not found: {found}"
    );
    let index = results
        .iter()
        .position(|r| r["id"] == created["id"])
        .unwrap()
        .to_string();
    let joined = ok(&[
        "online",
        "session",
        "join",
        "--world",
        "standalone:2",
        "--index",
        &index,
    ]);
    assert!(joined["connectString"].as_str().is_some(), "{joined}");
    let (code, v) = cli(&[
        "online",
        "session",
        "join",
        "--world",
        "standalone:2",
        "--index",
        "99",
    ]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "online.no_result");
    ok(&["online", "session", "destroy", "--world", "standalone:2"]);
    ok(&["online", "session", "destroy", "--world", "standalone"]);
    let (code, v) = cli(&["online", "session", "destroy", "--world", "standalone"]);
    assert_eq!(code, 4, "destroying a missing session must fail: {v}");
    call("pie_stop", json!({}));
}

#[test]
fn e2e_25_gas() {
    if !render_enabled() {
        eprintln!("GAS E2E skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)");
        return;
    }
    // TestProject enables GameplayAbilities and has the C++ set UHeroAttributes (Health 100, MaxHealth 100, Mana 50),
    // generated with `gas attribute-set HeroAttributes --attr Health=100 --attr MaxHealth=100 --attr Mana=50`.
    for tag in ["E2E.Gas.Burning", "Ability.E2E.Hit", "GameplayCue.E2E.Hit"] {
        ok(&["tags", "add", tag]);
    }
    let tags = ok(&["tags", "list", "--filter", "E2E.Gas"]);
    assert_eq!(tags["tags"], json!(["E2E.Gas.Burning"]), "{tags}");

    let hit = root("GE_Hit");
    let burn = root("GE_Burn");
    let ability = root("GA_Hit");
    let made = ok(&[
        "gas",
        "effect",
        &hit,
        "--modifier",
        "HeroAttributes.Health:add:-10",
    ]);
    assert_eq!(made["modifiers"], 1, "{made}");
    ok(&[
        "gas",
        "effect",
        &burn,
        "--duration",
        "10",
        "--period",
        "1",
        "--modifier",
        "HeroAttributes.Mana:add:-2",
        "--grant-tag",
        "E2E.Gas.Burning",
        "--stacking",
        "target",
        "--stack-limit",
        "2",
    ]);
    ok(&[
        "gas",
        "ability",
        &ability,
        "--effect",
        &hit,
        "--tag",
        "Ability.E2E.Hit",
    ]);
    ok(&[
        "gas",
        "cue",
        &root("GC_Hit"),
        "--tag",
        "GameplayCue.E2E.Hit",
    ]);
    let (code, v) = cli(&[
        "gas",
        "effect",
        &root("GE_Bad"),
        "--modifier",
        "HeroAttributes.Stamina:add:1",
    ]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "gas.bad_attribute");

    let dummy = root("BP_GasDummy");
    ok(&["blueprint", "create", &dummy, "--parent", "Actor"]);
    let comp = ok(&[
        "gas",
        "component",
        &dummy,
        "--attribute-set",
        "HeroAttributes",
    ]);
    assert_eq!(comp["created"], true, "{comp}");
    ok(&[
        "level",
        "new",
        &root("L_Gas"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let spawned = call("actor_spawn", json!({"actorClass": dummy}));
    let label = spawned["label"].as_str().unwrap().to_string();
    let plain = call("actor_spawn", json!({"actorClass": "StaticMeshActor"}));

    call("pie_start", json!({}));
    let health = |s: &Value| {
        s["state"]["attributes"]["HeroAttributes.Health"]["current"]
            .as_f64()
            .unwrap()
    };
    let start = ok(&["gas", "state", &label]);
    assert_eq!(health(&start), 100.0, "{start}");
    let applied = ok(&["gas", "apply", &label, &hit]);
    assert_eq!(health(&applied), 90.0, "SC-004: {applied}");
    let given = ok(&["gas", "give", &label, &ability]);
    assert_eq!(
        given["state"]["abilities"].as_array().unwrap().len(),
        1,
        "{given}"
    );
    let activated = ok(&["gas", "activate", &label, &ability]);
    assert_eq!(activated["activated"], true, "{activated}");
    assert_eq!(health(&activated), 80.0, "{activated}");
    let burning = ok(&["gas", "apply", &label, &burn]);
    assert!(
        burning["state"]["tags"]
            .as_array()
            .unwrap()
            .contains(&json!("E2E.Gas.Burning")),
        "{burning}"
    );
    std::thread::sleep(std::time::Duration::from_millis(1500));
    let later = ok(&["gas", "state", &label]);
    let mana = later["state"]["attributes"]["HeroAttributes.Mana"]["current"]
        .as_f64()
        .unwrap();
    assert!(mana <= 46.0, "the periodic effect did not tick: {later}");
    let (code, v) = cli(&["gas", "state", plain["label"].as_str().unwrap()]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "gas.no_component");
    call("pie_stop", json!({}));
}

#[test]
fn e2e_29_visual_perf() {
    if !render_enabled() {
        eprintln!("visual E2E skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)");
        return;
    }
    ok(&[
        "level",
        "new",
        &root("L_Visual"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let name = format!("e2e-{}", run_id());
    call("pie_start", json!({}));
    std::thread::sleep(std::time::Duration::from_secs(2));
    let base = ok(&["visual", "baseline", &name]);
    assert!(base["width"].as_u64().unwrap() > 0, "{base}");
    std::thread::sleep(std::time::Duration::from_millis(500));
    let same = ok(&["visual", "compare", &name, "--threshold", "2"]);
    assert!(same["diffPercent"].as_f64().unwrap() <= 2.0, "{same}");

    let trace = ok(&["perf", "trace", "start"]);
    let perf = ok(&["perf", "measure", "--seconds", "3"]);
    ok(&["perf", "trace", "stop"]);
    let summary = ok(&[
        "perf",
        "trace",
        "summary",
        trace["file"].as_str().unwrap(),
        "--top",
        "5",
    ]);
    assert!(
        summary["timers"]
            .as_array()
            .unwrap()
            .iter()
            .any(|t| t["name"] == "Frame"),
        "{summary}"
    );
    assert!(perf["frames"].as_u64().unwrap() > 10, "{perf}");
    assert!(perf["frameMs"]["p95"].as_f64().unwrap() > 0.0, "{perf}");
    assert_eq!(perf["partial"], false, "{perf}");
    call("pie_stop", json!({}));

    // Without the sun and sky the same view is much darker.
    let lights = call("actor_list", json!({"classFilter": "Light"}));
    let mut labels: Vec<String> = Vec::new();
    for class in [
        "DirectionalLight",
        "SkyLight",
        "SkyAtmosphere",
        "ExponentialHeightFog",
    ] {
        let found = call("actor_list", json!({"classFilter": class}));
        for a in found.as_array().unwrap() {
            labels.push(a["label"].as_str().unwrap().to_string());
        }
    }
    assert!(!labels.is_empty(), "no lights in the template: {lights}");
    call("actor_destroy", json!({"labels": labels}));
    call("pie_start", json!({}));
    std::thread::sleep(std::time::Duration::from_secs(2));
    let (code, v) = cli(&["visual", "compare", &name, "--threshold", "2"]);
    call("pie_stop", json!({}));
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "visual.mismatch");
    assert!(
        v["error"]["details"]["diffPercent"].as_f64().unwrap() > 10.0,
        "{v}"
    );
}

#[test]
fn e2e_27_lighting() {
    if !render_enabled() {
        eprintln!("lighting E2E skipped: needs a rendering editor (UNREALENGINE_CLI_E2E_RENDER=1)");
        return;
    }
    ok(&[
        "level",
        "new",
        &root("L_Lighting"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let outdoor = ok(&["light", "preset", "outdoor"]);
    assert!(
        outdoor["removed"].as_array().unwrap().len() >= 3,
        "{outdoor}"
    );
    assert!(
        outdoor["spawned"]
            .as_array()
            .unwrap()
            .contains(&json!("Sun")),
        "{outdoor}"
    );
    let name = format!("e2e-light-{}", run_id());
    call("pie_start", json!({}));
    std::thread::sleep(std::time::Duration::from_secs(2));
    ok(&["visual", "baseline", &name]);
    call("pie_stop", json!({}));
    let night = ok(&["light", "preset", "night"]);
    assert!(
        night["removed"].as_array().unwrap().contains(&json!("Sun")),
        "{night}"
    );
    call("pie_start", json!({}));
    std::thread::sleep(std::time::Duration::from_secs(2));
    let (code, v) = cli(&["visual", "compare", &name, "--threshold", "5"]);
    call("pie_stop", json!({}));
    assert_eq!(code, 4, "night should look different from day: {v}");

    let settings = ok(&["render", "get"]);
    for key in ["lumen", "virtualShadowMaps", "nanite", "antiAliasing"] {
        assert!(settings["settings"][key].is_string(), "{settings}");
    }
    let (code, v) = cli(&["render", "set", "noSuchSetting", "1"]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "render.unknown_setting");

    // TestProject disables static lighting, so the build job ends with a clear failure.
    let (code, v) = cli(&["light", "build"]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "lighting.build_failed");
}

#[test]
fn e2e_26_ik_retarget() {
    if !enabled() {
        return;
    }
    let ch = "/Engine/Tutorial/SubEditors/TutorialAssets/Character";
    let mesh = format!("{ch}/TutorialTPP");
    let (src, tgt, rtg) = (root("IK_Source"), root("IK_Target"), root("RTG_Tutorial"));
    let rig = ok(&["anim", "ikrig", &src, "--mesh", &mesh]);
    assert_eq!(rig["retargetRoot"], "pelvis", "{rig}");
    assert!(rig["chains"].as_array().unwrap().len() >= 10, "{rig}");
    assert!(rig["solvers"].as_u64().unwrap() >= 1, "{rig}");
    ok(&["anim", "ikrig", &tgt, "--mesh", &mesh]);
    let map = ok(&[
        "anim",
        "retargeter",
        &rtg,
        "--source",
        &src,
        "--target",
        &tgt,
    ]);
    let mapping = map["mapping"].as_array().unwrap();
    assert!(mapping.iter().all(|m| m["source"] == m["target"]), "{map}");
    let out = ok(&[
        "anim",
        "retarget",
        &rtg,
        &format!("{ch}/Tutorial_Walk_Fwd"),
        "--folder",
        &root("Retargeted"),
    ]);
    let created = out["created"].as_array().unwrap();
    assert_eq!(created.len(), 1, "{out}");
    let asset = created[0].as_str().unwrap();
    assert!(asset.ends_with("Tutorial_Walk_Fwd_Retargeted"), "{out}");
    ok(&["asset", "deps", asset]);

    let cr = ok(&["anim", "controlrig", &root("CR_Tutorial"), "--mesh", &mesh]);
    assert!(cr["bones"].as_u64().unwrap() > 50, "{cr}");
    // TestProject enables PoseSearch.
    let ps = ok(&[
        "anim",
        "posesearch",
        &root("PSD_Tutorial"),
        "--skeleton",
        &format!("{ch}/TutorialTPP_Skeleton"),
        &format!("{ch}/Tutorial_Walk_Fwd"),
        &format!("{ch}/Tutorial_Idle"),
    ]);
    assert_eq!(ps["animations"].as_array().unwrap().len(), 2, "{ps}");
}

#[test]
fn e2e_28_audio_physics_pcg() {
    if !enabled() {
        return;
    }
    // MetaSound: a sine tone wired to the output with a set frequency.
    let ms = root("MS_Tone");
    let made = ok(&["audio", "metasound", "create", &ms]);
    let out = made["outputs"]
        .as_array()
        .unwrap()
        .iter()
        .map(|o| o.as_str().unwrap())
        .find(|o| o.contains("Audio"))
        .unwrap()
        .to_string();
    let sine = ok(&["audio", "metasound", "node", &ms, "UE.Sine.Audio"]);
    let id = sine["node"].as_str().unwrap().to_string();
    assert!(
        sine["inputs"]
            .as_array()
            .unwrap()
            .iter()
            .any(|i| i["name"] == "Frequency"),
        "{sine}"
    );
    ok(&[
        "audio",
        "metasound",
        "connect",
        &ms,
        &format!("{id}:Audio"),
        &format!("graph:{out}"),
    ]);
    let set = ok(&["audio", "metasound", "set", &ms, &id, "Frequency", "330"]);
    assert_eq!(set["type"], "Float", "{set}");
    let (code, v) = cli(&["audio", "metasound", "node", &ms, "UE.NoSuchNode.Audio"]);
    assert_eq!(code, 4, "{v}");
    assert_eq!(v["error"]["code"], "metasound.no_class");

    // Chaos: a destructible from two meshes.
    let gc = ok(&[
        "physics",
        "collection",
        &root("GC_Shapes"),
        "--mesh",
        "/Engine/BasicShapes/Cube",
        "--mesh",
        "/Engine/BasicShapes/Sphere",
    ]);
    assert_eq!(gc["meshes"], 2, "{gc}");

    // PCG: a 10 x 10 grid of cubes.
    ok(&[
        "level",
        "new",
        &root("L_PCG"),
        "--template",
        "/Engine/Maps/Templates/Template_Default",
    ]);
    let graph = root("PCG_Grid");
    ok(&["pcg", "create", &graph]);
    let grid = ok(&[
        "pcg",
        "node",
        &graph,
        "CreatePointsGrid",
        "--props",
        r#"{"grid_extents":[500,500,0],"cell_size":[100,100,100]}"#,
    ]);
    let spawner = ok(&[
        "pcg",
        "node",
        &graph,
        "StaticMeshSpawner",
        "--props",
        r#"{"mesh":"/Engine/BasicShapes/Cube"}"#,
    ]);
    let (g, s) = (
        grid["node"].as_str().unwrap(),
        spawner["node"].as_str().unwrap(),
    );
    ok(&[
        "pcg",
        "connect",
        &graph,
        &format!("{g}:Out"),
        &format!("{s}:In"),
    ]);
    ok(&["pcg", "connect", &graph, &format!("{s}:Out"), "output:Out"]);
    let done = ok(&["pcg", "generate", &graph]);
    assert_eq!(done["instances"], 100, "{done}");
}

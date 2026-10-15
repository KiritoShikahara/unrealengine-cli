use std::path::{Path, PathBuf};

use include_dir::{Dir, DirEntry, include_dir};
use serde_json::{Value, json};

use crate::core::error::CliError;

/// Plugin sources shipped inside the binary.
pub static PLUGIN: Dir<'_> = include_dir!("$OUT_DIR/plugin");
pub const PLUGIN_NAME: &str = "UnrealCliBridge";

fn plugin_files() -> Vec<(&'static Path, &'static [u8])> {
    fn walk(d: &'static Dir<'static>, out: &mut Vec<(&'static Path, &'static [u8])>) {
        for e in d.entries() {
            match e {
                DirEntry::Dir(sub) => {
                    let first = sub
                        .path()
                        .components()
                        .next()
                        .map(|c| c.as_os_str().to_string_lossy().to_string());
                    if !matches!(first.as_deref(), Some("Binaries" | "Intermediate")) {
                        walk(sub, out);
                    }
                }
                DirEntry::File(f) => {
                    let ext = f
                        .path()
                        .extension()
                        .and_then(|e| e.to_str())
                        .unwrap_or_default();
                    if ext != "pyc" {
                        out.push((f.path(), f.contents()));
                    }
                }
            }
        }
    }
    let mut out = Vec::new();
    walk(&PLUGIN, &mut out);
    out
}

/// Ensures `Plugins` in a .uproject lists the bridge as enabled; returns the new text if changed.
pub fn enable_in_uproject(text: &str) -> Result<Option<String>, CliError> {
    enable_plugins_in_uproject(text, &[PLUGIN_NAME])
}

/// Ensures `Plugins` in a .uproject lists each plugin as enabled; returns the new text if changed.
pub fn enable_plugins_in_uproject(text: &str, names: &[&str]) -> Result<Option<String>, CliError> {
    let mut v: Value = serde_json::from_str(text).map_err(|e| {
        CliError::usage(
            "project.invalid",
            format!(".uproject is not valid JSON: {e}"),
        )
    })?;
    let obj = v
        .as_object_mut()
        .ok_or_else(|| CliError::usage("project.invalid", ".uproject must be a JSON object"))?;
    let plugins = obj.entry("Plugins").or_insert_with(|| json!([]));
    let list = plugins
        .as_array_mut()
        .ok_or_else(|| CliError::usage("project.invalid", "Plugins must be an array"))?;
    let mut changed = false;
    for name in names {
        if let Some(p) = list.iter_mut().find(|p| p["Name"] == *name) {
            if p["Enabled"] != true {
                p["Enabled"] = json!(true);
                changed = true;
            }
        } else {
            list.push(json!({ "Name": name, "Enabled": true }));
            changed = true;
        }
    }
    Ok(changed.then(|| to_tabbed_json(&v)))
}

fn to_tabbed_json(v: &Value) -> String {
    let mut buf = Vec::new();
    let fmt = serde_json::ser::PrettyFormatter::with_indent(b"\t");
    let mut ser = serde_json::Serializer::with_formatter(&mut buf, fmt);
    serde::Serialize::serialize(v, &mut ser).expect("json serializes");
    String::from_utf8(buf).expect("utf-8") + "\n"
}

fn has_code_module(project_dir: &Path) -> bool {
    std::fs::read_dir(project_dir.join("Source"))
        .map(|rd| {
            rd.flatten()
                .any(|e| e.file_name().to_string_lossy().ends_with(".Target.cs"))
        })
        .unwrap_or(false)
}

fn code_module_files(name: &str) -> Vec<(PathBuf, String)> {
    let target = |kind: &str, suffix: &str| {
        format!(
            "using UnrealBuildTool;\n\npublic class {name}{suffix}Target : TargetRules\n{{\n    public {name}{suffix}Target(TargetInfo Target) : base(Target)\n    {{\n        Type = TargetType.{kind};\n        DefaultBuildSettings = BuildSettingsVersion.Latest;\n        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;\n        ExtraModuleNames.Add(\"{name}\");\n    }}\n}}\n"
        )
    };
    vec![
        (
            PathBuf::from(format!("Source/{name}.Target.cs")),
            target("Game", ""),
        ),
        (
            PathBuf::from(format!("Source/{name}Editor.Target.cs")),
            target("Editor", "Editor"),
        ),
        (
            PathBuf::from(format!("Source/{name}/{name}.Build.cs")),
            format!(
                "using UnrealBuildTool;\n\npublic class {name} : ModuleRules\n{{\n    public {name}(ReadOnlyTargetRules Target) : base(Target)\n    {{\n        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n        PublicDependencyModuleNames.AddRange(new string[] {{ \"Core\", \"CoreUObject\", \"Engine\" }});\n    }}\n}}\n"
            ),
        ),
        (
            PathBuf::from(format!("Source/{name}/{name}.cpp")),
            format!(
                "#include \"Modules/ModuleManager.h\"\n\nIMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, {name}, \"{name}\");\n"
            ),
        ),
    ]
}

fn add_module_to_uproject(text: &str, name: &str) -> Result<String, CliError> {
    let mut v: Value = serde_json::from_str(text).map_err(|e| {
        CliError::usage(
            "project.invalid",
            format!(".uproject is not valid JSON: {e}"),
        )
    })?;
    let modules = v
        .as_object_mut()
        .unwrap()
        .entry("Modules")
        .or_insert_with(|| json!([]));
    if let Some(list) = modules.as_array_mut()
        && !list.iter().any(|m| m["Name"] == name)
    {
        list.push(json!({ "Name": name, "Type": "Runtime", "LoadingPhase": "Default" }));
    }
    Ok(to_tabbed_json(&v))
}

/// Installs the plugin into the project. With `dry_run`, only reports the planned changes.
pub fn setup(uproject: &Path, dry_run: bool, add_code_module: bool) -> Result<Value, CliError> {
    let project_dir = uproject.parent().unwrap_or(Path::new("."));
    let dest = project_dir.join("Plugins").join(PLUGIN_NAME);
    let mut changes = Vec::new();
    let mut writes: Vec<(PathBuf, Vec<u8>)> = Vec::new();

    for (rel, contents) in plugin_files() {
        let path = dest.join(rel);
        let action = match std::fs::read(&path) {
            Ok(existing) if existing == contents => continue,
            Ok(_) => "update",
            Err(_) => "create",
        };
        changes
            .push(json!({ "action": action, "path": path.to_string_lossy().replace('\\', "/") }));
        writes.push((path, contents.to_vec()));
    }

    let original = std::fs::read_to_string(uproject).map_err(|e| {
        CliError::usage("project.unreadable", format!("{}: {e}", uproject.display()))
    })?;
    let mut uproject_text = enable_in_uproject(&original)?.unwrap_or_else(|| original.clone());

    let code = has_code_module(project_dir);
    if !code {
        if !add_code_module {
            return Err(CliError::usage(
                "setup.no_code_module",
                "this is a Blueprint-only project; the plugin must be compiled by a C++ project target",
            )
            .with_hint("rerun with --add-code-module to add a minimal C++ module, or add a C++ class in the editor"));
        }
        let name = uproject
            .file_stem()
            .unwrap_or_default()
            .to_string_lossy()
            .to_string();
        for (rel, body) in code_module_files(&name) {
            let path = project_dir.join(rel);
            if !path.exists() {
                changes.push(json!({ "action": "create", "path": path.to_string_lossy().replace('\\', "/") }));
                writes.push((path, body.into_bytes()));
            }
        }
        uproject_text = add_module_to_uproject(&uproject_text, &name)?;
    }
    if uproject_text != original {
        changes.push(json!({ "action": "edit", "path": uproject.to_string_lossy().replace('\\', "/"), "what": "enable UnrealCliBridge" }));
        writes.push((uproject.to_path_buf(), uproject_text.into_bytes()));
    }

    if !dry_run {
        for (path, bytes) in &writes {
            if let Some(parent) = path.parent() {
                std::fs::create_dir_all(parent)
                    .map_err(|e| CliError::internal("io.write", e.to_string()))?;
            }
            std::fs::write(path, bytes)
                .map_err(|e| CliError::internal("io.write", format!("{}: {e}", path.display())))?;
        }
    }
    Ok(json!({
        "dryRun": dry_run,
        "plugin": dest.to_string_lossy().replace('\\', "/"),
        "changes": changes,
        "upToDate": writes.is_empty(),
    }))
}

/// Version of the bundled plugin (from the .uplugin).
pub fn bundled_plugin_version() -> String {
    PLUGIN
        .get_file("UnrealCliBridge.uplugin")
        .and_then(|f| serde_json::from_slice::<Value>(f.contents()).ok())
        .and_then(|v| v["VersionName"].as_str().map(str::to_string))
        .unwrap_or_default()
}

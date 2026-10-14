use std::path::Path;

/// Copies the plugin sources (no build outputs) into OUT_DIR so they can be embedded with include_dir.
fn copy(src: &Path, dst: &Path) {
    std::fs::create_dir_all(dst).unwrap();
    for entry in std::fs::read_dir(src).unwrap().flatten() {
        let name = entry.file_name();
        let name_s = name.to_string_lossy();
        let path = entry.path();
        if path.is_dir() {
            if matches!(
                name_s.as_ref(),
                "Binaries" | "Intermediate" | "__pycache__" | "Saved"
            ) {
                continue;
            }
            copy(&path, &dst.join(&name));
        } else if !name_s.ends_with(".pyc") {
            std::fs::copy(&path, dst.join(&name)).unwrap();
        }
    }
}

fn main() {
    let src = Path::new("Plugin/UnrealCliBridge");
    let dst = Path::new(&std::env::var("OUT_DIR").unwrap()).join("plugin");
    let _ = std::fs::remove_dir_all(&dst);
    copy(src, &dst);
    println!("cargo:rerun-if-changed=Plugin/UnrealCliBridge/UnrealCliBridge.uplugin");
    println!("cargo:rerun-if-changed=Plugin/UnrealCliBridge/Source");
    println!("cargo:rerun-if-changed=Plugin/UnrealCliBridge/Content");
}

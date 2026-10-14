//! Undo Git Bash (MSYS) path conversion of Unreal package paths.
//!
//! Git Bash rewrites arguments such as `/Game/BP_Hero` into `C:/Program Files/Git/Game/BP_Hero`
//! before a native program sees them. Arguments that point into a Git for Windows install are
//! turned back into `/Game/BP_Hero`.

use std::path::Path;

/// Returns the unconverted argument if `arg` (or the value after `=`) was rewritten by MSYS.
pub fn unmangle(arg: &str, is_msys_root: &dyn Fn(&Path) -> bool) -> String {
    if let Some((flag, value)) = arg.split_once('=')
        && flag.starts_with("--")
    {
        return format!("{flag}={}", unmangle_value(value, is_msys_root));
    }
    unmangle_value(arg, is_msys_root)
}

fn unmangle_value(value: &str, is_msys_root: &dyn Fn(&Path) -> bool) -> String {
    let bytes = value.as_bytes();
    if bytes.len() < 4
        || !bytes[0].is_ascii_alphabetic()
        || bytes[1] != b':'
        || !matches!(bytes[2], b'/' | b'\\')
    {
        return value.to_string();
    }
    let normalized = value.replace('\\', "/");
    let mut search_from = 0;
    while let Some(i) = normalized[search_from..].find("/Git/") {
        let end = search_from + i + "/Git".len();
        let root = &normalized[..end];
        if is_msys_root(Path::new(root)) {
            return normalized[end..].to_string();
        }
        search_from = end;
    }
    value.to_string()
}

/// A Git for Windows root contains usr/bin/sh.exe.
pub fn looks_like_msys_root(root: &Path) -> bool {
    root.join("usr/bin/sh.exe").is_file()
}

/// Applies [`unmangle`] to every argument when running under Git Bash on Windows.
pub fn fix_args(args: Vec<String>) -> Vec<String> {
    if !cfg!(windows) || std::env::var_os("UNREAL_CLI_KEEP_ARGS").is_some() {
        return args;
    }
    args.into_iter()
        .map(|a| unmangle(&a, &looks_like_msys_root))
        .collect()
}

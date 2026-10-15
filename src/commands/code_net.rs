//! `code replicate` / `code rpc`: add replicated properties and RPCs to a C++ class (header and source).

use std::path::{Path, PathBuf};

use regex::Regex;
use serde_json::{Value, json};

use crate::core::error::CliError;

pub struct ClassFiles {
    pub header: PathBuf,
    pub source: PathBuf,
    pub header_text: String,
    pub source_text: String,
    pub class: String,
}

/// The header's .cpp: Public/ -> Private/ (or Classes/ -> Private/), else next to the header.
fn source_for(header: &Path) -> Option<PathBuf> {
    let stem = header.file_stem()?.to_string_lossy().to_string();
    let file = format!("{stem}.cpp");
    let mut candidates = Vec::new();
    let mut parts: Vec<String> = header
        .parent()?
        .components()
        .map(|c| c.as_os_str().to_string_lossy().to_string())
        .collect();
    if let Some(i) = parts.iter().rposition(|p| p == "Public" || p == "Classes") {
        parts[i] = "Private".into();
        candidates.push(parts.iter().collect::<PathBuf>().join(&file));
    }
    candidates.push(header.with_extension("cpp"));
    candidates.into_iter().find(|p| p.is_file())
}

pub fn load(header: PathBuf) -> Result<ClassFiles, CliError> {
    let header_text = std::fs::read_to_string(&header)
        .map_err(|e| CliError::usage("code.unreadable", e.to_string()))?;
    let source = source_for(&header).ok_or_else(|| {
        CliError::usage(
            "code.source_not_found",
            format!("no .cpp found for {}", header.display()),
        )
        .with_hint("put it in the module's Private/ folder or next to the header")
    })?;
    let source_text = std::fs::read_to_string(&source)
        .map_err(|e| CliError::usage("code.unreadable", e.to_string()))?;
    let re = Regex::new(r"(?m)^\s*class\s+(?:[A-Z0-9_]+_API\s+)?(\w+)\s*(?:final\s*)?:").unwrap();
    let class = re
        .captures(&header_text)
        .map(|c| c[1].to_string())
        .ok_or_else(|| {
            CliError::usage(
                "code.class_not_found",
                format!("no class declaration in {}", header.display()),
            )
        })?;
    Ok(ClassFiles {
        header,
        source,
        header_text,
        source_text,
        class,
    })
}

/// Byte index of the brace that closes the block opened by the first `{` at or after `from`.
fn block_end(text: &str, from: usize) -> Option<usize> {
    let open = from + text[from..].find('{')?;
    let mut depth = 0;
    for (i, c) in text[open..].char_indices() {
        match c {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(open + i);
                }
            }
            _ => {}
        }
    }
    None
}

/// Inserts `block` (lines ending in '\n') right before the class's closing `};`.
fn insert_in_class(text: &str, class: &str, block: &str) -> Result<String, CliError> {
    let decl = Regex::new(&format!(r"(?m)^\s*class\s+(?:[A-Z0-9_]+_API\s+)?{class}\b"))
        .unwrap()
        .find(text)
        .ok_or_else(|| CliError::usage("code.class_not_found", format!("no class {class}")))?;
    let end = block_end(text, decl.end())
        .ok_or_else(|| CliError::usage("code.parse", format!("class {class} is not closed")))?;
    Ok(format!("{}{}{}", &text[..end], block, &text[end..]))
}

fn add_include(source: &str) -> String {
    if source.contains("Net/UnrealNetwork.h") {
        return source.to_string();
    }
    let Some(start) = source.find("#include") else {
        return format!("#include \"Net/UnrealNetwork.h\"\n{source}");
    };
    let eol = source[start..]
        .find('\n')
        .map(|i| start + i)
        .unwrap_or(source.len());
    format!(
        "{}\n#include \"Net/UnrealNetwork.h\"{}",
        &source[..eol],
        &source[eol..]
    )
}

fn append(source: &str, block: &str) -> String {
    let mut s = source.to_string();
    if !s.ends_with('\n') {
        s.push('\n');
    }
    s.push('\n');
    s.push_str(block);
    s
}

fn has_member(header: &str, name: &str) -> bool {
    Regex::new(&format!(r"\b{}\s*[;(]", regex::escape(name)))
        .unwrap()
        .is_match(header)
}

pub fn replicate(
    f: &ClassFiles,
    name: &str,
    ty: &str,
    notify: bool,
    condition: Option<&str>,
) -> Result<(String, String), CliError> {
    if has_member(&f.header_text, name) {
        return Err(CliError::usage(
            "code.exists",
            format!("{} already declares {name}", f.class),
        ));
    }
    let c = &f.class;
    let spec = if notify {
        format!("ReplicatedUsing = OnRep_{name}")
    } else {
        "Replicated".to_string()
    };
    let mut block = format!("\npublic:\n\tUPROPERTY({spec})\n\t{ty} {name};\n");
    if notify {
        block.push_str(&format!("\n\tUFUNCTION()\n\tvoid OnRep_{name}();\n"));
    }
    if !f.header_text.contains("GetLifetimeReplicatedProps") {
        block.push_str("\n\tvirtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;\n");
    }
    let header = insert_in_class(&f.header_text, c, &block)?;

    let line = match condition {
        Some(cond) => {
            let cond = cond.strip_prefix("COND_").unwrap_or(cond);
            format!("\tDOREPLIFETIME_CONDITION({c}, {name}, COND_{cond});\n")
        }
        None => format!("\tDOREPLIFETIME({c}, {name});\n"),
    };
    let mut source = add_include(&f.source_text);
    let def = format!("{c}::GetLifetimeReplicatedProps(");
    source = match source.find(&def) {
        Some(at) => {
            let end = block_end(&source, at).ok_or_else(|| {
                CliError::usage("code.parse", "GetLifetimeReplicatedProps is not closed")
            })?;
            format!("{}{}{}", &source[..end], line, &source[end..])
        }
        None => append(
            &source,
            &format!(
                "void {c}::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const\n{{\n\tSuper::GetLifetimeReplicatedProps(OutLifetimeProps);\n{line}}}\n"
            ),
        ),
    };
    if notify {
        source = append(&source, &format!("void {c}::OnRep_{name}()\n{{\n}}\n"));
    }
    Ok((header, source))
}

pub fn rpc(
    f: &ClassFiles,
    name: &str,
    kind: &str,
    reliable: bool,
    validate: bool,
    params: &[(String, String)],
) -> Result<(String, String), CliError> {
    if has_member(&f.header_text, name) {
        return Err(CliError::usage(
            "code.exists",
            format!("{} already declares {name}", f.class),
        ));
    }
    let c = &f.class;
    let spec = match kind {
        "server" => "Server",
        "client" => "Client",
        _ => "NetMulticast",
    };
    let mut specs = vec![spec, if reliable { "Reliable" } else { "Unreliable" }];
    if validate {
        specs.push("WithValidation");
    }
    let args = params
        .iter()
        .map(|(n, t)| format!("{t} {n}"))
        .collect::<Vec<_>>()
        .join(", ");
    let header = insert_in_class(
        &f.header_text,
        c,
        &format!(
            "\npublic:\n\tUFUNCTION({})\n\tvoid {name}({args});\n",
            specs.join(", ")
        ),
    )?;
    let mut source = append(
        &f.source_text,
        &format!("void {c}::{name}_Implementation({args})\n{{\n}}\n"),
    );
    if validate {
        source = append(
            &source,
            &format!("bool {c}::{name}_Validate({args})\n{{\n\treturn true;\n}}\n"),
        );
    }
    Ok((header, source))
}

/// Writes (or, for a dry run, only describes) the changed files.
pub fn finish(
    f: &ClassFiles,
    dir: &Path,
    header: String,
    source: String,
    dry_run: bool,
    added: Vec<String>,
) -> Result<Value, CliError> {
    let rel = |p: &Path| {
        p.strip_prefix(dir)
            .unwrap_or(p)
            .to_string_lossy()
            .replace('\\', "/")
    };
    if !dry_run {
        std::fs::write(&f.header, &header)
            .and_then(|_| std::fs::write(&f.source, &source))
            .map_err(|e| CliError::internal("io.write", e.to_string()))?;
    }
    let mut out = json!({
        "class": f.class,
        "header": rel(&f.header),
        "source": rel(&f.source),
        "added": added,
        "dryRun": dry_run,
    });
    if !source.contains("bReplicates") && !f.header_text.contains("bReplicates") {
        out["hint"] = json!("the actor must replicate: set bReplicates = true in its constructor");
    }
    out["next"] = json!("apply it: unrealengine-cli code apply (header changes rebuild)");
    Ok(out)
}

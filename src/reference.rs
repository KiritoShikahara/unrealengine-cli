//! Read-only search of engine headers and the editor's Python API stub.

use std::path::{Path, PathBuf};

use regex::RegexBuilder;
use serde_json::{Value, json};

use crate::core::error::CliError;

fn walk(dir: &Path, exts: &[&str], out: &mut Vec<PathBuf>) {
    let Ok(rd) = std::fs::read_dir(dir) else {
        return;
    };
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            if !matches!(
                p.file_name().and_then(|n| n.to_str()),
                Some("ThirdParty" | "Intermediate" | "Binaries")
            ) {
                walk(&p, exts, out);
            }
        } else if p
            .extension()
            .and_then(|x| x.to_str())
            .is_some_and(|x| exts.contains(&x))
        {
            out.push(p);
        }
    }
}

/// Searches `roots` for `query` (case-sensitive word match unless `regex`).
pub fn search_files(
    roots: &[PathBuf],
    exts: &[&str],
    query: &str,
    regex: bool,
    limit: usize,
) -> Result<Vec<Value>, CliError> {
    let pattern = if regex {
        query.to_string()
    } else {
        format!(r"\b{}\b", regex::escape(query))
    };
    let re = RegexBuilder::new(&pattern)
        .build()
        .map_err(|e| CliError::usage("args.invalid", format!("bad regex: {e}")))?;
    let mut hits = Vec::new();
    for root in roots {
        let mut files = Vec::new();
        walk(root, exts, &mut files);
        for f in files {
            let Ok(text) = std::fs::read_to_string(&f) else {
                continue;
            };
            for (i, line) in text.lines().enumerate() {
                if re.is_match(line) {
                    hits.push(json!({ "file": f.to_string_lossy().replace('\\', "/"), "line": i + 1, "text": line.trim() }));
                    if hits.len() >= limit {
                        return Ok(hits);
                    }
                }
            }
        }
    }
    Ok(hits)
}

pub fn search(
    engine_root: Option<&Path>,
    project_dir: Option<&Path>,
    query: &str,
    scope: &str,
    regex: bool,
    limit: usize,
) -> Result<Value, CliError> {
    let mut hits = Vec::new();
    if scope == "python" || scope == "all" {
        let stub = project_dir.map(|d| d.join("Intermediate/PythonStub"));
        match stub.filter(|s| s.is_dir()) {
            Some(s) => hits.extend(search_files(&[s], &["py"], query, regex, limit)?),
            None if scope == "python" => {
                return Err(CliError::usage(
                    "reference.no_python_stub",
                    "no Python stub in Intermediate/PythonStub",
                )
                .with_hint(
                    "enable Developer Mode in Python plugin settings and open the editor once",
                ));
            }
            None => {}
        }
    }
    if (scope == "cpp" || scope == "all") && hits.len() < limit {
        let root = engine_root
            .ok_or_else(|| CliError::usage("engine.not_found", "no engine for this project"))?;
        let roots: Vec<PathBuf> = ["Runtime", "Editor", "Developer"]
            .iter()
            .map(|d| root.join("Engine/Source").join(d))
            .collect();
        hits.extend(search_files(
            &roots,
            &["h"],
            query,
            regex,
            limit - hits.len(),
        )?);
    }
    Ok(json!({ "query": query, "scope": scope, "hits": hits }))
}

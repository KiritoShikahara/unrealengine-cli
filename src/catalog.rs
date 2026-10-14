use serde_json::Value;

/// Tool catalog snapshot taken from a live editor (`scripts/gen-catalog.sh`), for offline use.
pub const BUNDLED: &str = include_str!("../tools/catalog.json");

pub fn bundled() -> Vec<Value> {
    serde_json::from_str(BUNDLED).unwrap_or_default()
}

/// Ranks tools by how many query words appear in name, group and summary (name hits weigh most).
pub fn find(tools: &[Value], query: &str, limit: usize) -> Vec<Value> {
    let words: Vec<String> = query.split_whitespace().map(str::to_lowercase).collect();
    let mut scored: Vec<(usize, &Value)> = tools
        .iter()
        .filter_map(|t| {
            let name = t["name"].as_str().unwrap_or_default().to_lowercase();
            let group = t["group"].as_str().unwrap_or_default().to_lowercase();
            let text = format!(
                "{} {}",
                t["summary"].as_str().unwrap_or_default(),
                t["description"].as_str().unwrap_or_default()
            )
            .to_lowercase();
            if words.is_empty() {
                return Some((1, t));
            }
            if words.len() == 1 && (name == words[0] || group == words[0]) {
                return Some((100, t));
            }
            let score: usize = words
                .iter()
                .map(|w| {
                    3 * usize::from(name.contains(w.as_str()))
                        + 2 * usize::from(group == *w)
                        + usize::from(text.contains(w.as_str()))
                })
                .sum();
            (score > 0).then_some((score, t))
        })
        .collect();
    scored.sort_by(|a, b| {
        b.0.cmp(&a.0)
            .then_with(|| a.1["name"].as_str().cmp(&b.1["name"].as_str()))
    });
    scored
        .into_iter()
        .take(limit)
        .map(|(_, t)| t.clone())
        .collect()
}

/// One-line view used by `tool list` / `tool find`.
pub fn summary(t: &Value) -> Value {
    serde_json::json!({
        "name": t["name"],
        "group": t["group"],
        "summary": t["summary"],
        "mutating": t["mutating"],
    })
}

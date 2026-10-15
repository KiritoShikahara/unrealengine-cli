//! Minimal editing of Unreal config files (`Config/Default*.ini`): read and set one key in a
//! section, keeping the rest of the file and its line endings.

fn newline(text: &str) -> &'static str {
    if text.contains("\r\n") { "\r\n" } else { "\n" }
}

fn is_section(line: &str) -> bool {
    line.trim_start().starts_with('[')
}

/// Range of body lines `[start, end)` of `[section]`, if present.
fn section_range(lines: &[&str], section: &str) -> Option<(usize, usize)> {
    let header = format!("[{section}]");
    let start = lines.iter().position(|l| l.trim() == header)? + 1;
    let end = lines[start..]
        .iter()
        .position(|l| is_section(l))
        .map_or(lines.len(), |i| start + i);
    Some((start, end))
}

fn key_of(line: &str) -> Option<&str> {
    let t = line.trim_start();
    if t.starts_with(';') || t.starts_with('#') {
        return None;
    }
    t.split_once('=').map(|(k, _)| k.trim())
}

/// Value of `key` in `[section]` (the last one when repeated).
pub fn get(text: &str, section: &str, key: &str) -> Option<String> {
    let lines: Vec<&str> = text.lines().collect();
    let (s, e) = section_range(&lines, section)?;
    lines[s..e]
        .iter()
        .rev()
        .find(|l| key_of(l).is_some_and(|k| k.eq_ignore_ascii_case(key)))
        .and_then(|l| l.split_once('=').map(|(_, v)| v.trim().to_string()))
}

/// Sets `key=value` in `[section]`: replaces the first line with that key, else appends it to the
/// section, else adds the section at the end.
pub fn set(text: &str, section: &str, key: &str, value: &str) -> String {
    let nl = newline(text);
    let mut lines: Vec<String> = text.lines().map(String::from).collect();
    let refs: Vec<&str> = lines.iter().map(String::as_str).collect();
    let entry = format!("{key}={value}");
    match section_range(&refs, section) {
        Some((s, e)) => {
            if let Some(i) =
                (s..e).find(|&i| key_of(&lines[i]).is_some_and(|k| k.eq_ignore_ascii_case(key)))
            {
                lines[i] = entry;
            } else {
                let mut at = e;
                while at > s && lines[at - 1].trim().is_empty() {
                    at -= 1;
                }
                lines.insert(at, entry);
            }
        }
        None => {
            if lines.last().is_some_and(|l| !l.trim().is_empty()) {
                lines.push(String::new());
            }
            lines.push(format!("[{section}]"));
            lines.push(entry);
        }
    }
    let mut out = lines.join(nl);
    out.push_str(nl);
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn set_replaces_appends_or_adds_a_section() {
        let t = "[A]\nx=1\n\n[B]\ny=2\n";
        assert_eq!(set(t, "A", "x", "9"), "[A]\nx=9\n\n[B]\ny=2\n");
        assert_eq!(set(t, "A", "z", "3"), "[A]\nx=1\nz=3\n\n[B]\ny=2\n");
        assert_eq!(set(t, "C", "k", "v"), "[A]\nx=1\n\n[B]\ny=2\n\n[C]\nk=v\n");
        assert_eq!(set("", "C", "k", "v"), "[C]\nk=v\n");
        assert_eq!(get(t, "B", "Y").as_deref(), Some("2"));
        assert_eq!(get("[A]\n;x=1\n", "A", "x"), None);
    }
}

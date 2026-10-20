use std::time::Duration;

use serde::Serialize;
use serde_json::Value;

use super::error::CliError;
use super::exit_code::ExitCode;
use crate::cli::OutputFormat;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Meta {
    pub duration_ms: u64,
    pub cli_version: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub instance: Option<String>,
}

/// Common output shape for every command (contracts/cli-envelope.schema.json).
#[derive(Debug, Clone, Serialize)]
pub struct Envelope {
    pub ok: bool,
    pub command: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<CliError>,
    pub warnings: Vec<String>,
    pub meta: Meta,
}

impl Envelope {
    pub fn from_result(
        command: impl Into<String>,
        result: Result<Value, CliError>,
        elapsed: Duration,
    ) -> Self {
        let (ok, data, error) = match result {
            Ok(v) => (true, Some(v), None),
            Err(e) => (false, None, Some(e)),
        };
        Self {
            ok,
            command: command.into(),
            data,
            error,
            warnings: Vec::new(),
            meta: Meta {
                duration_ms: elapsed.as_millis() as u64,
                cli_version: env!("CARGO_PKG_VERSION").to_string(),
                instance: None,
            },
        }
    }

    pub fn exit_code(&self) -> i32 {
        match &self.error {
            None => ExitCode::Success.as_i32(),
            Some(e) => e.exit.as_i32(),
        }
    }

    pub fn to_json(&self) -> String {
        serde_json::to_string_pretty(self).expect("envelope serializes")
    }

    pub fn to_text(&self) -> String {
        let mut out = String::new();
        if let Some(data) = &self.data {
            match data {
                Value::String(s) => out.push_str(s),
                other => out.push_str(&serde_json::to_string_pretty(other).unwrap_or_default()),
            }
        }
        if let Some(e) = &self.error {
            out.push_str(&format!("error[{}]: {}", e.code, e.message));
            if let Some(h) = &e.hint {
                out.push_str(&format!("\nhint: {h}"));
            }
        }
        out
    }

    /// Warnings for text output; printed on stderr so stdout stays machine-readable.
    pub fn warning_lines(&self) -> Vec<String> {
        self.warnings
            .iter()
            .map(|w| format!("warning: {w}"))
            .collect()
    }

    pub fn print(&self, format: OutputFormat) {
        match format {
            OutputFormat::Json => println!("{}", self.to_json()),
            OutputFormat::Text if self.ok => println!("{}", self.to_text()),
            OutputFormat::Text => eprintln!("{}", self.to_text()),
        }
        if format == OutputFormat::Text {
            for w in self.warning_lines() {
                eprintln!("{w}");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn text_output_keeps_warnings_off_stdout() {
        let mut e = Envelope::from_result("x", Ok(json!({ "a": 1 })), Duration::ZERO);
        e.warnings.push("plugin mismatch".into());
        let text = e.to_text();
        assert!(serde_json::from_str::<Value>(&text).is_ok(), "{text}");
        assert_eq!(e.warning_lines(), vec!["warning: plugin mismatch"]);
    }
}

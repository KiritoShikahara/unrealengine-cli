use std::fmt;

use serde::Serialize;
use serde_json::Value;

use super::exit_code::ExitCode;

/// Structured failure carried in the envelope's `error` field.
#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct CliError {
    /// `domain.reason`, e.g. `connect.no_instance`
    pub code: String,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub hint: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub details: Option<Value>,
    #[serde(skip)]
    pub exit: ExitCode,
}

impl CliError {
    pub fn new(exit: ExitCode, code: impl Into<String>, message: impl Into<String>) -> Self {
        Self {
            code: code.into(),
            message: message.into(),
            hint: None,
            details: None,
            exit,
        }
    }

    pub fn with_hint(mut self, hint: impl Into<String>) -> Self {
        self.hint = Some(hint.into());
        self
    }

    pub fn with_details(mut self, details: Value) -> Self {
        self.details = Some(details);
        self
    }

    pub fn usage(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self::new(ExitCode::Usage, code, message)
    }

    pub fn connect(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self::new(ExitCode::Connect, code, message)
    }

    pub fn editor(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self::new(ExitCode::Editor, code, message)
    }

    pub fn timeout(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self::new(ExitCode::Timeout, code, message)
    }

    pub fn internal(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self::new(ExitCode::Internal, code, message)
    }
}

impl fmt::Display for CliError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for CliError {}

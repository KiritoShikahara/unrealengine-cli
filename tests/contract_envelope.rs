use std::time::Duration;

use serde_json::{Value, json};
use unrealengine_cli::core::envelope::Envelope;
use unrealengine_cli::core::error::CliError;

fn validator() -> jsonschema::Validator {
    let schema: Value = serde_json::from_str(include_str!(
        "../specs/001-unreal-cli/contracts/cli-envelope.schema.json"
    ))
    .unwrap();
    jsonschema::validator_for(&schema).unwrap()
}

fn as_value(e: &Envelope) -> Value {
    serde_json::from_str(&e.to_json()).unwrap()
}

#[test]
fn success_envelope_matches_schema() {
    let e = Envelope::from_result("ping", Ok(json!({"rttMs": 3})), Duration::from_millis(5));
    let v = as_value(&e);
    assert!(validator().is_valid(&v), "{v}");
    assert_eq!(e.exit_code(), 0);
}

#[test]
fn failure_envelope_matches_schema_and_maps_exit_code() {
    let err = CliError::connect("connect.no_instance", "no editor")
        .with_hint("unrealengine-cli editor open");
    let e = Envelope::from_result("ping", Err(err), Duration::ZERO);
    let v = as_value(&e);
    assert!(validator().is_valid(&v), "{v}");
    assert_eq!(v["error"]["hint"], "unrealengine-cli editor open");
    assert_eq!(e.exit_code(), 3);
}

#[test]
fn failure_without_error_is_rejected_by_schema() {
    let v = json!({"ok": false, "command": "x", "warnings": [], "meta": {"durationMs": 0, "cliVersion": "0"}});
    assert!(!validator().is_valid(&v));
}

#[test]
fn error_code_must_be_domain_dot_reason() {
    let e = Envelope::from_result("x", Err(CliError::editor("Bad Code", "m")), Duration::ZERO);
    assert!(!validator().is_valid(&as_value(&e)));
}

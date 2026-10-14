use std::path::Path;

use unrealengine_cli::update::{
    auto_update_due, auto_update_eligible, is_newer, parse_latest_location, sha256_hex, verify,
};

#[test]
fn auto_update_runs_at_most_once_a_day() {
    let dir = std::env::temp_dir().join(format!("uecli-auto-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let stamp = dir.join("last-update-check");
    let _ = std::fs::remove_file(&stamp);
    assert!(auto_update_due(&stamp, 1_000_000), "no stamp yet");
    std::fs::write(&stamp, "1000000").unwrap();
    assert!(!auto_update_due(&stamp, 1_000_000 + 3600));
    assert!(auto_update_due(&stamp, 1_000_000 + 86_400));
    std::fs::write(&stamp, "garbage").unwrap();
    assert!(auto_update_due(&stamp, 5));
}

#[test]
fn auto_update_skips_dev_builds_and_opt_out() {
    let installed = Path::new("C:/Users/u/.local/bin/unrealengine-cli.exe");
    let dev = Path::new("C:/src/repo/target/debug/unrealengine-cli.exe");
    assert!(auto_update_eligible(
        installed,
        false,
        &["unrealengine-cli", "ping"]
    ));
    assert!(!auto_update_eligible(
        dev,
        false,
        &["unrealengine-cli", "ping"]
    ));
    assert!(!auto_update_eligible(
        installed,
        true,
        &["unrealengine-cli", "ping"]
    ));
    assert!(!auto_update_eligible(
        installed,
        false,
        &["unrealengine-cli", "self", "update"]
    ));
}

#[test]
fn release_redirect_and_version_compare() {
    let h = "HTTP/1.1 302 Found\r\nLocation: https://github.com/o/unrealengine-cli/releases/tag/v9.1.0\r\n\r\n";
    assert_eq!(parse_latest_location(h).as_deref(), Some("v9.1.0"));
    assert_eq!(parse_latest_location("HTTP/1.1 404\r\n"), None);
    assert!(is_newer("v9.1.0"));
    assert!(!is_newer("v0.0.1"));
    assert!(!is_newer(concat!("v", env!("CARGO_PKG_VERSION"))));
}

#[test]
fn checksum_match_and_mismatch() {
    let data = b"binary";
    let sums = format!(
        "{}  unrealengine-cli-x.zip\nabc  other.zip\n",
        sha256_hex(data)
    );
    assert!(verify(data, &sums, "unrealengine-cli-x.zip").is_ok());
    let err = verify(b"tampered", &sums, "unrealengine-cli-x.zip").unwrap_err();
    assert_eq!(err.code, "update.checksum_mismatch");
    let err = verify(data, &sums, "missing.zip").unwrap_err();
    assert_eq!(err.code, "update.no_checksum");
}

#[test]
fn install_scripts_verify_checksums_before_installing() {
    let ps = include_str!("../scripts/install.ps1");
    let sh = include_str!("../scripts/install.sh");
    for s in [ps, sh] {
        assert!(s.contains("SHA256SUMS"));
        let verify_at = s.find("checksum mismatch").expect("aborts on mismatch");
        let install_at = s.find("INSTALL-STEP").expect("install step marker");
        assert!(
            verify_at < install_at,
            "verification must come before installing"
        );
    }
}

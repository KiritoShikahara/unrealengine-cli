use unreal_cli::update::{sha256_hex, verify};

#[test]
fn checksum_match_and_mismatch() {
    let data = b"binary";
    let sums = format!("{}  unreal-cli-x.zip\nabc  other.zip\n", sha256_hex(data));
    assert!(verify(data, &sums, "unreal-cli-x.zip").is_ok());
    let err = verify(b"tampered", &sums, "unreal-cli-x.zip").unwrap_err();
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

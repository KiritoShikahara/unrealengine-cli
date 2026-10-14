use unrealengine_cli::engine::build::{editor_target, parse_diagnostics};

#[test]
fn msvc_errors_and_warnings() {
    let log = include_str!("fixtures/build_msvc.log");
    let d = parse_diagnostics(log);
    assert_eq!(d.len(), 3, "{d:?}");
    assert_eq!(d[0].file, r"C:\Proj\Source\Game\MyActor.cpp");
    assert_eq!(d[0].line, 37);
    assert_eq!(d[0].column, Some(39));
    assert_eq!(d[0].severity, "error");
    assert_eq!(d[0].code, "C2027");
    assert_eq!(d[0].message, "use of undefined type 'FProjectDescriptor'");
    assert_eq!(d[1].severity, "warning");
    assert_eq!(d[1].column, None);
    assert_eq!(d[2].code, "UBT");
}

#[test]
fn clang_errors() {
    let log = "/Users/me/Proj/Source/Game/A.cpp:12:5: error: use of undeclared identifier 'Foo'\n\
               /Users/me/Proj/Source/Game/A.h:3:1: warning: unused variable 'x' [-Wunused-variable]\n";
    let d = parse_diagnostics(log);
    assert_eq!(d.len(), 2);
    assert_eq!(d[0].file, "/Users/me/Proj/Source/Game/A.cpp");
    assert_eq!(d[0].column, Some(5));
    assert_eq!(d[1].code, "-Wunused-variable");
}

#[test]
fn duplicates_are_removed() {
    let line = "C:\\a.cpp(1): error C1: x\n";
    assert_eq!(parse_diagnostics(&line.repeat(3)).len(), 1);
}

#[test]
fn editor_target_comes_from_target_files() {
    let dir = tempfile::tempdir().unwrap();
    let up = dir.path().join("Game.uproject");
    std::fs::write(&up, "{}").unwrap();
    assert_eq!(editor_target(&up), "GameEditor");
    std::fs::create_dir(dir.path().join("Source")).unwrap();
    std::fs::write(dir.path().join("Source/Other.Target.cs"), "").unwrap();
    std::fs::write(dir.path().join("Source/OtherEditor.Target.cs"), "").unwrap();
    assert_eq!(editor_target(&up), "OtherEditor");
}

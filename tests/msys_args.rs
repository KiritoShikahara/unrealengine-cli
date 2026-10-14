use std::path::Path;

use unrealengine_cli::core::msys::unmangle;

fn git_root(p: &Path) -> bool {
    p == Path::new("C:/Program Files/Git")
}

#[test]
fn mangled_package_paths_are_restored() {
    assert_eq!(
        unmangle("C:/Program Files/Git/Game/BP_Hero", &git_root),
        "/Game/BP_Hero"
    );
    assert_eq!(
        unmangle("C:\\Program Files\\Git\\Niagara\\X", &git_root),
        "/Niagara/X"
    );
    assert_eq!(
        unmangle("--path=C:/Program Files/Git/Game", &git_root),
        "--path=/Game"
    );
}

#[test]
fn real_paths_are_left_alone() {
    assert_eq!(
        unmangle("C:/Users/me/Git/Proj/Game.uproject", &git_root),
        "C:/Users/me/Git/Proj/Game.uproject"
    );
    assert_eq!(unmangle("/Game/BP", &git_root), "/Game/BP");
    assert_eq!(unmangle("N1.then", &git_root), "N1.then");
    assert_eq!(unmangle("C:", &git_root), "C:");
}

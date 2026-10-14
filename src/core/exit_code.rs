/// Process exit codes (contracts/cli-commands.md).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExitCode {
    Success = 0,
    Internal = 1,
    Usage = 2,
    Connect = 3,
    Editor = 4,
    Timeout = 5,
    Build = 6,
}

impl ExitCode {
    pub fn as_i32(self) -> i32 {
        self as i32
    }
}

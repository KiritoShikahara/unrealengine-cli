//! Starts a long-lived process fully detached from the caller.
//!
//! On Windows `std::process::Command` lets the child inherit every inheritable handle, including the
//! caller's stdout pipe, so a shell that captures our output would wait until the editor exits.
//! There the process is created with `bInheritHandles = FALSE`.

use std::ffi::OsStr;
use std::path::Path;

use crate::core::error::CliError;

/// Quotes one argument for a Windows command line (CommandLineToArgvW rules).
pub fn quote_windows_arg(arg: &str) -> String {
    if !arg.is_empty() && !arg.contains([' ', '\t', '"']) {
        return arg.to_string();
    }
    let mut out = String::from('"');
    let mut backslashes = 0;
    for c in arg.chars() {
        match c {
            '\\' => backslashes += 1,
            '"' => {
                out.push_str(&"\\".repeat(backslashes * 2 + 1));
                out.push('"');
                backslashes = 0;
            }
            _ => {
                out.push_str(&"\\".repeat(backslashes));
                out.push(c);
                backslashes = 0;
            }
        }
    }
    out.push_str(&"\\".repeat(backslashes * 2));
    out.push('"');
    out
}

#[cfg(windows)]
pub fn spawn_detached(exe: &Path, args: &[&OsStr]) -> Result<u32, CliError> {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Foundation::CloseHandle;
    use windows_sys::Win32::System::Threading::{
        CREATE_NEW_PROCESS_GROUP, CreateProcessW, DETACHED_PROCESS, PROCESS_INFORMATION,
        STARTUPINFOW,
    };

    let mut line = quote_windows_arg(&exe.to_string_lossy());
    for a in args {
        line.push(' ');
        line.push_str(&quote_windows_arg(&a.to_string_lossy()));
    }
    let mut wide: Vec<u16> = OsStr::new(&line).encode_wide().chain(Some(0)).collect();
    // SAFETY: zeroed STARTUPINFOW/PROCESS_INFORMATION are valid initial states; cb is set below.
    let mut si: STARTUPINFOW = unsafe { std::mem::zeroed() };
    si.cb = std::mem::size_of::<STARTUPINFOW>() as u32;
    let mut pi: PROCESS_INFORMATION = unsafe { std::mem::zeroed() };
    // SAFETY: `wide` is a writable NUL-terminated buffer that outlives the call; other pointers are
    // null or point to the initialised structs above.
    let ok = unsafe {
        CreateProcessW(
            std::ptr::null(),
            wide.as_mut_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            0,
            DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
            std::ptr::null(),
            std::ptr::null(),
            &si,
            &mut pi,
        )
    };
    if ok == 0 {
        return Err(CliError::internal(
            "editor.spawn",
            format!(
                "cannot start {}: {}",
                exe.display(),
                std::io::Error::last_os_error()
            ),
        ));
    }
    // SAFETY: both handles were returned by a successful CreateProcessW and are closed once.
    unsafe {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    Ok(pi.dwProcessId)
}

#[cfg(not(windows))]
pub fn spawn_detached(exe: &Path, args: &[&OsStr]) -> Result<u32, CliError> {
    use std::process::{Command, Stdio};
    let child = Command::new(exe)
        .args(args)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|e| {
            CliError::internal(
                "editor.spawn",
                format!("cannot start {}: {e}", exe.display()),
            )
        })?;
    Ok(child.id())
}

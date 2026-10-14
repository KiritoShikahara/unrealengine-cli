use std::io::{self, Read, Write};

use serde_json::Value;

/// Largest accepted frame body (64 MiB).
pub const MAX_FRAME: usize = 64 * 1024 * 1024;

/// Writes one frame: big-endian u32 length followed by UTF-8 JSON.
pub fn write_frame<W: Write>(w: &mut W, value: &Value) -> io::Result<()> {
    let body = serde_json::to_vec(value)?;
    if body.len() > MAX_FRAME {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "frame too large",
        ));
    }
    w.write_all(&(body.len() as u32).to_be_bytes())?;
    w.write_all(&body)?;
    w.flush()
}

/// Reads one frame written by [`write_frame`].
pub fn read_frame<R: Read>(r: &mut R) -> io::Result<Value> {
    let mut len = [0u8; 4];
    r.read_exact(&mut len)?;
    let len = u32::from_be_bytes(len) as usize;
    if len > MAX_FRAME {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            "frame too large",
        ));
    }
    let mut body = vec![0u8; len];
    r.read_exact(&mut body)?;
    serde_json::from_slice(&body).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))
}

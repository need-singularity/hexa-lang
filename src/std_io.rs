//! std::io — I/O builtins for HEXA-LANG.
//!
//! Provides:
//!   io_stdin()                    — read one line from stdin
//!   io_read_lines(path)           — read file as array of lines
//!   io_write_bytes(path, bytes)   — write byte array to file
//!   io_pipe(cmd)                  — run shell command, capture output
//!   io_tempfile()                 — create temp file, return path
//!   io_buffered_reader(path)      — open buffered reader handle
//!   io_reader_next(reader)        — read next line (void when EOF)

#![allow(dead_code)]

use std::collections::HashMap;
use std::io::{BufRead, BufReader};
use std::sync::Mutex;
use std::fs::File;

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

// ── Global reader registry ──────────────────────────────────
// We store open BufReaders keyed by a monotonic ID.
// The HEXA Value representing a reader is a Map with { "_type": "buffered_reader", "_id": N }.

static NEXT_READER_ID: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(1);

type ReaderMap = HashMap<u64, BufReader<File>>;

mod reader_registry {
    use super::*;
    use std::sync::OnceLock;

    static INSTANCE: OnceLock<Mutex<ReaderMap>> = OnceLock::new();

    pub fn get() -> &'static Mutex<ReaderMap> {
        INSTANCE.get_or_init(|| Mutex::new(HashMap::new()))
    }
}

fn get_readers() -> &'static Mutex<ReaderMap> {
    reader_registry::get()
}

/// Dispatch an I/O builtin by name.  Called from `Interpreter::call_builtin`.
pub fn call_io_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "io_stdin" => builtin_io_stdin(interp, args),
        "io_read_lines" => builtin_io_read_lines(interp, args),
        "io_write_bytes" => builtin_io_write_bytes(interp, args),
        "io_pipe" => builtin_io_pipe(interp, args),
        "io_tempfile" => builtin_io_tempfile(interp, args),
        "io_buffered_reader" => builtin_io_buffered_reader(interp, args),
        "io_reader_next" => builtin_io_reader_next(interp, args),
        _ => Err(io_err(interp, format!("unknown io builtin: {}", name))),
    }
}

// ── Helpers ─────────────────────────────────────────────────

fn io_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

fn type_err(interp: &Interpreter, msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Type,
        message: msg,
        line: interp.current_line,
        col: interp.current_col,
        hint: None,
    }
}

fn require_str<'a>(interp: &Interpreter, args: &'a [Value], idx: usize, func: &str) -> Result<&'a str, HexaError> {
    match args.get(idx) {
        Some(Value::Str(s)) => Ok(s.as_str()),
        Some(_) => Err(type_err(interp, format!("{}() argument {} must be a string", func, idx + 1))),
        None => Err(type_err(interp, format!("{}() requires at least {} arguments", func, idx + 1))),
    }
}

// ── io_stdin() -> string ──

fn builtin_io_stdin(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let mut line = String::new();
    std::io::stdin().read_line(&mut line)
        .map_err(|e| io_err(_interp, format!("io_stdin failed: {}", e)))?;
    // Trim trailing newline
    if line.ends_with('\n') {
        line.pop();
        if line.ends_with('\r') {
            line.pop();
        }
    }
    Ok(Value::Str(line))
}

// ── io_read_lines(path: string) -> array ──

fn builtin_io_read_lines(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "io_read_lines")?;
    let content = std::fs::read_to_string(path)
        .map_err(|e| io_err(interp, format!("io_read_lines failed: {}", e)))?;
    let lines: Vec<Value> = content.lines().map(|l| Value::Str(l.to_string())).collect();
    Ok(Value::Array(lines))
}

// ── io_write_bytes(path: string, bytes: array) ──

fn builtin_io_write_bytes(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "io_write_bytes")?.to_string();
    let bytes_arr = match args.get(1) {
        Some(Value::Array(a)) => a,
        Some(_) => return Err(type_err(interp, "io_write_bytes() second argument must be an array".into())),
        None => return Err(type_err(interp, "io_write_bytes() requires 2 arguments".into())),
    };

    let mut bytes = Vec::with_capacity(bytes_arr.len());
    for v in bytes_arr {
        match v {
            Value::Int(n) => bytes.push(*n as u8),
            Value::Byte(b) => bytes.push(*b),
            _ => return Err(type_err(interp, "io_write_bytes() array must contain ints or bytes".into())),
        }
    }

    std::fs::write(&path, &bytes)
        .map_err(|e| io_err(interp, format!("io_write_bytes failed: {}", e)))?;
    Ok(Value::Void)
}

// ── io_pipe(cmd: string) -> string ──

fn builtin_io_pipe(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let cmd = require_str(interp, &args, 0, "io_pipe")?;

    let output = if cfg!(target_os = "windows") {
        std::process::Command::new("cmd")
            .args(["/C", cmd])
            .output()
    } else {
        std::process::Command::new("sh")
            .args(["-c", cmd])
            .output()
    };

    match output {
        Ok(out) => {
            let stdout = String::from_utf8_lossy(&out.stdout).to_string();
            let stderr = String::from_utf8_lossy(&out.stderr).to_string();
            if out.status.success() {
                Ok(Value::Str(stdout))
            } else {
                Err(io_err(interp, format!("io_pipe command failed (exit {}): {}", out.status, stderr)))
            }
        }
        Err(e) => Err(io_err(interp, format!("io_pipe failed: {}", e))),
    }
}

// ── io_tempfile() -> string ──

fn builtin_io_tempfile(interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let mut dir = std::env::temp_dir();
    let id = NEXT_READER_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let name = format!("hexa_tmp_{}", id);
    dir.push(&name);

    // Create the file
    std::fs::File::create(&dir)
        .map_err(|e| io_err(interp, format!("io_tempfile failed: {}", e)))?;

    match dir.to_str() {
        Some(s) => Ok(Value::Str(s.to_string())),
        None => Err(io_err(interp, "io_tempfile: path is not valid UTF-8".into())),
    }
}

// ── io_buffered_reader(path: string) -> map ──

fn builtin_io_buffered_reader(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "io_buffered_reader")?;
    let file = File::open(path)
        .map_err(|e| io_err(interp, format!("io_buffered_reader failed: {}", e)))?;
    let reader = BufReader::new(file);

    let id = NEXT_READER_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    get_readers().lock().unwrap().insert(id, reader);

    let mut handle = HashMap::new();
    handle.insert("_type".into(), Value::Str("buffered_reader".into()));
    handle.insert("_id".into(), Value::Int(id as i64));
    handle.insert("path".into(), Value::Str(path.to_string()));
    Ok(Value::Map(handle))
}

// ── io_reader_next(reader: map) -> string | void ──

fn builtin_io_reader_next(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let handle = match args.get(0) {
        Some(Value::Map(m)) => m,
        _ => return Err(type_err(interp, "io_reader_next() requires a buffered_reader handle".into())),
    };

    // Validate it's a reader handle
    let is_reader = matches!(handle.get("_type"), Some(Value::Str(s)) if s == "buffered_reader");
    if !is_reader {
        return Err(type_err(interp, "io_reader_next() requires a buffered_reader handle".into()));
    }

    let id = match handle.get("_id") {
        Some(Value::Int(n)) => *n as u64,
        _ => return Err(type_err(interp, "io_reader_next(): invalid reader handle".into())),
    };

    let mut readers = get_readers().lock().unwrap();
    let reader = match readers.get_mut(&id) {
        Some(r) => r,
        None => return Err(io_err(interp, "io_reader_next(): reader handle expired or closed".into())),
    };

    let mut line = String::new();
    match reader.read_line(&mut line) {
        Ok(0) => {
            // EOF — remove from registry
            readers.remove(&id);
            Ok(Value::Void)
        }
        Ok(_) => {
            // Trim trailing newline
            if line.ends_with('\n') {
                line.pop();
                if line.ends_with('\r') {
                    line.pop();
                }
            }
            Ok(Value::Str(line))
        }
        Err(e) => Err(io_err(interp, format!("io_reader_next failed: {}", e))),
    }
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_io_read_lines() {
        let dir = std::env::temp_dir().join("hexa_test_io");
        let _ = std::fs::create_dir_all(&dir);
        let file = dir.join("lines.txt");

        std::fs::write(&file, "line1\nline2\nline3").unwrap();
        let content = std::fs::read_to_string(&file).unwrap();
        let lines: Vec<&str> = content.lines().collect();
        assert_eq!(lines, vec!["line1", "line2", "line3"]);

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_io_tempfile() {
        let id = NEXT_READER_ID.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let mut dir = std::env::temp_dir();
        let name = format!("hexa_tmp_{}", id);
        dir.push(&name);

        std::fs::File::create(&dir).unwrap();
        assert!(dir.exists());

        let _ = std::fs::remove_file(&dir);
    }

    #[test]
    fn test_io_write_bytes_and_read() {
        let dir = std::env::temp_dir().join("hexa_test_io_bytes");
        let _ = std::fs::create_dir_all(&dir);
        let file = dir.join("bytes.bin");

        let data: Vec<u8> = vec![72, 69, 88, 65]; // "HEXA"
        std::fs::write(&file, &data).unwrap();
        let read_back = std::fs::read(&file).unwrap();
        assert_eq!(read_back, data);
        assert_eq!(std::str::from_utf8(&read_back).unwrap(), "HEXA");

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_io_buffered_reader() {
        let dir = std::env::temp_dir().join("hexa_test_io_bufreader");
        let _ = std::fs::create_dir_all(&dir);
        let file = dir.join("buf.txt");

        std::fs::write(&file, "alpha\nbeta\ngamma\n").unwrap();

        let f = File::open(&file).unwrap();
        let mut reader = BufReader::new(f);

        let mut line = String::new();
        reader.read_line(&mut line).unwrap();
        assert_eq!(line.trim_end(), "alpha");

        line.clear();
        reader.read_line(&mut line).unwrap();
        assert_eq!(line.trim_end(), "beta");

        line.clear();
        reader.read_line(&mut line).unwrap();
        assert_eq!(line.trim_end(), "gamma");

        line.clear();
        let n = reader.read_line(&mut line).unwrap();
        assert_eq!(n, 0); // EOF

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_io_pipe() {
        let output = std::process::Command::new("sh")
            .args(["-c", "echo hello_hexa"])
            .output()
            .unwrap();
        let stdout = String::from_utf8_lossy(&output.stdout).to_string();
        assert!(stdout.contains("hello_hexa"));
    }
}

//! std::fs — Filesystem builtins for HEXA-LANG.
//!
//! Provides:
//!   fs_read(path)             — read file to string
//!   fs_write(path, content)   — write string to file
//!   fs_append(path, content)  — append to file
//!   fs_exists(path)           — check if path exists
//!   fs_remove(path)           — delete file
//!   fs_mkdir(path)            — create directory (recursive)
//!   fs_list(path)             — list directory entries
//!   fs_copy(src, dst)         — copy file
//!   fs_move(src, dst)         — rename/move
//!   fs_metadata(path)         — size, modified, is_dir, is_file
//!   fs_glob(pattern)          — simple glob matching
//!   fs_watch(path, callback)  — polling-based file watcher

#![allow(dead_code)]

use std::collections::HashMap;
use std::fs;
use std::io::Write as IoWrite;
use std::path::Path;
use std::time::SystemTime;

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a filesystem builtin by name.  Called from `Interpreter::call_builtin`.
pub fn call_fs_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "fs_read" => builtin_fs_read(interp, args),
        "fs_write" => builtin_fs_write(interp, args),
        "fs_append" => builtin_fs_append(interp, args),
        "fs_exists" => builtin_fs_exists(interp, args),
        "fs_remove" => builtin_fs_remove(interp, args),
        "fs_mkdir" => builtin_fs_mkdir(interp, args),
        "fs_list" => builtin_fs_list(interp, args),
        "fs_copy" => builtin_fs_copy(interp, args),
        "fs_move" => builtin_fs_move(interp, args),
        "fs_metadata" => builtin_fs_metadata(interp, args),
        "fs_glob" => builtin_fs_glob(interp, args),
        "fs_watch" => builtin_fs_watch(interp, args),
        _ => Err(fs_err(interp, format!("unknown fs builtin: {}", name))),
    }
}

// ── Helpers ─────────────────────────────────────────────────

fn fs_err(interp: &Interpreter, msg: String) -> HexaError {
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

// ── fs_read(path: string) -> string ──

fn builtin_fs_read(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "fs_read")?;
    match fs::read_to_string(path) {
        Ok(contents) => Ok(Value::Str(contents)),
        Err(e) => Err(fs_err(interp, format!("fs_read failed: {}", e))),
    }
}

// ── fs_write(path: string, content: string) ──

fn builtin_fs_write(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "fs_write")?.to_string();
    let content = require_str(interp, &args, 1, "fs_write")?;
    match fs::write(&path, content) {
        Ok(_) => Ok(Value::Void),
        Err(e) => Err(fs_err(interp, format!("fs_write failed: {}", e))),
    }
}

// ── fs_append(path: string, content: string) ──

fn builtin_fs_append(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "fs_append")?.to_string();
    let content = require_str(interp, &args, 1, "fs_append")?;
    let mut file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .map_err(|e| fs_err(interp, format!("fs_append failed: {}", e)))?;
    file.write_all(content.as_bytes())
        .map_err(|e| fs_err(interp, format!("fs_append write failed: {}", e)))?;
    Ok(Value::Void)
}

// ── fs_exists(path: string) -> bool ──

fn builtin_fs_exists(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "fs_exists")?;
    Ok(Value::Bool(Path::new(path).exists()))
}

// ── fs_remove(path: string) ──

fn builtin_fs_remove(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "fs_remove")?.to_string();
    let p = Path::new(&path);
    if p.is_dir() {
        fs::remove_dir_all(p)
            .map_err(|e| fs_err(interp, format!("fs_remove dir failed: {}", e)))?;
    } else {
        fs::remove_file(p)
            .map_err(|e| fs_err(interp, format!("fs_remove file failed: {}", e)))?;
    }
    Ok(Value::Void)
}

// ── fs_mkdir(path: string) ──

fn builtin_fs_mkdir(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "fs_mkdir")?;
    fs::create_dir_all(path)
        .map_err(|e| fs_err(interp, format!("fs_mkdir failed: {}", e)))?;
    Ok(Value::Void)
}

// ── fs_list(path: string) -> array ──

fn builtin_fs_list(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "fs_list")?;
    let entries = fs::read_dir(path)
        .map_err(|e| fs_err(interp, format!("fs_list failed: {}", e)))?;
    let mut result = Vec::new();
    for entry in entries {
        let entry = entry.map_err(|e| fs_err(interp, format!("fs_list entry error: {}", e)))?;
        if let Some(name) = entry.file_name().to_str() {
            result.push(Value::Str(name.to_string()));
        }
    }
    result.sort_by(|a, b| {
        if let (Value::Str(sa), Value::Str(sb)) = (a, b) {
            sa.cmp(sb)
        } else {
            std::cmp::Ordering::Equal
        }
    });
    Ok(Value::Array(result))
}

// ── fs_copy(src: string, dst: string) ──

fn builtin_fs_copy(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let src = require_str(interp, &args, 0, "fs_copy")?.to_string();
    let dst = require_str(interp, &args, 1, "fs_copy")?;
    fs::copy(&src, dst)
        .map_err(|e| fs_err(interp, format!("fs_copy failed: {}", e)))?;
    Ok(Value::Void)
}

// ── fs_move(src: string, dst: string) ──

fn builtin_fs_move(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let src = require_str(interp, &args, 0, "fs_move")?.to_string();
    let dst = require_str(interp, &args, 1, "fs_move")?;
    fs::rename(&src, dst)
        .map_err(|e| fs_err(interp, format!("fs_move failed: {}", e)))?;
    Ok(Value::Void)
}

// ── fs_metadata(path: string) -> map ──

fn builtin_fs_metadata(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let path = require_str(interp, &args, 0, "fs_metadata")?;
    let meta = fs::metadata(path)
        .map_err(|e| fs_err(interp, format!("fs_metadata failed: {}", e)))?;
    let mut map = HashMap::new();
    map.insert("size".into(), Value::Int(meta.len() as i64));
    map.insert("is_dir".into(), Value::Bool(meta.is_dir()));
    map.insert("is_file".into(), Value::Bool(meta.is_file()));
    map.insert("readonly".into(), Value::Bool(meta.permissions().readonly()));

    if let Ok(modified) = meta.modified() {
        if let Ok(duration) = modified.duration_since(SystemTime::UNIX_EPOCH) {
            map.insert("modified".into(), Value::Int(duration.as_secs() as i64));
        }
    }
    if let Ok(created) = meta.created() {
        if let Ok(duration) = created.duration_since(SystemTime::UNIX_EPOCH) {
            map.insert("created".into(), Value::Int(duration.as_secs() as i64));
        }
    }

    Ok(Value::Map(map))
}

// ── fs_glob(pattern: string) -> array ──
//
// Simple glob: supports `*` (any chars) and `?` (single char) in the filename part.
// The directory part (everything before the last `/`) is used as-is.

fn builtin_fs_glob(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let pattern = require_str(interp, &args, 0, "fs_glob")?.to_string();

    let (dir, file_pattern) = if let Some(sep) = pattern.rfind('/') {
        (&pattern[..sep], &pattern[sep + 1..])
    } else if let Some(sep) = pattern.rfind('\\') {
        (&pattern[..sep], &pattern[sep + 1..])
    } else {
        (".", pattern.as_str())
    };

    let regex_pattern = glob_to_regex(file_pattern);
    let re = regex_pattern; // simple string match

    let entries = fs::read_dir(dir)
        .map_err(|e| fs_err(interp, format!("fs_glob failed to read dir '{}': {}", dir, e)))?;

    let mut result = Vec::new();
    for entry in entries {
        let entry = entry.map_err(|e| fs_err(interp, format!("fs_glob entry error: {}", e)))?;
        if let Some(name) = entry.file_name().to_str() {
            if glob_matches(&re, name) {
                let full_path = entry.path();
                if let Some(p) = full_path.to_str() {
                    result.push(Value::Str(p.to_string()));
                }
            }
        }
    }
    result.sort_by(|a, b| {
        if let (Value::Str(sa), Value::Str(sb)) = (a, b) {
            sa.cmp(sb)
        } else {
            std::cmp::Ordering::Equal
        }
    });
    Ok(Value::Array(result))
}

/// Convert a simple glob pattern to a list of segments for matching.
/// Supports `*` (match any sequence) and `?` (match single char).
#[derive(Debug)]
enum GlobSegment {
    Literal(String),
    Any,        // *
    Single,     // ?
}

fn glob_to_regex(pattern: &str) -> Vec<GlobSegment> {
    let mut segments = Vec::new();
    let mut literal = String::new();
    for ch in pattern.chars() {
        match ch {
            '*' => {
                if !literal.is_empty() {
                    segments.push(GlobSegment::Literal(std::mem::take(&mut literal)));
                }
                segments.push(GlobSegment::Any);
            }
            '?' => {
                if !literal.is_empty() {
                    segments.push(GlobSegment::Literal(std::mem::take(&mut literal)));
                }
                segments.push(GlobSegment::Single);
            }
            c => literal.push(c),
        }
    }
    if !literal.is_empty() {
        segments.push(GlobSegment::Literal(literal));
    }
    segments
}

fn glob_matches(segments: &[GlobSegment], text: &str) -> bool {
    glob_matches_inner(segments, text.as_bytes(), 0)
}

fn glob_matches_inner(segments: &[GlobSegment], text: &[u8], seg_idx: usize) -> bool {
    if seg_idx >= segments.len() {
        return text.is_empty();
    }
    match &segments[seg_idx] {
        GlobSegment::Literal(lit) => {
            let lit_bytes = lit.as_bytes();
            if text.len() >= lit_bytes.len() && &text[..lit_bytes.len()] == lit_bytes {
                glob_matches_inner(segments, &text[lit_bytes.len()..], seg_idx + 1)
            } else {
                false
            }
        }
        GlobSegment::Single => {
            if text.is_empty() {
                false
            } else {
                // skip one UTF-8 char worth of bytes
                let ch_len = utf8_char_len(text[0]);
                if text.len() >= ch_len {
                    glob_matches_inner(segments, &text[ch_len..], seg_idx + 1)
                } else {
                    false
                }
            }
        }
        GlobSegment::Any => {
            // Try matching rest of segments at every position
            for i in 0..=text.len() {
                if glob_matches_inner(segments, &text[i..], seg_idx + 1) {
                    return true;
                }
            }
            false
        }
    }
}

fn utf8_char_len(first_byte: u8) -> usize {
    if first_byte < 0x80 { 1 }
    else if first_byte < 0xE0 { 2 }
    else if first_byte < 0xF0 { 3 }
    else { 4 }
}

// ── fs_watch(path: string, callback: fn) ──
//
// Polling-based file watcher. Checks for changes every ~500ms.
// Calls callback(event_map) when a change is detected.
// The callback should return "__stop__" to stop watching.
// event_map has keys: path, kind ("modified"|"created"|"deleted").

fn builtin_fs_watch(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    if args.len() < 2 {
        return Err(type_err(interp, "fs_watch() requires 2 arguments (path, callback)".into()));
    }
    let path = match &args[0] {
        Value::Str(s) => s.clone(),
        _ => return Err(type_err(interp, "fs_watch() first argument must be string".into())),
    };
    let callback = args[1].clone();
    match &callback {
        Value::Fn(..) | Value::Lambda(..) | Value::BuiltinFn(_) => {}
        _ => return Err(type_err(interp, "fs_watch() second argument must be a function".into())),
    }

    // Snapshot current state
    let mut snapshot = build_snapshot(&path);

    loop {
        std::thread::sleep(std::time::Duration::from_millis(500));

        let current = build_snapshot(&path);

        // Detect changes
        let mut events = Vec::new();

        // New or modified files
        for (name, mtime) in &current {
            match snapshot.get(name) {
                None => events.push((name.clone(), "created")),
                Some(old_mtime) if old_mtime != mtime => events.push((name.clone(), "modified")),
                _ => {}
            }
        }
        // Deleted files
        for name in snapshot.keys() {
            if !current.contains_key(name) {
                events.push((name.clone(), "deleted"));
            }
        }

        for (event_path, kind) in events {
            let mut event_map = HashMap::new();
            event_map.insert("path".into(), Value::Str(event_path));
            event_map.insert("kind".into(), Value::Str(kind.to_string()));

            let result = interp.call_function(callback.clone(), vec![Value::Map(event_map)])?;
            if let Value::Str(s) = &result {
                if s == "__stop__" {
                    return Ok(Value::Void);
                }
            }
        }

        snapshot = current;
    }
}

/// Build a filename -> modified_time snapshot for a path (file or directory).
fn build_snapshot(path: &str) -> HashMap<String, u64> {
    let mut map = HashMap::new();
    let p = Path::new(path);
    if p.is_dir() {
        if let Ok(entries) = fs::read_dir(p) {
            for entry in entries.flatten() {
                if let (Some(name), Ok(meta)) = (entry.file_name().to_str().map(|s| s.to_string()), entry.metadata()) {
                    let mtime = meta.modified()
                        .ok()
                        .and_then(|t| t.duration_since(SystemTime::UNIX_EPOCH).ok())
                        .map(|d| d.as_secs())
                        .unwrap_or(0);
                    map.insert(name, mtime);
                }
            }
        }
    } else if p.exists() {
        let mtime = fs::metadata(p)
            .ok()
            .and_then(|m| m.modified().ok())
            .and_then(|t| t.duration_since(SystemTime::UNIX_EPOCH).ok())
            .map(|d| d.as_secs())
            .unwrap_or(0);
        map.insert(path.to_string(), mtime);
    }
    map
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_glob_matching() {
        let segs = glob_to_regex("*.txt");
        assert!(glob_matches(&segs, "hello.txt"));
        assert!(glob_matches(&segs, ".txt"));
        assert!(!glob_matches(&segs, "hello.rs"));

        let segs = glob_to_regex("test_?.rs");
        assert!(glob_matches(&segs, "test_a.rs"));
        assert!(!glob_matches(&segs, "test_ab.rs"));
        assert!(!glob_matches(&segs, "test_.rs"));

        let segs = glob_to_regex("*");
        assert!(glob_matches(&segs, "anything"));
        assert!(glob_matches(&segs, ""));

        let segs = glob_to_regex("hello");
        assert!(glob_matches(&segs, "hello"));
        assert!(!glob_matches(&segs, "hello2"));
    }

    #[test]
    fn test_fs_write_read_exists_remove_roundtrip() {
        let dir = std::env::temp_dir().join("hexa_test_fs");
        let _ = fs::create_dir_all(&dir);
        let file = dir.join("roundtrip.txt");
        let path_str = file.to_str().unwrap();

        // Write
        fs::write(path_str, "hello hexa fs").unwrap();
        // Read
        let content = fs::read_to_string(path_str).unwrap();
        assert_eq!(content, "hello hexa fs");
        // Exists
        assert!(Path::new(path_str).exists());
        // Remove
        fs::remove_file(path_str).unwrap();
        assert!(!Path::new(path_str).exists());

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_fs_mkdir_list() {
        let dir = std::env::temp_dir().join("hexa_test_fs_mkdir");
        let _ = fs::remove_dir_all(&dir);

        // mkdir
        fs::create_dir_all(dir.join("sub1")).unwrap();
        fs::create_dir_all(dir.join("sub2")).unwrap();
        fs::write(dir.join("file.txt"), "x").unwrap();

        // list
        let mut entries: Vec<String> = fs::read_dir(&dir)
            .unwrap()
            .filter_map(|e| e.ok().and_then(|e| e.file_name().to_str().map(|s| s.to_string())))
            .collect();
        entries.sort();
        assert_eq!(entries, vec!["file.txt", "sub1", "sub2"]);

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_fs_metadata() {
        let dir = std::env::temp_dir().join("hexa_test_fs_meta");
        let _ = fs::create_dir_all(&dir);
        let file = dir.join("meta.txt");
        fs::write(&file, "metadata test content").unwrap();

        let meta = fs::metadata(&file).unwrap();
        assert!(meta.is_file());
        assert!(!meta.is_dir());
        assert_eq!(meta.len(), 21); // "metadata test content" = 21 bytes

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_fs_append() {
        let dir = std::env::temp_dir().join("hexa_test_fs_append");
        let _ = fs::create_dir_all(&dir);
        let file = dir.join("append.txt");

        fs::write(&file, "line1\n").unwrap();
        {
            let mut f = std::fs::OpenOptions::new().append(true).open(&file).unwrap();
            f.write_all(b"line2\n").unwrap();
        }
        let content = fs::read_to_string(&file).unwrap();
        assert_eq!(content, "line1\nline2\n");

        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn test_fs_copy_move() {
        let dir = std::env::temp_dir().join("hexa_test_fs_copymove");
        let _ = fs::remove_dir_all(&dir);
        let _ = fs::create_dir_all(&dir);

        let src = dir.join("orig.txt");
        let dst = dir.join("copy.txt");
        let moved = dir.join("moved.txt");

        fs::write(&src, "copy me").unwrap();
        fs::copy(&src, &dst).unwrap();
        assert_eq!(fs::read_to_string(&dst).unwrap(), "copy me");

        fs::rename(&dst, &moved).unwrap();
        assert!(!dst.exists());
        assert_eq!(fs::read_to_string(&moved).unwrap(), "copy me");

        let _ = fs::remove_dir_all(&dir);
    }
}

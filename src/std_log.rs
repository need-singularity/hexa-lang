//! std::log — Structured logging builtins for HEXA-LANG.
//!
//! Provides:
//!   log_debug(msg)        — log at DEBUG level
//!   log_info(msg)         — log at INFO level
//!   log_warn(msg)         — log at WARN level
//!   log_error(msg)        — log at ERROR level
//!   log_set_level(level)  — set minimum log level ("debug"|"info"|"warn"|"error"|"off")
//!   log_get_entries()     — get all log entries as array of maps

#![allow(dead_code)]

use std::collections::HashMap;
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a log builtin by name.
pub fn call_log_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "log_debug" => builtin_log_msg(interp, args, LogLevel::Debug),
        "log_info" => builtin_log_msg(interp, args, LogLevel::Info),
        "log_warn" => builtin_log_msg(interp, args, LogLevel::Warn),
        "log_error" => builtin_log_msg(interp, args, LogLevel::Error),
        "log_set_level" => builtin_log_set_level(interp, args),
        "log_get_entries" => builtin_log_get_entries(interp, args),
        _ => Err(log_err(interp, format!("unknown log builtin: {}", name))),
    }
}

fn log_err(interp: &Interpreter, msg: String) -> HexaError {
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

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
enum LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Off = 4,
}

impl LogLevel {
    fn as_str(&self) -> &'static str {
        match self {
            LogLevel::Debug => "DEBUG",
            LogLevel::Info => "INFO",
            LogLevel::Warn => "WARN",
            LogLevel::Error => "ERROR",
            LogLevel::Off => "OFF",
        }
    }

    fn from_str(s: &str) -> Option<LogLevel> {
        match s.to_lowercase().as_str() {
            "debug" => Some(LogLevel::Debug),
            "info" => Some(LogLevel::Info),
            "warn" | "warning" => Some(LogLevel::Warn),
            "error" => Some(LogLevel::Error),
            "off" | "none" => Some(LogLevel::Off),
            _ => None,
        }
    }
}

struct LogEntry {
    level: LogLevel,
    message: String,
    timestamp: u64,
}

mod log_state {
    use super::*;
    use std::sync::OnceLock;

    pub struct LogState {
        pub min_level: LogLevel,
        pub entries: Vec<LogEntry>,
    }

    static INSTANCE: OnceLock<Mutex<LogState>> = OnceLock::new();

    pub fn get() -> &'static Mutex<LogState> {
        INSTANCE.get_or_init(|| Mutex::new(LogState {
            min_level: LogLevel::Debug,
            entries: Vec::new(),
        }))
    }
}

fn builtin_log_msg(interp: &mut Interpreter, args: Vec<Value>, level: LogLevel) -> Result<Value, HexaError> {
    let msg = match args.first() {
        Some(Value::Str(s)) => s.clone(),
        Some(v) => format!("{}", v),
        None => return Err(type_err(interp, format!("{}() requires 1 argument", level.as_str().to_lowercase()))),
    };

    let timestamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    let mut state = log_state::get().lock().unwrap();
    if level >= state.min_level {
        eprintln!("[{}] {}", level.as_str(), msg);
        state.entries.push(LogEntry { level, message: msg, timestamp });
    }
    Ok(Value::Void)
}

fn builtin_log_set_level(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let level_str = match args.first() {
        Some(Value::Str(s)) => s.as_str(),
        Some(_) => return Err(type_err(interp, "log_set_level() requires string argument".into())),
        None => return Err(type_err(interp, "log_set_level() requires 1 argument".into())),
    };
    let level = LogLevel::from_str(level_str)
        .ok_or_else(|| log_err(interp, format!("unknown log level: {}", level_str)))?;
    log_state::get().lock().unwrap().min_level = level;
    Ok(Value::Void)
}

fn builtin_log_get_entries(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let state = log_state::get().lock().unwrap();
    let entries: Vec<Value> = state.entries.iter().map(|e| {
        let mut m = HashMap::new();
        m.insert("level".into(), Value::Str(e.level.as_str().to_string()));
        m.insert("message".into(), Value::Str(e.message.clone()));
        m.insert("timestamp".into(), Value::Int(e.timestamp as i64));
        Value::Map(Box::new(m))
    }).collect();
    Ok(Value::Array(entries))
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_log_level_ordering() {
        assert!(LogLevel::Debug < LogLevel::Info);
        assert!(LogLevel::Info < LogLevel::Warn);
        assert!(LogLevel::Warn < LogLevel::Error);
        assert!(LogLevel::Error < LogLevel::Off);
    }

    #[test]
    fn test_log_level_from_str() {
        assert_eq!(LogLevel::from_str("debug"), Some(LogLevel::Debug));
        assert_eq!(LogLevel::from_str("INFO"), Some(LogLevel::Info));
        assert_eq!(LogLevel::from_str("Warning"), Some(LogLevel::Warn));
        assert_eq!(LogLevel::from_str("ERROR"), Some(LogLevel::Error));
        assert_eq!(LogLevel::from_str("off"), Some(LogLevel::Off));
        assert_eq!(LogLevel::from_str("invalid"), None);
    }

    #[test]
    fn test_log_level_as_str() {
        assert_eq!(LogLevel::Debug.as_str(), "DEBUG");
        assert_eq!(LogLevel::Info.as_str(), "INFO");
        assert_eq!(LogLevel::Warn.as_str(), "WARN");
        assert_eq!(LogLevel::Error.as_str(), "ERROR");
    }
}

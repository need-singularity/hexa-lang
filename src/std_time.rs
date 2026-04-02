//! std::time — Time and date builtins for HEXA-LANG.
//!
//! Provides:
//!   time_now()             — current unix timestamp (seconds)
//!   time_now_ms()          — current unix timestamp (milliseconds)
//!   time_sleep(ms)         — sleep for N milliseconds
//!   time_format(ts, fmt)   — format timestamp as string
//!   time_parse(s, fmt)     — parse string to timestamp
//!   time_elapsed(start)    — milliseconds elapsed since start timestamp

#![allow(dead_code)]

use std::collections::HashMap;
use std::time::{SystemTime, UNIX_EPOCH, Duration};

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch a time builtin by name.
pub fn call_time_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "time_now" => builtin_time_now(interp, args),
        "time_now_ms" => builtin_time_now_ms(interp, args),
        "time_sleep" => builtin_time_sleep(interp, args),
        "time_format" => builtin_time_format(interp, args),
        "time_parse" => builtin_time_parse(interp, args),
        "time_elapsed" => builtin_time_elapsed(interp, args),
        _ => Err(time_err(interp, format!("unknown time builtin: {}", name))),
    }
}

fn time_err(interp: &Interpreter, msg: String) -> HexaError {
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

// ── time_now() -> int (unix seconds) ──

fn builtin_time_now(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_secs();
    Ok(Value::Int(secs as i64))
}

// ── time_now_ms() -> int (unix milliseconds) ──

fn builtin_time_now_ms(_interp: &mut Interpreter, _args: Vec<Value>) -> Result<Value, HexaError> {
    let ms = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis();
    Ok(Value::Int(ms as i64))
}

// ── time_sleep(ms: int) ──

fn builtin_time_sleep(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    match args.first() {
        Some(Value::Int(ms)) => {
            if *ms < 0 {
                return Err(type_err(interp, "time_sleep() requires non-negative milliseconds".into()));
            }
            std::thread::sleep(Duration::from_millis(*ms as u64));
            Ok(Value::Void)
        }
        Some(_) => Err(type_err(interp, "time_sleep() requires int argument".into())),
        None => Err(type_err(interp, "time_sleep() requires 1 argument".into())),
    }
}

// ── time_format(timestamp: int, fmt: string) -> string ──
//
// Simple format tokens: %Y (year), %m (month), %d (day), %H (hour), %M (minute), %S (second)

fn builtin_time_format(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let ts = match args.first() {
        Some(Value::Int(n)) => *n,
        Some(_) => return Err(type_err(interp, "time_format() first argument must be int".into())),
        None => return Err(type_err(interp, "time_format() requires 2 arguments".into())),
    };
    let fmt = match args.get(1) {
        Some(Value::Str(s)) => s.as_str(),
        Some(_) => return Err(type_err(interp, "time_format() second argument must be string".into())),
        None => "%Y-%m-%d %H:%M:%S",
    };

    let parts = timestamp_to_parts(ts);
    let result = fmt
        .replace("%Y", &format!("{:04}", parts.year))
        .replace("%m", &format!("{:02}", parts.month))
        .replace("%d", &format!("{:02}", parts.day))
        .replace("%H", &format!("{:02}", parts.hour))
        .replace("%M", &format!("{:02}", parts.minute))
        .replace("%S", &format!("{:02}", parts.second));

    Ok(Value::Str(result))
}

// ── time_parse(s: string, fmt: string) -> int ──
//
// Parse a formatted date string back to unix timestamp.
// Supports the same tokens as time_format.

fn builtin_time_parse(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let s = match args.first() {
        Some(Value::Str(s)) => s.as_str(),
        Some(_) => return Err(type_err(interp, "time_parse() first argument must be string".into())),
        None => return Err(type_err(interp, "time_parse() requires 2 arguments".into())),
    };
    let fmt = match args.get(1) {
        Some(Value::Str(f)) => f.as_str(),
        Some(_) => return Err(type_err(interp, "time_parse() second argument must be string".into())),
        None => "%Y-%m-%d %H:%M:%S",
    };

    // Simple parser: extract numbers by position matching the format string
    let mut year = 1970i64;
    let mut month = 1u32;
    let mut day = 1u32;
    let mut hour = 0u32;
    let mut minute = 0u32;
    let mut second = 0u32;

    let fmt_bytes = fmt.as_bytes();
    let s_bytes = s.as_bytes();
    let mut si = 0usize;
    let mut fi = 0usize;

    while fi < fmt_bytes.len() && si < s_bytes.len() {
        if fi + 1 < fmt_bytes.len() && fmt_bytes[fi] == b'%' {
            let token = fmt_bytes[fi + 1];
            fi += 2;
            match token {
                b'Y' => { let (v, adv) = parse_digits(s_bytes, si, 4); year = v as i64; si += adv; }
                b'm' => { let (v, adv) = parse_digits(s_bytes, si, 2); month = v as u32; si += adv; }
                b'd' => { let (v, adv) = parse_digits(s_bytes, si, 2); day = v as u32; si += adv; }
                b'H' => { let (v, adv) = parse_digits(s_bytes, si, 2); hour = v as u32; si += adv; }
                b'M' => { let (v, adv) = parse_digits(s_bytes, si, 2); minute = v as u32; si += adv; }
                b'S' => { let (v, adv) = parse_digits(s_bytes, si, 2); second = v as u32; si += adv; }
                _ => { si += 1; }
            }
        } else {
            fi += 1;
            si += 1;
        }
    }

    let ts = parts_to_timestamp(year, month, day, hour, minute, second);
    Ok(Value::Int(ts))
}

// ── time_elapsed(start_ms: int) -> int ──

fn builtin_time_elapsed(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let start = match args.first() {
        Some(Value::Int(n)) => *n,
        Some(_) => return Err(type_err(interp, "time_elapsed() requires int argument".into())),
        None => return Err(type_err(interp, "time_elapsed() requires 1 argument".into())),
    };
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::ZERO)
        .as_millis() as i64;
    Ok(Value::Int(now - start))
}

// ── Date math helpers (no external deps) ──

struct DateParts {
    year: i64,
    month: u32,
    day: u32,
    hour: u32,
    minute: u32,
    second: u32,
}

fn timestamp_to_parts(ts: i64) -> DateParts {
    let total_secs = if ts >= 0 { ts as u64 } else { 0 };
    let second = (total_secs % 60) as u32;
    let total_mins = total_secs / 60;
    let minute = (total_mins % 60) as u32;
    let total_hours = total_mins / 60;
    let hour = (total_hours % 24) as u32;
    let mut days = (total_hours / 24) as i64;

    // Calculate year/month/day from days since epoch (1970-01-01)
    let mut year = 1970i64;
    loop {
        let days_in_year = if is_leap(year) { 366 } else { 365 };
        if days < days_in_year {
            break;
        }
        days -= days_in_year;
        year += 1;
    }

    let month_days = if is_leap(year) {
        [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    } else {
        [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    };

    let mut month = 0u32;
    for (i, &md) in month_days.iter().enumerate() {
        if days < md as i64 {
            month = i as u32 + 1;
            break;
        }
        days -= md as i64;
    }
    if month == 0 { month = 12; }
    let day = days as u32 + 1;

    DateParts { year, month, day, hour, minute, second }
}

fn is_leap(year: i64) -> bool {
    (year % 4 == 0 && year % 100 != 0) || year % 400 == 0
}

fn parts_to_timestamp(year: i64, month: u32, day: u32, hour: u32, minute: u32, second: u32) -> i64 {
    let mut days: i64 = 0;
    // Years from 1970 to year
    for y in 1970..year {
        days += if is_leap(y) { 366 } else { 365 };
    }
    let month_days = if is_leap(year) {
        [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    } else {
        [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
    };
    for m in 0..(month.saturating_sub(1) as usize) {
        days += month_days[m] as i64;
    }
    days += (day.saturating_sub(1)) as i64;
    days * 86400 + hour as i64 * 3600 + minute as i64 * 60 + second as i64
}

fn parse_digits(bytes: &[u8], start: usize, max_len: usize) -> (i64, usize) {
    let mut val = 0i64;
    let mut count = 0;
    for i in 0..max_len {
        if start + i >= bytes.len() { break; }
        let b = bytes[start + i];
        if b.is_ascii_digit() {
            val = val * 10 + (b - b'0') as i64;
            count += 1;
        } else {
            break;
        }
    }
    (val, count)
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_timestamp_to_parts_epoch() {
        let p = timestamp_to_parts(0);
        assert_eq!((p.year, p.month, p.day, p.hour, p.minute, p.second), (1970, 1, 1, 0, 0, 0));
    }

    #[test]
    fn test_timestamp_roundtrip() {
        // 2024-01-15 12:30:45
        let ts = parts_to_timestamp(2024, 1, 15, 12, 30, 45);
        let p = timestamp_to_parts(ts);
        assert_eq!((p.year, p.month, p.day, p.hour, p.minute, p.second), (2024, 1, 15, 12, 30, 45));
    }

    #[test]
    fn test_format_timestamp() {
        let ts = parts_to_timestamp(2026, 4, 2, 14, 30, 0);
        let p = timestamp_to_parts(ts);
        let formatted = "%Y-%m-%d %H:%M:%S"
            .replace("%Y", &format!("{:04}", p.year))
            .replace("%m", &format!("{:02}", p.month))
            .replace("%d", &format!("{:02}", p.day))
            .replace("%H", &format!("{:02}", p.hour))
            .replace("%M", &format!("{:02}", p.minute))
            .replace("%S", &format!("{:02}", p.second));
        assert_eq!(formatted, "2026-04-02 14:30:00");
    }

    #[test]
    fn test_parse_digits() {
        let (v, n) = parse_digits(b"2026", 0, 4);
        assert_eq!(v, 2026);
        assert_eq!(n, 4);
        let (v, n) = parse_digits(b"04", 0, 2);
        assert_eq!(v, 4);
        assert_eq!(n, 2);
    }

    #[test]
    fn test_parts_to_timestamp_and_back() {
        for &(y, m, d) in &[(1970, 1, 1), (2000, 2, 29), (2026, 12, 31)] {
            let ts = parts_to_timestamp(y, m, d, 0, 0, 0);
            let p = timestamp_to_parts(ts);
            assert_eq!((p.year, p.month as i64, p.day as i64), (y, m as i64, d as i64));
        }
    }
}

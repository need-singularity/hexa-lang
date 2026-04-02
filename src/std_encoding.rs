//! std::encoding — Encoding/decoding builtins for HEXA-LANG.
//!
//! Provides:
//!   base64_encode(s)     — encode string to base64
//!   base64_decode(s)     — decode base64 to string
//!   hex_encode(s)        — encode string to hex
//!   hex_decode(s)        — decode hex to string
//!   csv_parse(s)         — parse CSV string to array of arrays
//!   csv_format(rows)     — format array of arrays to CSV string
//!   url_encode(s)        — percent-encode a string
//!   url_decode(s)        — percent-decode a string

#![allow(dead_code)]

use crate::env::Value;
use crate::error::{HexaError, ErrorClass};
use crate::interpreter::Interpreter;

/// Dispatch an encoding builtin by name.
pub fn call_encoding_builtin(
    interp: &mut Interpreter,
    name: &str,
    args: Vec<Value>,
) -> Result<Value, HexaError> {
    match name {
        "base64_encode" => builtin_base64_encode(interp, args),
        "base64_decode" => builtin_base64_decode(interp, args),
        "hex_encode" => builtin_hex_encode(interp, args),
        "hex_decode" => builtin_hex_decode(interp, args),
        "csv_parse" => builtin_csv_parse(interp, args),
        "csv_format" => builtin_csv_format(interp, args),
        "url_encode" => builtin_url_encode(interp, args),
        "url_decode" => builtin_url_decode(interp, args),
        _ => Err(enc_err(interp, format!("unknown encoding builtin: {}", name))),
    }
}

fn enc_err(interp: &Interpreter, msg: String) -> HexaError {
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

// ── Base64 ──

const B64_CHARS: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

fn base64_encode_bytes(input: &[u8]) -> String {
    let mut out = String::with_capacity((input.len() + 2) / 3 * 4);
    for chunk in input.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = if chunk.len() > 1 { chunk[1] as u32 } else { 0 };
        let b2 = if chunk.len() > 2 { chunk[2] as u32 } else { 0 };
        let triple = (b0 << 16) | (b1 << 8) | b2;

        out.push(B64_CHARS[((triple >> 18) & 0x3F) as usize] as char);
        out.push(B64_CHARS[((triple >> 12) & 0x3F) as usize] as char);
        if chunk.len() > 1 {
            out.push(B64_CHARS[((triple >> 6) & 0x3F) as usize] as char);
        } else {
            out.push('=');
        }
        if chunk.len() > 2 {
            out.push(B64_CHARS[(triple & 0x3F) as usize] as char);
        } else {
            out.push('=');
        }
    }
    out
}

fn base64_decode_bytes(input: &str) -> Result<Vec<u8>, String> {
    let input = input.trim_end_matches('=');
    let mut out = Vec::new();
    let mut buf: u32 = 0;
    let mut bits = 0;

    for ch in input.chars() {
        let val = match ch {
            'A'..='Z' => ch as u32 - 'A' as u32,
            'a'..='z' => ch as u32 - 'a' as u32 + 26,
            '0'..='9' => ch as u32 - '0' as u32 + 52,
            '+' => 62,
            '/' => 63,
            ' ' | '\n' | '\r' | '\t' => continue,
            _ => return Err(format!("invalid base64 character: {}", ch)),
        };
        buf = (buf << 6) | val;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push((buf >> bits) as u8);
            buf &= (1 << bits) - 1;
        }
    }
    Ok(out)
}

fn builtin_base64_encode(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let s = require_str(interp, &args, 0, "base64_encode")?;
    Ok(Value::Str(base64_encode_bytes(s.as_bytes())))
}

fn builtin_base64_decode(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let s = require_str(interp, &args, 0, "base64_decode")?;
    match base64_decode_bytes(s) {
        Ok(bytes) => Ok(Value::Str(String::from_utf8_lossy(&bytes).to_string())),
        Err(e) => Err(enc_err(interp, format!("base64_decode failed: {}", e))),
    }
}

// ── Hex ──

fn builtin_hex_encode(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let s = require_str(interp, &args, 0, "hex_encode")?;
    let hex: String = s.bytes().map(|b| format!("{:02x}", b)).collect();
    Ok(Value::Str(hex))
}

fn builtin_hex_decode(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let s = require_str(interp, &args, 0, "hex_decode")?;
    let bytes: Result<Vec<u8>, _> = (0..s.len())
        .step_by(2)
        .map(|i| {
            if i + 2 > s.len() {
                Err(format!("hex_decode: odd-length string"))
            } else {
                u8::from_str_radix(&s[i..i + 2], 16)
                    .map_err(|e| format!("hex_decode: {}", e))
            }
        })
        .collect();
    match bytes {
        Ok(b) => Ok(Value::Str(String::from_utf8_lossy(&b).to_string())),
        Err(e) => Err(enc_err(interp, e)),
    }
}

// ── CSV ──

fn builtin_csv_parse(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let s = require_str(interp, &args, 0, "csv_parse")?;
    let rows: Vec<Value> = s.lines()
        .filter(|line| !line.is_empty())
        .map(|line| {
            let fields: Vec<Value> = parse_csv_line(line)
                .into_iter()
                .map(|f| Value::Str(f))
                .collect();
            Value::Array(fields)
        })
        .collect();
    Ok(Value::Array(rows))
}

fn parse_csv_line(line: &str) -> Vec<String> {
    let mut fields = Vec::new();
    let mut current = String::new();
    let mut in_quotes = false;
    let mut chars = line.chars().peekable();

    while let Some(ch) = chars.next() {
        if in_quotes {
            if ch == '"' {
                if chars.peek() == Some(&'"') {
                    chars.next();
                    current.push('"');
                } else {
                    in_quotes = false;
                }
            } else {
                current.push(ch);
            }
        } else {
            match ch {
                ',' => {
                    fields.push(std::mem::take(&mut current));
                }
                '"' => {
                    in_quotes = true;
                }
                _ => {
                    current.push(ch);
                }
            }
        }
    }
    fields.push(current);
    fields
}

fn builtin_csv_format(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let rows = match args.first() {
        Some(Value::Array(a)) => a,
        Some(_) => return Err(type_err(interp, "csv_format() requires array of arrays".into())),
        None => return Err(type_err(interp, "csv_format() requires 1 argument".into())),
    };
    let mut out = String::new();
    for row in rows {
        match row {
            Value::Array(fields) => {
                let line: Vec<String> = fields.iter().map(|f| {
                    let s = format!("{}", f);
                    if s.contains(',') || s.contains('"') || s.contains('\n') {
                        format!("\"{}\"", s.replace('"', "\"\""))
                    } else {
                        s
                    }
                }).collect();
                out.push_str(&line.join(","));
                out.push('\n');
            }
            _ => {
                out.push_str(&format!("{}\n", row));
            }
        }
    }
    Ok(Value::Str(out))
}

// ── URL encoding ──

fn builtin_url_encode(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let s = require_str(interp, &args, 0, "url_encode")?;
    let mut out = String::new();
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char);
            }
            _ => {
                out.push_str(&format!("%{:02X}", b));
            }
        }
    }
    Ok(Value::Str(out))
}

fn builtin_url_decode(interp: &mut Interpreter, args: Vec<Value>) -> Result<Value, HexaError> {
    let s = require_str(interp, &args, 0, "url_decode")?;
    let mut out = Vec::new();
    let bytes = s.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            if let Ok(b) = u8::from_str_radix(std::str::from_utf8(&bytes[i+1..i+3]).unwrap_or(""), 16) {
                out.push(b);
                i += 3;
                continue;
            }
        }
        if bytes[i] == b'+' {
            out.push(b' ');
        } else {
            out.push(bytes[i]);
        }
        i += 1;
    }
    Ok(Value::Str(String::from_utf8_lossy(&out).to_string()))
}

// ── Tests ──

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_base64_roundtrip() {
        let cases = &["", "f", "fo", "foo", "foobar", "Hello, World!"];
        for &input in cases {
            let encoded = base64_encode_bytes(input.as_bytes());
            let decoded = base64_decode_bytes(&encoded).unwrap();
            assert_eq!(std::str::from_utf8(&decoded).unwrap(), input, "failed for: {}", input);
        }
    }

    #[test]
    fn test_base64_known() {
        assert_eq!(base64_encode_bytes(b"Hello"), "SGVsbG8=");
        assert_eq!(base64_encode_bytes(b"HEXA"), "SEVYQQ==");
    }

    #[test]
    fn test_hex_roundtrip() {
        let input = "HEXA";
        let hex: String = input.bytes().map(|b| format!("{:02x}", b)).collect();
        assert_eq!(hex, "48455841");
        let bytes: Vec<u8> = (0..hex.len()).step_by(2)
            .map(|i| u8::from_str_radix(&hex[i..i+2], 16).unwrap())
            .collect();
        assert_eq!(std::str::from_utf8(&bytes).unwrap(), input);
    }

    #[test]
    fn test_csv_parse() {
        let rows = parse_csv_line("a,b,c");
        assert_eq!(rows, vec!["a", "b", "c"]);
        let rows = parse_csv_line("\"hello, world\",b,c");
        assert_eq!(rows, vec!["hello, world", "b", "c"]);
        let rows = parse_csv_line("a,\"b\"\"c\",d");
        assert_eq!(rows, vec!["a", "b\"c", "d"]);
    }

    #[test]
    fn test_url_encode_decode() {
        let input = "hello world&foo=bar";
        let mut encoded = String::new();
        for b in input.bytes() {
            match b {
                b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => encoded.push(b as char),
                _ => encoded.push_str(&format!("%{:02X}", b)),
            }
        }
        assert_eq!(encoded, "hello%20world%26foo%3Dbar");

        // Decode
        let mut out = Vec::new();
        let bytes = encoded.as_bytes();
        let mut i = 0;
        while i < bytes.len() {
            if bytes[i] == b'%' && i + 2 < bytes.len() {
                if let Ok(b) = u8::from_str_radix(std::str::from_utf8(&bytes[i+1..i+3]).unwrap_or(""), 16) {
                    out.push(b);
                    i += 3;
                    continue;
                }
            }
            out.push(bytes[i]);
            i += 1;
        }
        assert_eq!(std::str::from_utf8(&out).unwrap(), input);
    }
}

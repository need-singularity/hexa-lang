#![allow(dead_code)]

//! Minimal LSP server for HEXA-LANG.
//!
//! Speaks JSON-RPC over stdin/stdout with `Content-Length` framing.
//! Supports:
//!   - initialize / shutdown / exit
//!   - textDocument/didOpen, textDocument/didChange  (diagnostics)
//!   - textDocument/completion                        (keywords + builtins)

use std::io::{self, BufRead, Write};
use std::collections::HashMap;

use crate::lexer::Lexer;
use crate::parser::Parser;
use crate::type_checker::TypeChecker;
use crate::error::HexaError;

// ── JSON helpers (minimal, no serde required at runtime) ───────

/// A minimal JSON value type for LSP message handling.
#[derive(Debug, Clone)]
pub enum JsonValue {
    Null,
    Bool(bool),
    Number(f64),
    Str(String),
    Array(Vec<JsonValue>),
    Object(Vec<(String, JsonValue)>),
}

impl JsonValue {
    pub fn get(&self, key: &str) -> Option<&JsonValue> {
        match self {
            JsonValue::Object(pairs) => {
                for (k, v) in pairs {
                    if k == key {
                        return Some(v);
                    }
                }
                None
            }
            _ => None,
        }
    }

    pub fn as_str(&self) -> Option<&str> {
        match self {
            JsonValue::Str(s) => Some(s.as_str()),
            _ => None,
        }
    }

    pub fn as_i64(&self) -> Option<i64> {
        match self {
            JsonValue::Number(n) => Some(*n as i64),
            _ => None,
        }
    }

    /// Serialize to JSON string.
    pub fn to_json(&self) -> String {
        match self {
            JsonValue::Null => "null".to_string(),
            JsonValue::Bool(b) => if *b { "true".to_string() } else { "false".to_string() },
            JsonValue::Number(n) => {
                if *n == (*n as i64) as f64 {
                    format!("{}", *n as i64)
                } else {
                    format!("{}", n)
                }
            }
            JsonValue::Str(s) => format!("\"{}\"", escape_json_str(s)),
            JsonValue::Array(items) => {
                let parts: Vec<String> = items.iter().map(|v| v.to_json()).collect();
                format!("[{}]", parts.join(","))
            }
            JsonValue::Object(pairs) => {
                let parts: Vec<String> = pairs.iter()
                    .map(|(k, v)| format!("\"{}\":{}", escape_json_str(k), v.to_json()))
                    .collect();
                format!("{{{}}}", parts.join(","))
            }
        }
    }
}

fn escape_json_str(s: &str) -> String {
    let mut out = String::new();
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                out.push_str(&format!("\\u{:04x}", c as u32));
            }
            c => out.push(c),
        }
    }
    out
}

/// Parse a JSON string into JsonValue. Minimal parser.
pub fn parse_json(input: &str) -> Result<JsonValue, String> {
    let chars: Vec<char> = input.chars().collect();
    let (val, _) = parse_json_value(&chars, 0)?;
    Ok(val)
}

fn skip_ws(chars: &[char], mut pos: usize) -> usize {
    while pos < chars.len() && chars[pos].is_ascii_whitespace() {
        pos += 1;
    }
    pos
}

fn parse_json_value(chars: &[char], pos: usize) -> Result<(JsonValue, usize), String> {
    let pos = skip_ws(chars, pos);
    if pos >= chars.len() {
        return Err("unexpected end of JSON".into());
    }
    match chars[pos] {
        '"' => parse_json_string(chars, pos),
        '{' => parse_json_object(chars, pos),
        '[' => parse_json_array(chars, pos),
        't' => {
            if chars[pos..].iter().take(4).collect::<String>() == "true" {
                Ok((JsonValue::Bool(true), pos + 4))
            } else {
                Err(format!("unexpected token at {}", pos))
            }
        }
        'f' => {
            if chars[pos..].iter().take(5).collect::<String>() == "false" {
                Ok((JsonValue::Bool(false), pos + 5))
            } else {
                Err(format!("unexpected token at {}", pos))
            }
        }
        'n' => {
            if chars[pos..].iter().take(4).collect::<String>() == "null" {
                Ok((JsonValue::Null, pos + 4))
            } else {
                Err(format!("unexpected token at {}", pos))
            }
        }
        c if c == '-' || c.is_ascii_digit() => parse_json_number(chars, pos),
        c => Err(format!("unexpected char '{}' at {}", c, pos)),
    }
}

fn parse_json_string(chars: &[char], pos: usize) -> Result<(JsonValue, usize), String> {
    let (s, end) = parse_json_string_raw(chars, pos)?;
    Ok((JsonValue::Str(s), end))
}

fn parse_json_string_raw(chars: &[char], mut pos: usize) -> Result<(String, usize), String> {
    if chars[pos] != '"' {
        return Err("expected '\"'".into());
    }
    pos += 1;
    let mut s = String::new();
    while pos < chars.len() {
        match chars[pos] {
            '"' => return Ok((s, pos + 1)),
            '\\' => {
                pos += 1;
                if pos >= chars.len() { return Err("unterminated string escape".into()); }
                match chars[pos] {
                    '"' => s.push('"'),
                    '\\' => s.push('\\'),
                    '/' => s.push('/'),
                    'n' => s.push('\n'),
                    'r' => s.push('\r'),
                    't' => s.push('\t'),
                    'u' => {
                        // Parse 4 hex digits
                        if pos + 4 >= chars.len() { return Err("incomplete unicode escape".into()); }
                        let hex: String = chars[pos+1..pos+5].iter().collect();
                        let code = u32::from_str_radix(&hex, 16).map_err(|_| "invalid unicode escape")?;
                        if let Some(c) = char::from_u32(code) {
                            s.push(c);
                        }
                        pos += 4;
                    }
                    c => s.push(c),
                }
                pos += 1;
            }
            c => { s.push(c); pos += 1; }
        }
    }
    Err("unterminated string".into())
}

fn parse_json_number(chars: &[char], mut pos: usize) -> Result<(JsonValue, usize), String> {
    let start = pos;
    if pos < chars.len() && chars[pos] == '-' { pos += 1; }
    while pos < chars.len() && chars[pos].is_ascii_digit() { pos += 1; }
    if pos < chars.len() && chars[pos] == '.' {
        pos += 1;
        while pos < chars.len() && chars[pos].is_ascii_digit() { pos += 1; }
    }
    if pos < chars.len() && (chars[pos] == 'e' || chars[pos] == 'E') {
        pos += 1;
        if pos < chars.len() && (chars[pos] == '+' || chars[pos] == '-') { pos += 1; }
        while pos < chars.len() && chars[pos].is_ascii_digit() { pos += 1; }
    }
    let num_str: String = chars[start..pos].iter().collect();
    let n: f64 = num_str.parse().map_err(|_| format!("invalid number: {}", num_str))?;
    Ok((JsonValue::Number(n), pos))
}

fn parse_json_object(chars: &[char], mut pos: usize) -> Result<(JsonValue, usize), String> {
    pos += 1; // skip '{'
    let mut pairs = Vec::new();
    pos = skip_ws(chars, pos);
    if pos < chars.len() && chars[pos] == '}' {
        return Ok((JsonValue::Object(pairs), pos + 1));
    }
    loop {
        pos = skip_ws(chars, pos);
        let (key, next) = parse_json_string_raw(chars, pos)?;
        pos = skip_ws(chars, next);
        if pos >= chars.len() || chars[pos] != ':' {
            return Err("expected ':' in object".into());
        }
        pos += 1;
        let (val, next) = parse_json_value(chars, pos)?;
        pairs.push((key, val));
        pos = skip_ws(chars, next);
        if pos >= chars.len() {
            return Err("unterminated object".into());
        }
        if chars[pos] == '}' { return Ok((JsonValue::Object(pairs), pos + 1)); }
        if chars[pos] != ',' { return Err(format!("expected ',' or '}}' in object at {}", pos)); }
        pos += 1;
    }
}

fn parse_json_array(chars: &[char], mut pos: usize) -> Result<(JsonValue, usize), String> {
    pos += 1; // skip '['
    let mut items = Vec::new();
    pos = skip_ws(chars, pos);
    if pos < chars.len() && chars[pos] == ']' {
        return Ok((JsonValue::Array(items), pos + 1));
    }
    loop {
        let (val, next) = parse_json_value(chars, pos)?;
        items.push(val);
        pos = skip_ws(chars, next);
        if pos >= chars.len() {
            return Err("unterminated array".into());
        }
        if chars[pos] == ']' { return Ok((JsonValue::Array(items), pos + 1)); }
        if chars[pos] != ',' { return Err(format!("expected ',' or ']' in array at {}", pos)); }
        pos += 1;
    }
}

// ── LSP Constants ──────────────────────────────────────────────

/// All 53 keywords for completion.
const KEYWORDS: &[&str] = &[
    "if", "else", "match", "for", "while", "loop",
    "type", "struct", "enum", "trait", "impl",
    "fn", "return", "yield", "async", "await",
    "let", "mut", "const", "static",
    "mod", "use", "pub", "crate",
    "own", "borrow", "move", "drop",
    "spawn", "channel", "select", "atomic",
    "effect", "handle", "resume", "pure",
    "proof", "assert", "invariant", "theorem",
    "macro", "derive", "where", "comptime",
    "try", "catch", "throw", "panic", "recover",
    "intent", "generate", "verify", "optimize",
];

/// Builtin function names for completion.
const BUILTINS: &[&str] = &[
    "print", "println", "len", "type_of",
    "sigma", "phi", "tau", "gcd", "sopfr",
    "read_file", "write_file", "file_exists",
    "keys", "values", "has_key",
    "to_string", "to_int", "to_float",
    "abs", "min", "max", "floor", "ceil", "round", "sqrt", "pow",
    "log", "log2", "sin", "cos", "tan",
    "format", "args", "env_var", "exit", "clock",
    "Some", "Ok", "Err",
    "channel", "random", "random_int", "sleep", "print_err",
    "json_parse", "json_stringify",
    "psi_coupling", "psi_balance", "psi_steps", "psi_entropy", "psi_frustration",
    "consciousness_max_cells", "consciousness_decoder_dim", "consciousness_phi",
    "hexad_modules", "hexad_right", "hexad_left",
];

// ── Diagnostic generation ──────────────────────────────────────

/// A single diagnostic entry (LSP-compatible).
#[derive(Debug, Clone)]
pub struct LspDiagnostic {
    pub line: usize,   // 0-based
    pub col: usize,    // 0-based
    pub end_col: usize,
    pub severity: u8,  // 1=Error, 2=Warning, 3=Info, 4=Hint
    pub message: String,
}

/// Run lexer + parser + type_checker on source, returning diagnostics.
pub fn diagnose(source: &str) -> Vec<LspDiagnostic> {
    let mut diags = Vec::new();

    // Lex
    let tokens = match Lexer::new(source).tokenize() {
        Ok(t) => t,
        Err(e) => {
            diags.push(parse_error_to_diag(&e));
            return diags;
        }
    };

    // Parse
    let result = match Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(e) => {
            diags.push(hexa_error_to_diag(&e));
            return diags;
        }
    };

    // Type check
    let mut checker = TypeChecker::new();
    if let Err(e) = checker.check(&result.stmts, &result.spans) {
        diags.push(hexa_error_to_diag(&e));
    }

    diags
}

fn parse_error_to_diag(msg: &str) -> LspDiagnostic {
    // Try to extract "line N:M: ..." from error string
    let (line, col, message) = if msg.starts_with("line ") {
        let rest = &msg[5..];
        let mut parts = rest.splitn(2, ':');
        let loc = parts.next().unwrap_or("1:1");
        let msg_text = parts.next().unwrap_or(msg).trim();
        let mut lc = loc.splitn(2, ':');
        let l = lc.next().unwrap_or("1").parse::<usize>().unwrap_or(1);
        let c = lc.next().unwrap_or("1").parse::<usize>().unwrap_or(1);
        (l, c, msg_text.to_string())
    } else {
        (1, 1, msg.to_string())
    };
    LspDiagnostic {
        line: line.saturating_sub(1),
        col: col.saturating_sub(1),
        end_col: col.saturating_sub(1) + 1,
        severity: 1,
        message,
    }
}

fn hexa_error_to_diag(e: &HexaError) -> LspDiagnostic {
    LspDiagnostic {
        line: e.line.saturating_sub(1),
        col: e.col.saturating_sub(1),
        end_col: e.col.saturating_sub(1) + 1,
        severity: 1,
        message: e.message.clone(),
    }
}

// ── Message framing ────────────────────────────────────────────

/// Read one LSP message from stdin.
/// Format: `Content-Length: N\r\n\r\n{JSON body of N bytes}`
pub fn read_message<R: BufRead>(reader: &mut R) -> Result<String, String> {
    let mut content_length: usize = 0;
    loop {
        let mut header = String::new();
        let n = reader.read_line(&mut header).map_err(|e| e.to_string())?;
        if n == 0 {
            return Err("EOF".into());
        }
        let trimmed = header.trim();
        if trimmed.is_empty() {
            break;
        }
        if let Some(rest) = trimmed.strip_prefix("Content-Length:") {
            content_length = rest.trim().parse::<usize>().map_err(|e| e.to_string())?;
        }
        // Skip other headers (Content-Type, etc.)
    }
    if content_length == 0 {
        return Err("missing Content-Length".into());
    }
    let mut body = vec![0u8; content_length];
    reader.read_exact(&mut body).map_err(|e| e.to_string())?;
    String::from_utf8(body).map_err(|e| e.to_string())
}

/// Write one LSP message to stdout.
pub fn send_message<W: Write>(writer: &mut W, body: &str) -> Result<(), String> {
    let msg = format!("Content-Length: {}\r\n\r\n{}", body.len(), body);
    writer.write_all(msg.as_bytes()).map_err(|e| e.to_string())?;
    writer.flush().map_err(|e| e.to_string())?;
    Ok(())
}

// ── Response builders ──────────────────────────────────────────

fn make_response(id: &JsonValue, result: JsonValue) -> JsonValue {
    JsonValue::Object(vec![
        ("jsonrpc".into(), JsonValue::Str("2.0".into())),
        ("id".into(), id.clone()),
        ("result".into(), result),
    ])
}

fn make_notification(method: &str, params: JsonValue) -> JsonValue {
    JsonValue::Object(vec![
        ("jsonrpc".into(), JsonValue::Str("2.0".into())),
        ("method".into(), JsonValue::Str(method.into())),
        ("params".into(), params),
    ])
}

fn diagnostics_to_json(uri: &str, diags: &[LspDiagnostic]) -> JsonValue {
    let diag_array: Vec<JsonValue> = diags.iter().map(|d| {
        JsonValue::Object(vec![
            ("range".into(), JsonValue::Object(vec![
                ("start".into(), JsonValue::Object(vec![
                    ("line".into(), JsonValue::Number(d.line as f64)),
                    ("character".into(), JsonValue::Number(d.col as f64)),
                ])),
                ("end".into(), JsonValue::Object(vec![
                    ("line".into(), JsonValue::Number(d.line as f64)),
                    ("character".into(), JsonValue::Number(d.end_col as f64)),
                ])),
            ])),
            ("severity".into(), JsonValue::Number(d.severity as f64)),
            ("source".into(), JsonValue::Str("hexa".into())),
            ("message".into(), JsonValue::Str(d.message.clone())),
        ])
    }).collect();

    make_notification("textDocument/publishDiagnostics", JsonValue::Object(vec![
        ("uri".into(), JsonValue::Str(uri.into())),
        ("diagnostics".into(), JsonValue::Array(diag_array)),
    ]))
}

fn completion_items() -> JsonValue {
    let mut items: Vec<JsonValue> = Vec::new();

    // Keywords (kind=14 = Keyword)
    for kw in KEYWORDS {
        items.push(JsonValue::Object(vec![
            ("label".into(), JsonValue::Str(kw.to_string())),
            ("kind".into(), JsonValue::Number(14.0)),
            ("detail".into(), JsonValue::Str("keyword".into())),
        ]));
    }

    // Builtins (kind=3 = Function)
    for b in BUILTINS {
        items.push(JsonValue::Object(vec![
            ("label".into(), JsonValue::Str(b.to_string())),
            ("kind".into(), JsonValue::Number(3.0)),
            ("detail".into(), JsonValue::Str("builtin".into())),
        ]));
    }

    JsonValue::Array(items)
}

fn initialize_result() -> JsonValue {
    JsonValue::Object(vec![
        ("capabilities".into(), JsonValue::Object(vec![
            ("textDocumentSync".into(), JsonValue::Number(1.0)), // Full sync
            ("completionProvider".into(), JsonValue::Object(vec![
                ("triggerCharacters".into(), JsonValue::Array(vec![
                    JsonValue::Str(".".into()),
                ])),
            ])),
        ])),
        ("serverInfo".into(), JsonValue::Object(vec![
            ("name".into(), JsonValue::Str("hexa-lsp".into())),
            ("version".into(), JsonValue::Str("1.0.0".into())),
        ])),
    ])
}

// ── Main server loop ───────────────────────────────────────────

/// Run the LSP server, reading from stdin and writing to stdout.
pub fn run_lsp() {
    let stdin = io::stdin();
    let stdout = io::stdout();
    let mut reader = io::BufReader::new(stdin.lock());
    let mut writer = stdout.lock();

    // Track open documents
    let mut docs: HashMap<String, String> = HashMap::new();

    loop {
        let body = match read_message(&mut reader) {
            Ok(b) => b,
            Err(_) => break,
        };

        let msg = match parse_json(&body) {
            Ok(m) => m,
            Err(_) => continue,
        };

        let method = msg.get("method").and_then(|m| m.as_str()).unwrap_or("");
        let id = msg.get("id").cloned();
        let params = msg.get("params").cloned().unwrap_or(JsonValue::Null);

        match method {
            "initialize" => {
                if let Some(ref id) = id {
                    let resp = make_response(id, initialize_result());
                    let _ = send_message(&mut writer, &resp.to_json());
                }
            }
            "initialized" => {
                // Client acknowledged initialization; nothing to do.
            }
            "shutdown" => {
                if let Some(ref id) = id {
                    let resp = make_response(id, JsonValue::Null);
                    let _ = send_message(&mut writer, &resp.to_json());
                }
            }
            "exit" => {
                break;
            }
            "textDocument/didOpen" => {
                if let Some(td) = params.get("textDocument") {
                    let uri = td.get("uri").and_then(|u| u.as_str()).unwrap_or("").to_string();
                    let text = td.get("text").and_then(|t| t.as_str()).unwrap_or("").to_string();
                    let diags = diagnose(&text);
                    docs.insert(uri.clone(), text);
                    let notif = diagnostics_to_json(&uri, &diags);
                    let _ = send_message(&mut writer, &notif.to_json());
                }
            }
            "textDocument/didChange" => {
                if let Some(td) = params.get("textDocument") {
                    let uri = td.get("uri").and_then(|u| u.as_str()).unwrap_or("").to_string();
                    // Full sync: take last content change
                    if let Some(JsonValue::Array(changes)) = params.get("contentChanges") {
                        if let Some(last) = changes.last() {
                            let text = last.get("text").and_then(|t| t.as_str()).unwrap_or("").to_string();
                            let diags = diagnose(&text);
                            docs.insert(uri.clone(), text);
                            let notif = diagnostics_to_json(&uri, &diags);
                            let _ = send_message(&mut writer, &notif.to_json());
                        }
                    }
                }
            }
            "textDocument/completion" => {
                if let Some(ref id) = id {
                    let resp = make_response(id, completion_items());
                    let _ = send_message(&mut writer, &resp.to_json());
                }
            }
            _ => {
                // Unknown method: if it has an id, respond with method not found
                if let Some(ref id) = id {
                    let err_resp = JsonValue::Object(vec![
                        ("jsonrpc".into(), JsonValue::Str("2.0".into())),
                        ("id".into(), id.clone()),
                        ("error".into(), JsonValue::Object(vec![
                            ("code".into(), JsonValue::Number(-32601.0)),
                            ("message".into(), JsonValue::Str(format!("method not found: {}", method))),
                        ])),
                    ]);
                    let _ = send_message(&mut writer, &err_resp.to_json());
                }
            }
        }
    }
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_read_message() {
        let body_str = r#"{"jsonrpc":"2.0","id":1}"#;
        let input = format!("Content-Length: {}\r\n\r\n{}", body_str.len(), body_str);
        let mut reader = io::BufReader::new(input.as_bytes());
        let body = read_message(&mut reader).unwrap();
        assert_eq!(body, body_str);
    }

    #[test]
    fn test_send_message() {
        let mut buf: Vec<u8> = Vec::new();
        send_message(&mut buf, "{\"ok\":true}").unwrap();
        let output = String::from_utf8(buf).unwrap();
        assert!(output.starts_with("Content-Length: 11\r\n\r\n"));
        assert!(output.ends_with("{\"ok\":true}"));
    }

    #[test]
    fn test_diagnose_valid_source() {
        let diags = diagnose("let x = 42");
        assert!(diags.is_empty(), "valid source should produce no diagnostics");
    }

    #[test]
    fn test_diagnose_type_error() {
        let diags = diagnose("let x: int = \"hello\"");
        assert!(!diags.is_empty(), "type mismatch should produce a diagnostic");
        assert_eq!(diags[0].severity, 1); // Error
        assert!(diags[0].message.contains("type mismatch"));
    }

    #[test]
    fn test_diagnose_parse_error() {
        let diags = diagnose("let = ");
        assert!(!diags.is_empty(), "parse error should produce a diagnostic");
    }

    #[test]
    fn test_completion_items_count() {
        let items = completion_items();
        if let JsonValue::Array(arr) = items {
            // 53 keywords + all builtins
            assert!(arr.len() >= 53, "should have at least 53 keyword completions");
            assert_eq!(arr.len(), KEYWORDS.len() + BUILTINS.len());
        } else {
            panic!("completion_items should return an array");
        }
    }

    #[test]
    fn test_json_parse_roundtrip() {
        let input = r#"{"method":"initialize","id":1,"params":{"capabilities":{}}}"#;
        let val = parse_json(input).unwrap();
        assert_eq!(val.get("method").unwrap().as_str().unwrap(), "initialize");
        assert_eq!(val.get("id").unwrap().as_i64().unwrap(), 1);
    }

    #[test]
    fn test_initialize_response() {
        let result = initialize_result();
        let caps = result.get("capabilities").unwrap();
        // Should have textDocumentSync and completionProvider
        assert!(caps.get("textDocumentSync").is_some());
        assert!(caps.get("completionProvider").is_some());
    }

    #[test]
    fn test_diagnostics_json_format() {
        let diags = vec![LspDiagnostic {
            line: 0,
            col: 5,
            end_col: 10,
            severity: 1,
            message: "test error".into(),
        }];
        let notif = diagnostics_to_json("file:///test.hexa", &diags);
        let json_str = notif.to_json();
        assert!(json_str.contains("publishDiagnostics"));
        assert!(json_str.contains("test error"));
        assert!(json_str.contains("file:///test.hexa"));
    }
}

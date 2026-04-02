#![allow(dead_code)]

//! LSP server for HEXA-LANG.
//!
//! Speaks JSON-RPC over stdin/stdout with `Content-Length` framing.
//! Supports:
//!   - initialize / shutdown / exit
//!   - textDocument/didOpen, textDocument/didChange  (diagnostics)
//!   - textDocument/completion                        (keywords + builtins)
//!   - textDocument/definition                        (go-to-definition)
//!   - textDocument/hover                             (type/signature info)
//!   - textDocument/rename                            (rename symbol)

use std::io::{self, BufRead, Write};
use std::collections::HashMap;

use crate::ast::*;
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

// ── Keyword/builtin documentation ─────────────────────────────

fn keyword_doc(kw: &str) -> Option<&'static str> {
    Some(match kw {
        "if" => "Conditional branch: `if cond { ... } else { ... }`",
        "else" => "Alternative branch of an `if` expression",
        "match" => "Pattern matching: `match expr { pat => body, ... }`",
        "for" => "Iterator loop: `for x in collection { ... }`",
        "while" => "Conditional loop: `while cond { ... }`",
        "loop" => "Infinite loop: `loop { ... }` (break to exit)",
        "type" => "Type alias declaration",
        "struct" => "Struct declaration: `struct Name { field: Type, ... }`",
        "enum" => "Enum declaration: `enum Name { Variant, Variant(Type), ... }`",
        "trait" => "Trait declaration: `trait Name { fn method(...) -> Ret }`",
        "impl" => "Implementation block: `impl Trait for Type { ... }`",
        "fn" => "Function declaration: `fn name(params) -> Ret { ... }`",
        "return" => "Return a value from the current function",
        "yield" => "Yield a value from a generator",
        "async" => "Mark a function as asynchronous",
        "await" => "Await a future value: `await expr`",
        "let" => "Variable binding: `let name: Type = value`",
        "mut" => "Mark a binding as mutable",
        "const" => "Compile-time constant: `const NAME: Type = value`",
        "static" => "Module-level static variable: `static NAME: Type = value`",
        "mod" => "Module declaration",
        "use" => "Import items from a module: `use path::to::item`",
        "pub" => "Mark an item as publicly visible",
        "crate" => "Crate-level visibility",
        "own" => "Create an owned value: `own expr`",
        "borrow" => "Create a read-only reference: `borrow expr`",
        "move" => "Transfer ownership: `move expr`",
        "drop" => "Explicit deallocation: `drop x`",
        "spawn" => "Spawn concurrent execution: `spawn { ... }`",
        "channel" => "Create a communication channel",
        "select" => "Select from multiple channels: `select { ... }`",
        "atomic" => "Atomic operation",
        "effect" => "Declare an algebraic effect: `effect Name { ... }`",
        "handle" => "Handle algebraic effects: `handle { body } with { ... }`",
        "resume" => "Resume from an effect handler: `resume(val)`",
        "pure" => "Mark a function as pure (no side effects)",
        "proof" => "Formal proof block: `proof name { ... }`",
        "assert" => "Runtime assertion: `assert expr`",
        "invariant" => "Declare an invariant that must hold",
        "theorem" => "Declare a theorem to be proved",
        "macro" => "Macro definition: `macro! name { pattern => body }`",
        "derive" => "Auto-derive trait implementations",
        "where" => "Additional type constraints: `where T: Trait`",
        "comptime" => "Compile-time evaluation: `comptime { ... }`",
        "try" => "Try block: `try { ... } catch e { ... }`",
        "catch" => "Catch exception from a try block",
        "throw" => "Throw an exception: `throw expr`",
        "panic" => "Unrecoverable error",
        "recover" => "Recover from a panic",
        "intent" => "Declare intent for AI code generation",
        "generate" => "Generate code with AI assistance",
        "verify" => "Verification block: `verify name { ... }`",
        "optimize" => "Optimize function with AI rewrite",
        _ => return None,
    })
}

fn builtin_doc(name: &str) -> Option<&'static str> {
    Some(match name {
        "print" => "fn print(value: any) — Print value without newline",
        "println" => "fn println(value: any) — Print value with newline",
        "len" => "fn len(collection: any) -> int — Get collection length",
        "type_of" => "fn type_of(value: any) -> string — Get runtime type name",
        "sigma" => "fn sigma(n: int) -> int — Sum of divisors",
        "phi" => "fn phi(n: int) -> int — Euler's totient function",
        "tau" => "fn tau(n: int) -> int — Number of divisors",
        "gcd" => "fn gcd(a: int, b: int) -> int — Greatest common divisor",
        "sopfr" => "fn sopfr(n: int) -> int — Sum of prime factors with repetition",
        "read_file" => "fn read_file(path: string) -> string — Read file contents",
        "write_file" => "fn write_file(path: string, content: string) — Write file",
        "file_exists" => "fn file_exists(path: string) -> bool — Check if file exists",
        "keys" => "fn keys(map: Map) -> [string] — Get map keys",
        "values" => "fn values(map: Map) -> [any] — Get map values",
        "has_key" => "fn has_key(map: Map, key: string) -> bool — Check key existence",
        "to_string" => "fn to_string(value: any) -> string — Convert to string",
        "to_int" => "fn to_int(value: any) -> int — Convert to integer",
        "to_float" => "fn to_float(value: any) -> float — Convert to float",
        "abs" => "fn abs(x: number) -> number — Absolute value",
        "min" => "fn min(a: number, b: number) -> number — Minimum",
        "max" => "fn max(a: number, b: number) -> number — Maximum",
        "floor" => "fn floor(x: float) -> int — Floor",
        "ceil" => "fn ceil(x: float) -> int — Ceiling",
        "round" => "fn round(x: float) -> int — Round to nearest",
        "sqrt" => "fn sqrt(x: float) -> float — Square root",
        "pow" => "fn pow(base: float, exp: float) -> float — Power",
        "log" => "fn log(x: float) -> float — Natural logarithm",
        "log2" => "fn log2(x: float) -> float — Base-2 logarithm",
        "sin" => "fn sin(x: float) -> float — Sine",
        "cos" => "fn cos(x: float) -> float — Cosine",
        "tan" => "fn tan(x: float) -> float — Tangent",
        "format" => "fn format(fmt: string, ...) -> string — Format string",
        "args" => "fn args() -> [string] — Command-line arguments",
        "env_var" => "fn env_var(name: string) -> string — Environment variable",
        "exit" => "fn exit(code: int) — Exit process",
        "clock" => "fn clock() -> float — Current time in seconds",
        "Some" => "Some(value) — Wrap value in Option",
        "Ok" => "Ok(value) — Wrap value in Result (success)",
        "Err" => "Err(value) — Wrap value in Result (error)",
        "random" => "fn random() -> float — Random float in [0, 1)",
        "random_int" => "fn random_int(min: int, max: int) -> int — Random integer",
        "sleep" => "fn sleep(ms: int) — Sleep for milliseconds",
        "print_err" => "fn print_err(value: any) — Print to stderr",
        "json_parse" => "fn json_parse(s: string) -> any — Parse JSON string",
        "json_stringify" => "fn json_stringify(value: any) -> string — Serialize to JSON",
        _ => return None,
    })
}

// ── Symbol table ──────────────────────────────────────────────

/// Kind of symbol for the symbol table.
#[derive(Debug, Clone, PartialEq)]
pub enum SymbolKind {
    Variable,
    Function,
    Struct,
    Enum,
    Trait,
    Field,
    Param,
    EnumVariant,
}

/// A symbol entry in the symbol table.
#[derive(Debug, Clone)]
pub struct Symbol {
    pub name: String,
    pub kind: SymbolKind,
    pub line: usize,   // 1-based
    pub col: usize,    // 1-based
    pub detail: String, // type info, signature, etc.
}

/// A reference to a symbol (usage site).
#[derive(Debug, Clone)]
pub struct SymbolRef {
    pub name: String,
    pub line: usize,  // 1-based
    pub col: usize,   // 1-based
    pub len: usize,   // length of the name
}

/// Build a symbol table from source code.
/// Returns (definitions, references) where definitions are declaration sites
/// and references are all usage sites (including at declaration).
pub fn build_symbol_table(source: &str) -> (Vec<Symbol>, Vec<SymbolRef>) {
    let tokens = match Lexer::new(source).tokenize() {
        Ok(t) => t,
        Err(_) => return (Vec::new(), Vec::new()),
    };
    let result = match Parser::new(tokens).parse_with_spans() {
        Ok(r) => r,
        Err(_) => return (Vec::new(), Vec::new()),
    };

    let mut symbols = Vec::new();
    let mut refs = Vec::new();

    for (stmt, &(line, col)) in result.stmts.iter().zip(result.spans.iter()) {
        collect_symbols_from_stmt(stmt, line, col, &mut symbols, &mut refs);
    }

    // Also collect identifier references by scanning tokens from source
    collect_refs_from_source(source, &mut refs);

    (symbols, refs)
}

fn collect_symbols_from_stmt(
    stmt: &Stmt,
    line: usize,
    col: usize,
    symbols: &mut Vec<Symbol>,
    refs: &mut Vec<SymbolRef>,
) {
    match stmt {
        Stmt::Let(name, typ, _expr, _vis) => {
            let detail = match typ {
                Some(t) => format!("let {}: {}", name, t),
                None => format!("let {}", name),
            };
            symbols.push(Symbol {
                name: name.clone(),
                kind: SymbolKind::Variable,
                line, col,
                detail,
            });
        }
        Stmt::Const(name, typ, _expr, _vis) => {
            let detail = match typ {
                Some(t) => format!("const {}: {}", name, t),
                None => format!("const {}", name),
            };
            symbols.push(Symbol {
                name: name.clone(),
                kind: SymbolKind::Variable,
                line, col,
                detail,
            });
        }
        Stmt::Static(name, typ, _expr, _vis) => {
            let detail = match typ {
                Some(t) => format!("static {}: {}", name, t),
                None => format!("static {}", name),
            };
            symbols.push(Symbol {
                name: name.clone(),
                kind: SymbolKind::Variable,
                line, col,
                detail,
            });
        }
        Stmt::FnDecl(f) | Stmt::AsyncFnDecl(f) | Stmt::ComptimeFn(f) | Stmt::Optimize(f) => {
            let params_str: Vec<String> = f.params.iter().map(|p| {
                match &p.typ {
                    Some(t) => format!("{}: {}", p.name, t),
                    None => p.name.clone(),
                }
            }).collect();
            let ret = match &f.ret_type {
                Some(r) => format!(" -> {}", r),
                None => String::new(),
            };
            let detail = format!("fn {}({}){}", f.name, params_str.join(", "), ret);
            symbols.push(Symbol {
                name: f.name.clone(),
                kind: SymbolKind::Function,
                line, col,
                detail,
            });
            // Collect params as symbols
            for p in &f.params {
                symbols.push(Symbol {
                    name: p.name.clone(),
                    kind: SymbolKind::Param,
                    line, col,
                    detail: match &p.typ {
                        Some(t) => format!("{}: {}", p.name, t),
                        None => p.name.clone(),
                    },
                });
            }
            // Walk body
            for s in &f.body {
                collect_symbols_from_stmt(s, line, col, symbols, refs);
            }
        }
        Stmt::StructDecl(sd) => {
            let fields_str: Vec<String> = sd.fields.iter().map(|(name, typ, _)| {
                format!("{}: {}", name, typ)
            }).collect();
            let detail = format!("struct {} {{ {} }}", sd.name, fields_str.join(", "));
            symbols.push(Symbol {
                name: sd.name.clone(),
                kind: SymbolKind::Struct,
                line, col,
                detail,
            });
            // Fields as symbols
            for (fname, ftype, _) in &sd.fields {
                symbols.push(Symbol {
                    name: fname.clone(),
                    kind: SymbolKind::Field,
                    line, col,
                    detail: format!("{}: {} (in {})", fname, ftype, sd.name),
                });
            }
        }
        Stmt::EnumDecl(ed) => {
            let variants_str: Vec<String> = ed.variants.iter().map(|(name, fields)| {
                match fields {
                    Some(f) => format!("{}({})", name, f.join(", ")),
                    None => name.clone(),
                }
            }).collect();
            let detail = format!("enum {} {{ {} }}", ed.name, variants_str.join(", "));
            symbols.push(Symbol {
                name: ed.name.clone(),
                kind: SymbolKind::Enum,
                line, col,
                detail,
            });
            for (vname, vfields) in &ed.variants {
                let vdetail = match vfields {
                    Some(f) => format!("{}::{}({})", ed.name, vname, f.join(", ")),
                    None => format!("{}::{}", ed.name, vname),
                };
                symbols.push(Symbol {
                    name: vname.clone(),
                    kind: SymbolKind::EnumVariant,
                    line, col,
                    detail: vdetail,
                });
            }
        }
        Stmt::TraitDecl(td) => {
            let methods: Vec<String> = td.methods.iter().map(|m| {
                let params_str: Vec<String> = m.params.iter().map(|p| {
                    match &p.typ {
                        Some(t) => format!("{}: {}", p.name, t),
                        None => p.name.clone(),
                    }
                }).collect();
                let ret = match &m.ret_type {
                    Some(r) => format!(" -> {}", r),
                    None => String::new(),
                };
                format!("fn {}({}){}", m.name, params_str.join(", "), ret)
            }).collect();
            let detail = format!("trait {} {{ {} }}", td.name, methods.join("; "));
            symbols.push(Symbol {
                name: td.name.clone(),
                kind: SymbolKind::Trait,
                line, col,
                detail,
            });
        }
        Stmt::ImplBlock(ib) => {
            for m in &ib.methods {
                let params_str: Vec<String> = m.params.iter().map(|p| {
                    match &p.typ {
                        Some(t) => format!("{}: {}", p.name, t),
                        None => p.name.clone(),
                    }
                }).collect();
                let ret = match &m.ret_type {
                    Some(r) => format!(" -> {}", r),
                    None => String::new(),
                };
                let detail = format!("fn {}({}){} (impl {})", m.name, params_str.join(", "), ret, ib.target);
                symbols.push(Symbol {
                    name: m.name.clone(),
                    kind: SymbolKind::Function,
                    line, col,
                    detail,
                });
                for s in &m.body {
                    collect_symbols_from_stmt(s, line, col, symbols, refs);
                }
            }
        }
        Stmt::For(var, _expr, body) => {
            symbols.push(Symbol {
                name: var.clone(),
                kind: SymbolKind::Variable,
                line, col,
                detail: format!("for {}", var),
            });
            for s in body {
                collect_symbols_from_stmt(s, line, col, symbols, refs);
            }
        }
        Stmt::While(_, body) | Stmt::Loop(body) => {
            for s in body {
                collect_symbols_from_stmt(s, line, col, symbols, refs);
            }
        }
        Stmt::TryCatch(try_block, var, catch_block) => {
            symbols.push(Symbol {
                name: var.clone(),
                kind: SymbolKind::Variable,
                line, col,
                detail: format!("catch {}", var),
            });
            for s in try_block {
                collect_symbols_from_stmt(s, line, col, symbols, refs);
            }
            for s in catch_block {
                collect_symbols_from_stmt(s, line, col, symbols, refs);
            }
        }
        Stmt::Mod(_, stmts) => {
            for s in stmts {
                collect_symbols_from_stmt(s, line, col, symbols, refs);
            }
        }
        Stmt::EffectDecl(ed) => {
            symbols.push(Symbol {
                name: ed.name.clone(),
                kind: SymbolKind::Trait,
                line, col,
                detail: format!("effect {}", ed.name),
            });
        }
        _ => {}
    }
}

/// Scan source text to find all identifier tokens with their positions.
/// This gives us reference locations that are more accurate than AST spans
/// (since the AST only has statement-level spans).
fn collect_refs_from_source(source: &str, refs: &mut Vec<SymbolRef>) {
    for (line_idx, line_text) in source.lines().enumerate() {
        let line_num = line_idx + 1; // 1-based
        let chars: Vec<char> = line_text.chars().collect();
        let mut i = 0;
        while i < chars.len() {
            if chars[i].is_alphabetic() || chars[i] == '_' {
                let start = i;
                while i < chars.len() && (chars[i].is_alphanumeric() || chars[i] == '_') {
                    i += 1;
                }
                let word: String = chars[start..i].iter().collect();
                // Skip keywords
                if !KEYWORDS.contains(&word.as_str()) {
                    refs.push(SymbolRef {
                        name: word.clone(),
                        line: line_num,
                        col: start + 1, // 1-based
                        len: word.len(),
                    });
                }
            } else {
                i += 1;
            }
        }
    }
}

/// Get the word at a given 0-based line and 0-based character position.
fn word_at_position(source: &str, line: usize, character: usize) -> Option<String> {
    let line_text = source.lines().nth(line)?;
    let chars: Vec<char> = line_text.chars().collect();
    if character >= chars.len() {
        return None;
    }
    // Check the character at cursor is part of an identifier
    if !chars[character].is_alphanumeric() && chars[character] != '_' {
        return None;
    }
    // Find word boundaries
    let mut start = character;
    while start > 0 && (chars[start - 1].is_alphanumeric() || chars[start - 1] == '_') {
        start -= 1;
    }
    let mut end = character;
    while end < chars.len() && (chars[end].is_alphanumeric() || chars[end] == '_') {
        end += 1;
    }
    Some(chars[start..end].iter().collect())
}

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

// ── Go-to-definition ──────────────────────────────────────────

/// Handle textDocument/definition request.
/// Returns a Location JSON or null if not found.
fn handle_definition(uri: &str, source: &str, line: usize, character: usize) -> JsonValue {
    let word = match word_at_position(source, line, character) {
        Some(w) => w,
        None => return JsonValue::Null,
    };

    let (symbols, _refs) = build_symbol_table(source);

    // Find the definition — prefer non-Param/Field kinds first for exact matches
    let def = symbols.iter()
        .filter(|s| s.name == word)
        .min_by_key(|s| match s.kind {
            SymbolKind::Function => 0,
            SymbolKind::Struct => 0,
            SymbolKind::Enum => 0,
            SymbolKind::Trait => 0,
            SymbolKind::Variable => 1,
            SymbolKind::Param => 2,
            SymbolKind::Field => 3,
            SymbolKind::EnumVariant => 4,
        });

    match def {
        Some(sym) => {
            let target_line = sym.line.saturating_sub(1); // to 0-based
            let target_col = sym.col.saturating_sub(1);
            make_location(uri, target_line, target_col)
        }
        None => JsonValue::Null,
    }
}

fn make_location(uri: &str, line: usize, col: usize) -> JsonValue {
    JsonValue::Object(vec![
        ("uri".into(), JsonValue::Str(uri.into())),
        ("range".into(), JsonValue::Object(vec![
            ("start".into(), JsonValue::Object(vec![
                ("line".into(), JsonValue::Number(line as f64)),
                ("character".into(), JsonValue::Number(col as f64)),
            ])),
            ("end".into(), JsonValue::Object(vec![
                ("line".into(), JsonValue::Number(line as f64)),
                ("character".into(), JsonValue::Number(col as f64)),
            ])),
        ])),
    ])
}

// ── Hover ─────────────────────────────────────────────────────

/// Handle textDocument/hover request.
/// Returns a Hover JSON or null.
fn handle_hover(source: &str, line: usize, character: usize) -> JsonValue {
    let word = match word_at_position(source, line, character) {
        Some(w) => w,
        None => return JsonValue::Null,
    };

    // Check keywords first
    if let Some(doc) = keyword_doc(&word) {
        return make_hover(&format!("**keyword** `{}`\n\n{}", word, doc));
    }

    // Check builtins
    if let Some(doc) = builtin_doc(&word) {
        return make_hover(&format!("**builtin**\n\n```\n{}\n```", doc));
    }

    // Check symbol table
    let (symbols, _refs) = build_symbol_table(source);
    let sym = symbols.iter()
        .filter(|s| s.name == word)
        .min_by_key(|s| match s.kind {
            SymbolKind::Function => 0,
            SymbolKind::Struct => 0,
            SymbolKind::Enum => 0,
            SymbolKind::Trait => 0,
            SymbolKind::Variable => 1,
            SymbolKind::Param => 2,
            SymbolKind::Field => 3,
            SymbolKind::EnumVariant => 4,
        });

    match sym {
        Some(s) => {
            let kind_str = match s.kind {
                SymbolKind::Variable => "variable",
                SymbolKind::Function => "function",
                SymbolKind::Struct => "struct",
                SymbolKind::Enum => "enum",
                SymbolKind::Trait => "trait",
                SymbolKind::Field => "field",
                SymbolKind::Param => "parameter",
                SymbolKind::EnumVariant => "variant",
            };
            make_hover(&format!("**{}** `{}`\n\n```hexa\n{}\n```", kind_str, s.name, s.detail))
        }
        None => JsonValue::Null,
    }
}

fn make_hover(contents: &str) -> JsonValue {
    JsonValue::Object(vec![
        ("contents".into(), JsonValue::Object(vec![
            ("kind".into(), JsonValue::Str("markdown".into())),
            ("value".into(), JsonValue::Str(contents.into())),
        ])),
    ])
}

// ── Rename ────────────────────────────────────────────────────

/// Handle textDocument/rename request.
/// Returns a WorkspaceEdit with TextEdits for all occurrences.
fn handle_rename(uri: &str, source: &str, line: usize, character: usize, new_name: &str) -> JsonValue {
    let word = match word_at_position(source, line, character) {
        Some(w) => w,
        None => return JsonValue::Null,
    };

    // Reject renaming keywords/builtins
    if KEYWORDS.contains(&word.as_str()) || BUILTINS.contains(&word.as_str()) {
        return JsonValue::Null;
    }

    // Check symbol exists
    let (symbols, refs) = build_symbol_table(source);
    let has_def = symbols.iter().any(|s| s.name == word);
    if !has_def {
        return JsonValue::Null;
    }

    // Collect all reference locations for this name
    let edits: Vec<JsonValue> = refs.iter()
        .filter(|r| r.name == word)
        .map(|r| {
            let start_line = r.line.saturating_sub(1);
            let start_col = r.col.saturating_sub(1);
            let end_col = start_col + r.len;
            JsonValue::Object(vec![
                ("range".into(), JsonValue::Object(vec![
                    ("start".into(), JsonValue::Object(vec![
                        ("line".into(), JsonValue::Number(start_line as f64)),
                        ("character".into(), JsonValue::Number(start_col as f64)),
                    ])),
                    ("end".into(), JsonValue::Object(vec![
                        ("line".into(), JsonValue::Number(start_line as f64)),
                        ("character".into(), JsonValue::Number(end_col as f64)),
                    ])),
                ])),
                ("newText".into(), JsonValue::Str(new_name.into())),
            ])
        })
        .collect();

    if edits.is_empty() {
        return JsonValue::Null;
    }

    // WorkspaceEdit { changes: { uri: [edits] } }
    JsonValue::Object(vec![
        ("changes".into(), JsonValue::Object(vec![
            (uri.into(), JsonValue::Array(edits)),
        ])),
    ])
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
            ("definitionProvider".into(), JsonValue::Bool(true)),
            ("hoverProvider".into(), JsonValue::Bool(true)),
            ("renameProvider".into(), JsonValue::Bool(true)),
        ])),
        ("serverInfo".into(), JsonValue::Object(vec![
            ("name".into(), JsonValue::Str("hexa-lsp".into())),
            ("version".into(), JsonValue::Str("2.0.0".into())),
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
            "textDocument/definition" => {
                if let Some(ref id) = id {
                    let uri = params.get("textDocument")
                        .and_then(|td| td.get("uri"))
                        .and_then(|u| u.as_str())
                        .unwrap_or("");
                    let line = params.get("position")
                        .and_then(|p| p.get("line"))
                        .and_then(|l| l.as_i64())
                        .unwrap_or(0) as usize;
                    let character = params.get("position")
                        .and_then(|p| p.get("character"))
                        .and_then(|c| c.as_i64())
                        .unwrap_or(0) as usize;
                    let source = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                    let result = handle_definition(uri, source, line, character);
                    let resp = make_response(id, result);
                    let _ = send_message(&mut writer, &resp.to_json());
                }
            }
            "textDocument/hover" => {
                if let Some(ref id) = id {
                    let uri = params.get("textDocument")
                        .and_then(|td| td.get("uri"))
                        .and_then(|u| u.as_str())
                        .unwrap_or("");
                    let line = params.get("position")
                        .and_then(|p| p.get("line"))
                        .and_then(|l| l.as_i64())
                        .unwrap_or(0) as usize;
                    let character = params.get("position")
                        .and_then(|p| p.get("character"))
                        .and_then(|c| c.as_i64())
                        .unwrap_or(0) as usize;
                    let source = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                    let result = handle_hover(source, line, character);
                    let resp = make_response(id, result);
                    let _ = send_message(&mut writer, &resp.to_json());
                }
            }
            "textDocument/rename" => {
                if let Some(ref id) = id {
                    let uri = params.get("textDocument")
                        .and_then(|td| td.get("uri"))
                        .and_then(|u| u.as_str())
                        .unwrap_or("");
                    let line = params.get("position")
                        .and_then(|p| p.get("line"))
                        .and_then(|l| l.as_i64())
                        .unwrap_or(0) as usize;
                    let character = params.get("position")
                        .and_then(|p| p.get("character"))
                        .and_then(|c| c.as_i64())
                        .unwrap_or(0) as usize;
                    let new_name = params.get("newName")
                        .and_then(|n| n.as_str())
                        .unwrap_or("");
                    let source = docs.get(uri).map(|s| s.as_str()).unwrap_or("");
                    let result = handle_rename(uri, source, line, character, new_name);
                    let resp = make_response(id, result);
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

    // ── Symbol table tests ────────────────────────────────────

    #[test]
    fn test_build_symbol_table_let() {
        let source = "let x = 42";
        let (symbols, refs) = build_symbol_table(source);
        assert!(symbols.iter().any(|s| s.name == "x" && s.kind == SymbolKind::Variable));
        assert!(refs.iter().any(|r| r.name == "x"));
    }

    #[test]
    fn test_build_symbol_table_fn() {
        let source = "fn add(a: int, b: int) -> int {\n    return a + b\n}";
        let (symbols, _refs) = build_symbol_table(source);
        let func = symbols.iter().find(|s| s.name == "add" && s.kind == SymbolKind::Function);
        assert!(func.is_some(), "should find function 'add'");
        let f = func.unwrap();
        assert!(f.detail.contains("fn add(a: int, b: int) -> int"));
    }

    #[test]
    fn test_build_symbol_table_struct() {
        let source = "struct Point {\n    x: float\n    y: float\n}";
        let (symbols, _refs) = build_symbol_table(source);
        assert!(symbols.iter().any(|s| s.name == "Point" && s.kind == SymbolKind::Struct));
        assert!(symbols.iter().any(|s| s.name == "x" && s.kind == SymbolKind::Field));
        assert!(symbols.iter().any(|s| s.name == "y" && s.kind == SymbolKind::Field));
    }

    #[test]
    fn test_build_symbol_table_enum() {
        let source = "enum Color {\n    Red\n    Green\n    Blue\n}";
        let (symbols, _refs) = build_symbol_table(source);
        assert!(symbols.iter().any(|s| s.name == "Color" && s.kind == SymbolKind::Enum));
        assert!(symbols.iter().any(|s| s.name == "Red" && s.kind == SymbolKind::EnumVariant));
    }

    #[test]
    fn test_build_symbol_table_const_static() {
        let source = "const PI = 3.14\nstatic COUNT = 0";
        let (symbols, _refs) = build_symbol_table(source);
        assert!(symbols.iter().any(|s| s.name == "PI" && s.kind == SymbolKind::Variable));
        assert!(symbols.iter().any(|s| s.name == "COUNT" && s.kind == SymbolKind::Variable));
    }

    // ── word_at_position tests ────────────────────────────────

    #[test]
    fn test_word_at_position() {
        let source = "let foo = 42\nlet bar = foo + 1";
        assert_eq!(word_at_position(source, 0, 4), Some("foo".into()));  // 'f' in 'foo'
        assert_eq!(word_at_position(source, 0, 5), Some("foo".into()));  // 'o' in 'foo'
        assert_eq!(word_at_position(source, 1, 10), Some("foo".into())); // 'f' in second 'foo'
        assert_eq!(word_at_position(source, 0, 3), None);                // space between 'let' and 'foo'
        assert_eq!(word_at_position(source, 0, 8), None);                // '=' not ident
    }

    // ── Go-to-definition tests ────────────────────────────────

    #[test]
    fn test_definition_variable() {
        let source = "let foo = 42\nlet bar = foo + 1";
        let result = handle_definition("file:///test.hexa", source, 1, 10);
        // Should find definition — result is a Location object (not null)
        assert!(!matches!(result, JsonValue::Null), "should find definition for 'foo'");
    }

    #[test]
    fn test_definition_function() {
        let source = "fn greet(name: string) -> string {\n    return name\n}\nlet x = greet(\"hi\")";
        let result = handle_definition("file:///test.hexa", source, 3, 8);
        assert!(!matches!(result, JsonValue::Null), "should find definition for 'greet'");
    }

    #[test]
    fn test_definition_not_found() {
        let source = "let x = 42";
        let result = handle_definition("file:///test.hexa", source, 0, 8);
        // Position 8 is on '4' which is a digit, not an ident
        assert!(matches!(result, JsonValue::Null));
    }

    // ── Hover tests ───────────────────────────────────────────

    #[test]
    fn test_hover_keyword() {
        let source = "let x = 42";
        let result = handle_hover(source, 0, 0); // 'l' in 'let'
        assert!(!matches!(result, JsonValue::Null), "should show hover for 'let'");
        let json = result.to_json();
        assert!(json.contains("keyword"), "hover should mention keyword");
    }

    #[test]
    fn test_hover_builtin() {
        let source = "println(42)";
        let result = handle_hover(source, 0, 0); // 'p' in 'println'
        assert!(!matches!(result, JsonValue::Null), "should show hover for 'println'");
        let json = result.to_json();
        assert!(json.contains("builtin"), "hover should mention builtin");
    }

    #[test]
    fn test_hover_variable() {
        let source = "let counter: int = 0\ncounter";
        let result = handle_hover(source, 1, 0); // 'c' in 'counter'
        assert!(!matches!(result, JsonValue::Null), "should show hover for variable");
        let json = result.to_json();
        assert!(json.contains("variable") || json.contains("counter"));
    }

    #[test]
    fn test_hover_function() {
        let source = "fn add(a: int, b: int) -> int {\n    return a + b\n}";
        let result = handle_hover(source, 0, 3); // 'a' in 'add'
        assert!(!matches!(result, JsonValue::Null), "should show hover for function");
        let json = result.to_json();
        assert!(json.contains("fn add"));
    }

    #[test]
    fn test_hover_struct() {
        let source = "struct Point {\n    x: float\n    y: float\n}";
        let result = handle_hover(source, 0, 7); // 'P' in 'Point'
        assert!(!matches!(result, JsonValue::Null), "should show hover for struct");
        let json = result.to_json();
        assert!(json.contains("struct Point"));
    }

    // ── Rename tests ──────────────────────────────────────────

    #[test]
    fn test_rename_variable() {
        let source = "let foo = 42\nlet bar = foo + 1";
        let result = handle_rename("file:///test.hexa", source, 0, 4, "baz");
        assert!(!matches!(result, JsonValue::Null), "should return workspace edit");
        let json = result.to_json();
        assert!(json.contains("baz"), "edits should contain new name");
        // Should have edits for both occurrences of 'foo'
        assert!(json.contains("changes"));
    }

    #[test]
    fn test_rename_function() {
        let source = "fn greet(name: string) -> string {\n    return name\n}\ngreet(\"hi\")";
        let result = handle_rename("file:///test.hexa", source, 0, 3, "hello");
        assert!(!matches!(result, JsonValue::Null), "should return workspace edit for function rename");
        let json = result.to_json();
        assert!(json.contains("hello"));
    }

    #[test]
    fn test_rename_keyword_rejected() {
        let source = "let x = 42";
        let result = handle_rename("file:///test.hexa", source, 0, 0, "var");
        assert!(matches!(result, JsonValue::Null), "should reject keyword rename");
    }

    #[test]
    fn test_rename_builtin_rejected() {
        let source = "println(42)";
        let result = handle_rename("file:///test.hexa", source, 0, 0, "output");
        assert!(matches!(result, JsonValue::Null), "should reject builtin rename");
    }

    // ── Initialize capabilities test ──────────────────────────

    #[test]
    fn test_initialize_has_new_capabilities() {
        let result = initialize_result();
        let caps = result.get("capabilities").unwrap();
        assert!(caps.get("definitionProvider").is_some(), "should advertise definitionProvider");
        assert!(caps.get("hoverProvider").is_some(), "should advertise hoverProvider");
        assert!(caps.get("renameProvider").is_some(), "should advertise renameProvider");
    }

    // ── Keyword/builtin doc tests ─────────────────────────────

    #[test]
    fn test_keyword_doc_coverage() {
        // All keywords should have docs
        for kw in KEYWORDS {
            assert!(keyword_doc(kw).is_some(), "missing doc for keyword: {}", kw);
        }
    }

    #[test]
    fn test_builtin_doc_coverage() {
        // Most builtins should have docs (excluding psi_* and consciousness_* and hexad_*)
        let special = &[
            "psi_coupling", "psi_balance", "psi_steps", "psi_entropy", "psi_frustration",
            "consciousness_max_cells", "consciousness_decoder_dim", "consciousness_phi",
            "hexad_modules", "hexad_right", "hexad_left", "channel",
        ];
        for b in BUILTINS {
            if !special.contains(b) {
                assert!(builtin_doc(b).is_some(), "missing doc for builtin: {}", b);
            }
        }
    }

    // ── Edge case tests ───────────────────────────────────────

    #[test]
    fn test_symbol_table_empty_source() {
        let (symbols, refs) = build_symbol_table("");
        assert!(symbols.is_empty());
        assert!(refs.is_empty());
    }

    #[test]
    fn test_symbol_table_invalid_source() {
        let (symbols, refs) = build_symbol_table("{{{{invalid!!!!}}}}");
        // Should not panic, just return empty
        let _ = symbols;
        let _ = refs;
    }

    #[test]
    fn test_word_at_position_out_of_bounds() {
        assert_eq!(word_at_position("hello", 5, 0), None); // line out of range
        assert_eq!(word_at_position("hello", 0, 100), None); // col out of range
    }

    #[test]
    fn test_hover_on_empty() {
        let result = handle_hover("", 0, 0);
        assert!(matches!(result, JsonValue::Null));
    }

    #[test]
    fn test_definition_on_keyword() {
        let source = "let x = 42";
        // Position 0 = 'l' in 'let', but 'let' is not in symbol table
        // word_at_position returns "let", but build_symbol_table won't have it
        let result = handle_definition("file:///test.hexa", source, 0, 0);
        // "let" is a keyword, it won't be in symbols, so should be Null
        assert!(matches!(result, JsonValue::Null));
    }

    #[test]
    fn test_rename_multiple_occurrences() {
        let source = "let val = 10\nlet res = val + val";
        let result = handle_rename("file:///test.hexa", source, 1, 10, "num");
        assert!(!matches!(result, JsonValue::Null));
        let json = result.to_json();
        // Should contain multiple edits for 'val'
        // Count occurrences of "num" in the output (at least 3: def + 2 uses)
        let count = json.matches("\"num\"").count();
        assert!(count >= 3, "expected at least 3 edit entries for 'val', got {}", count);
    }
}

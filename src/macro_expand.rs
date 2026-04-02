#![allow(dead_code)]

//! Hygienic macro expansion for Hexa.
//!
//! The expander matches invocation tokens against macro rule patterns,
//! captures fragments, and produces expanded AST statements.
//! Hygiene is implemented via gensym: bindings introduced by macros
//! are suffixed with `__macro_N` to avoid collisions with user code.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};

use crate::ast::*;
use crate::error::{HexaError, ErrorClass};

/// Global counter for hygienic name generation.
static GENSYM_COUNTER: AtomicU64 = AtomicU64::new(0);

fn gensym(base: &str) -> String {
    let id = GENSYM_COUNTER.fetch_add(1, Ordering::Relaxed);
    format!("{}__{}", base, id)
}

/// A captured macro fragment: either a single value or a repeated sequence.
#[derive(Debug, Clone)]
pub enum MacroFragment {
    Single(Vec<MacroLitToken>),
    Repeated(Vec<Vec<MacroLitToken>>),
}

/// Stored macro definition, used by the interpreter.
#[derive(Debug, Clone)]
pub struct StoredMacro {
    pub rules: Vec<MacroRule>,
}

/// Expand a macro invocation given the macro's rules and the invocation tokens.
/// Returns a list of Stmt to be executed (the last expression's value is the result).
pub fn expand_macro(
    mac: &StoredMacro,
    invocation_tokens: &[MacroLitToken],
    line: usize,
    col: usize,
) -> Result<Vec<Stmt>, HexaError> {
    for rule in &mac.rules {
        if let Some(captures) = match_pattern(&rule.pattern, invocation_tokens) {
            let expanded_tokens = substitute_body(&rule.body, &captures)?;
            let stmts = tokens_to_stmts(&expanded_tokens, line, col)?;
            return Ok(stmts);
        }
    }
    Err(HexaError {
        class: ErrorClass::Syntax,
        message: "no macro rule matched the invocation".into(),
        line,
        col,
        hint: None,
    })
}

/// Try to match invocation tokens against a pattern.
/// Returns captured fragments on success, None on failure.
fn match_pattern(
    pattern: &[MacroPatternToken],
    tokens: &[MacroLitToken],
) -> Option<HashMap<String, MacroFragment>> {
    let mut captures = HashMap::new();
    let mut pos = 0;

    for pat_token in pattern {
        match pat_token {
            MacroPatternToken::Capture(name, spec) => {
                if pos >= tokens.len() {
                    return None;
                }
                // Capture a single fragment based on specifier
                let (fragment, consumed) = capture_fragment(spec, tokens, pos)?;
                captures.insert(name.clone(), MacroFragment::Single(fragment));
                pos += consumed;
            }
            MacroPatternToken::Literal(lit) => {
                if pos >= tokens.len() {
                    return None;
                }
                if !lit_tokens_eq(&tokens[pos], lit) {
                    return None;
                }
                pos += 1;
            }
            MacroPatternToken::Repetition(inner_pat, separator, _kind) => {
                // Collect repeated captures
                let mut rep_captures: HashMap<String, Vec<Vec<MacroLitToken>>> = HashMap::new();
                // Initialize capture names from inner pattern
                for ip in inner_pat {
                    if let MacroPatternToken::Capture(name, _) = ip {
                        rep_captures.insert(name.clone(), Vec::new());
                    }
                }

                let mut iteration = 0;
                loop {
                    if pos >= tokens.len() {
                        break;
                    }
                    // Try to match the inner pattern at current position
                    let mut inner_pos = pos;
                    let mut iter_captures: HashMap<String, Vec<MacroLitToken>> = HashMap::new();
                    let mut matched = true;

                    for ip in inner_pat {
                        match ip {
                            MacroPatternToken::Capture(name, spec) => {
                                if inner_pos >= tokens.len() {
                                    matched = false;
                                    break;
                                }
                                if let Some((frag, consumed)) = capture_fragment(spec, tokens, inner_pos) {
                                    iter_captures.insert(name.clone(), frag);
                                    inner_pos += consumed;
                                } else {
                                    matched = false;
                                    break;
                                }
                            }
                            MacroPatternToken::Literal(lit) => {
                                if inner_pos >= tokens.len() || !lit_tokens_eq(&tokens[inner_pos], lit) {
                                    matched = false;
                                    break;
                                }
                                inner_pos += 1;
                            }
                            MacroPatternToken::Repetition(..) => {
                                // Nested repetition not supported
                                matched = false;
                                break;
                            }
                        }
                    }

                    if !matched {
                        break;
                    }

                    // Store captured values
                    for (name, frag) in iter_captures {
                        rep_captures.entry(name).or_default().push(frag);
                    }
                    pos = inner_pos;
                    iteration += 1;

                    // Check for separator
                    if separator.is_some() {
                        if pos < tokens.len() && matches!(&tokens[pos], MacroLitToken::Comma) {
                            pos += 1; // consume separator
                        } else {
                            break;
                        }
                    }
                }

                let _ = iteration; // suppress unused warning
                // Convert to MacroFragment::Repeated
                for (name, frags) in rep_captures {
                    captures.insert(name, MacroFragment::Repeated(frags));
                }
            }
        }
    }

    // All pattern tokens must have been consumed
    // (allow trailing tokens in invocation for flexibility)
    Some(captures)
}

/// Capture a fragment starting at `pos` in `tokens`.
/// Returns (captured tokens, number consumed).
fn capture_fragment(
    spec: &MacroFragSpec,
    tokens: &[MacroLitToken],
    pos: usize,
) -> Option<(Vec<MacroLitToken>, usize)> {
    if pos >= tokens.len() {
        return None;
    }
    match spec {
        MacroFragSpec::Ident => {
            match &tokens[pos] {
                MacroLitToken::Ident(_) => Some((vec![tokens[pos].clone()], 1)),
                _ => None,
            }
        }
        MacroFragSpec::Literal => {
            match &tokens[pos] {
                MacroLitToken::IntLit(_) | MacroLitToken::FloatLit(_) |
                MacroLitToken::StringLit(_) | MacroLitToken::BoolLit(_) => {
                    Some((vec![tokens[pos].clone()], 1))
                }
                _ => None,
            }
        }
        MacroFragSpec::Expr => {
            // Capture an expression: greedily take tokens until we hit a comma or
            // closing delimiter at depth 0.
            let mut depth = 0i32;
            let mut end = pos;
            while end < tokens.len() {
                match &tokens[end] {
                    MacroLitToken::LParen | MacroLitToken::LBracket | MacroLitToken::LBrace => {
                        depth += 1;
                    }
                    MacroLitToken::RParen | MacroLitToken::RBracket | MacroLitToken::RBrace => {
                        if depth == 0 {
                            break;
                        }
                        depth -= 1;
                    }
                    MacroLitToken::Comma if depth == 0 => break,
                    MacroLitToken::Semicolon if depth == 0 => break,
                    _ => {}
                }
                end += 1;
            }
            if end == pos {
                return None;
            }
            let frag = tokens[pos..end].to_vec();
            Some((frag, end - pos))
        }
        MacroFragSpec::Ty => {
            // Type is just an identifier
            match &tokens[pos] {
                MacroLitToken::Ident(_) => Some((vec![tokens[pos].clone()], 1)),
                _ => None,
            }
        }
        MacroFragSpec::Block => {
            // Capture balanced braces
            if !matches!(&tokens[pos], MacroLitToken::LBrace) {
                return None;
            }
            let mut depth = 0i32;
            let mut end = pos;
            while end < tokens.len() {
                match &tokens[end] {
                    MacroLitToken::LBrace => depth += 1,
                    MacroLitToken::RBrace => {
                        depth -= 1;
                        if depth == 0 {
                            end += 1;
                            break;
                        }
                    }
                    _ => {}
                }
                end += 1;
            }
            let frag = tokens[pos..end].to_vec();
            Some((frag, end - pos))
        }
    }
}

/// Compare two MacroLitTokens for equality.
fn lit_tokens_eq(a: &MacroLitToken, b: &MacroLitToken) -> bool {
    a == b
}

/// Substitute captured fragments into a macro body template.
/// Returns expanded tokens.
fn substitute_body(
    body: &[MacroBodyToken],
    captures: &HashMap<String, MacroFragment>,
) -> Result<Vec<MacroLitToken>, HexaError> {
    let mut output = Vec::new();

    for token in body {
        match token {
            MacroBodyToken::Literal(lit) => {
                output.push(lit.clone());
            }
            MacroBodyToken::Var(name) => {
                match captures.get(name) {
                    Some(MacroFragment::Single(toks)) => {
                        output.extend(toks.iter().cloned());
                    }
                    Some(MacroFragment::Repeated(_)) => {
                        return Err(HexaError {
                            class: ErrorClass::Syntax,
                            message: format!("cannot use repeated capture '{}' outside of $()*", name),
                            line: 0, col: 0, hint: None,
                        });
                    }
                    None => {
                        return Err(HexaError {
                            class: ErrorClass::Syntax,
                            message: format!("undefined macro variable: ${}", name),
                            line: 0, col: 0, hint: None,
                        });
                    }
                }
            }
            MacroBodyToken::Repetition(inner_body, _kind) => {
                // Find the first repeated capture to determine iteration count
                let rep_count = find_repetition_count(inner_body, captures);
                for i in 0..rep_count {
                    let iter_tokens = substitute_repetition_iter(inner_body, captures, i)?;
                    output.extend(iter_tokens);
                }
            }
        }
    }

    Ok(output)
}

/// Find how many iterations a repetition should expand.
fn find_repetition_count(
    body: &[MacroBodyToken],
    captures: &HashMap<String, MacroFragment>,
) -> usize {
    for token in body {
        match token {
            MacroBodyToken::Var(name) => {
                if let Some(MacroFragment::Repeated(items)) = captures.get(name) {
                    return items.len();
                }
            }
            MacroBodyToken::Repetition(inner, _) => {
                let count = find_repetition_count(inner, captures);
                if count > 0 {
                    return count;
                }
            }
            _ => {}
        }
    }
    0
}

/// Substitute one iteration of a repetition.
fn substitute_repetition_iter(
    body: &[MacroBodyToken],
    captures: &HashMap<String, MacroFragment>,
    index: usize,
) -> Result<Vec<MacroLitToken>, HexaError> {
    let mut output = Vec::new();
    for token in body {
        match token {
            MacroBodyToken::Literal(lit) => {
                output.push(lit.clone());
            }
            MacroBodyToken::Var(name) => {
                match captures.get(name) {
                    Some(MacroFragment::Repeated(items)) => {
                        if index < items.len() {
                            output.extend(items[index].iter().cloned());
                        }
                    }
                    Some(MacroFragment::Single(toks)) => {
                        output.extend(toks.iter().cloned());
                    }
                    None => {
                        return Err(HexaError {
                            class: ErrorClass::Syntax,
                            message: format!("undefined macro variable: ${}", name),
                            line: 0, col: 0, hint: None,
                        });
                    }
                }
            }
            MacroBodyToken::Repetition(inner, _) => {
                // Nested repetition — flatten
                let nested = substitute_repetition_iter(inner, captures, index)?;
                output.extend(nested);
            }
        }
    }
    Ok(output)
}

/// Apply hygiene: rename let-bound identifiers introduced by the macro.
fn apply_hygiene(stmts: &mut Vec<Stmt>) {
    let mut renames: HashMap<String, String> = HashMap::new();

    for stmt in stmts.iter_mut() {
        hygiene_stmt(stmt, &mut renames);
    }
}

fn hygiene_stmt(stmt: &mut Stmt, renames: &mut HashMap<String, String>) {
    match stmt {
        Stmt::Let(name, _, expr, _) => {
            // Rename this binding
            let new_name = gensym(name);
            renames.insert(name.clone(), new_name.clone());
            *name = new_name;
            if let Some(e) = expr {
                hygiene_expr(e, renames);
            }
        }
        Stmt::Expr(e) => hygiene_expr(e, renames),
        Stmt::Return(Some(e)) => hygiene_expr(e, renames),
        Stmt::Assign(lhs, rhs) => {
            hygiene_expr(lhs, renames);
            hygiene_expr(rhs, renames);
        }
        Stmt::For(var, iter_expr, body) => {
            hygiene_expr(iter_expr, renames);
            let new_var = gensym(var);
            renames.insert(var.clone(), new_var.clone());
            *var = new_var;
            for s in body {
                hygiene_stmt(s, renames);
            }
        }
        _ => {} // Other statements don't introduce bindings typically
    }
}

fn hygiene_expr(expr: &mut Expr, renames: &HashMap<String, String>) {
    match expr {
        Expr::Ident(name) => {
            if let Some(new_name) = renames.get(name) {
                *name = new_name.clone();
            }
        }
        Expr::Binary(l, _, r) => {
            hygiene_expr(l, renames);
            hygiene_expr(r, renames);
        }
        Expr::Unary(_, e) => hygiene_expr(e, renames),
        Expr::Call(callee, args) => {
            hygiene_expr(callee, renames);
            for a in args {
                hygiene_expr(a, renames);
            }
        }
        Expr::Array(items) => {
            for item in items {
                hygiene_expr(item, renames);
            }
        }
        Expr::Field(obj, _) => hygiene_expr(obj, renames),
        Expr::Index(arr, idx) => {
            hygiene_expr(arr, renames);
            hygiene_expr(idx, renames);
        }
        Expr::If(cond, then_block, else_block) => {
            hygiene_expr(cond, renames);
            for s in then_block {
                hygiene_stmt(s, &mut renames.clone());
            }
            if let Some(eb) = else_block {
                for s in eb {
                    hygiene_stmt(s, &mut renames.clone());
                }
            }
        }
        Expr::Block(stmts) => {
            let mut sub_renames = renames.clone();
            for s in stmts {
                hygiene_stmt(s, &mut sub_renames);
            }
        }
        _ => {} // Other expr types don't need renaming
    }
}

/// Convert expanded MacroLitTokens back into Hexa source code, then parse.
fn tokens_to_stmts(
    tokens: &[MacroLitToken],
    line: usize,
    col: usize,
) -> Result<Vec<Stmt>, HexaError> {
    // Convert tokens to source string
    let source = macro_tokens_to_source(tokens);

    // Parse the expanded source
    let lexer_tokens = crate::lexer::Lexer::new(&source).tokenize()
        .map_err(|e| HexaError {
            class: ErrorClass::Syntax,
            message: format!("macro expansion error: {}", e),
            line, col, hint: None,
        })?;

    let mut parser = crate::parser::Parser::new(lexer_tokens);
    let mut stmts = parser.parse()
        .map_err(|mut e| {
            e.message = format!("in macro expansion: {}", e.message);
            e.line = line;
            e.col = col;
            e
        })?;

    // Apply hygiene to introduced bindings
    apply_hygiene(&mut stmts);

    Ok(stmts)
}

/// Convert MacroLitTokens to source code string.
fn macro_tokens_to_source(tokens: &[MacroLitToken]) -> String {
    let mut parts = Vec::new();
    for tok in tokens {
        let s = match tok {
            MacroLitToken::Ident(name) => name.clone(),
            MacroLitToken::IntLit(n) => n.to_string(),
            MacroLitToken::FloatLit(n) => format!("{}", n),
            MacroLitToken::StringLit(s) => format!("\"{}\"", s),
            MacroLitToken::BoolLit(b) => b.to_string(),
            MacroLitToken::LParen => "(".into(),
            MacroLitToken::RParen => ")".into(),
            MacroLitToken::LBrace => "{".into(),
            MacroLitToken::RBrace => "}".into(),
            MacroLitToken::LBracket => "[".into(),
            MacroLitToken::RBracket => "]".into(),
            MacroLitToken::Comma => ",".into(),
            MacroLitToken::Colon => ":".into(),
            MacroLitToken::ColonColon => "::".into(),
            MacroLitToken::Semicolon => ";".into(),
            MacroLitToken::Dot => ".".into(),
            MacroLitToken::Plus => "+".into(),
            MacroLitToken::Minus => "-".into(),
            MacroLitToken::Star => "*".into(),
            MacroLitToken::Slash => "/".into(),
            MacroLitToken::Percent => "%".into(),
            MacroLitToken::Power => "**".into(),
            MacroLitToken::Eq => "=".into(),
            MacroLitToken::EqEq => "==".into(),
            MacroLitToken::NotEq => "!=".into(),
            MacroLitToken::Lt => "<".into(),
            MacroLitToken::Gt => ">".into(),
            MacroLitToken::LtEq => "<=".into(),
            MacroLitToken::GtEq => ">=".into(),
            MacroLitToken::And => "&&".into(),
            MacroLitToken::Or => "||".into(),
            MacroLitToken::Not => "!".into(),
            MacroLitToken::Arrow => "->".into(),
            MacroLitToken::DotDot => "..".into(),
            MacroLitToken::DotDotEq => "..=".into(),
            MacroLitToken::BitAnd => "&".into(),
            MacroLitToken::BitOr => "|".into(),
            MacroLitToken::BitXor => "^".into(),
            MacroLitToken::BitNot => "~".into(),
            MacroLitToken::Let => "let".into(),
            MacroLitToken::Mut => "mut".into(),
            MacroLitToken::Fn => "fn".into(),
            MacroLitToken::Return => "return".into(),
            MacroLitToken::If => "if".into(),
            MacroLitToken::Else => "else".into(),
            MacroLitToken::For => "for".into(),
            MacroLitToken::While => "while".into(),
            MacroLitToken::Loop => "loop".into(),
            MacroLitToken::Struct => "struct".into(),
            MacroLitToken::Enum => "enum".into(),
            MacroLitToken::Trait => "trait".into(),
            MacroLitToken::Impl => "impl".into(),
            MacroLitToken::Match => "match".into(),
            MacroLitToken::True => "true".into(),
            MacroLitToken::False => "false".into(),
            MacroLitToken::Newline => "\n".into(),
        };
        parts.push(s);
    }
    parts.join(" ")
}

/// Generate derive implementations for a struct.
/// Returns statements that implement the derived traits.
pub fn generate_derive(
    type_name: &str,
    traits: &[String],
    fields: &[(String, String, Visibility)],
    line: usize,
    col: usize,
) -> Result<Vec<Stmt>, HexaError> {
    let mut stmts = Vec::new();

    for trait_name in traits {
        match trait_name.as_str() {
            "Display" => {
                stmts.extend(derive_display(type_name, fields, line, col)?);
            }
            "Debug" => {
                stmts.extend(derive_debug(type_name, fields, line, col)?);
            }
            "Eq" => {
                stmts.extend(derive_eq(type_name, fields, line, col)?);
            }
            _ => {
                return Err(HexaError {
                    class: ErrorClass::Syntax,
                    message: format!("unknown derive trait: {}", trait_name),
                    line, col, hint: None,
                });
            }
        }
    }

    Ok(stmts)
}

/// Generate Display impl: fn to_string(self) -> string
fn derive_display(
    type_name: &str,
    fields: &[(String, String, Visibility)],
    _line: usize,
    _col: usize,
) -> Result<Vec<Stmt>, HexaError> {
    // Generate: impl Display for TypeName { fn to_string(self) -> string { ... } }
    // Build source code and parse it
    let mut field_parts = Vec::new();
    for (name, _, _) in fields {
        field_parts.push(format!("\"{}: \" + to_string(self.{})", name, name));
    }
    let body_expr = if field_parts.is_empty() {
        format!("\"{}{{}}\"", type_name)
    } else {
        format!("\"{}{{ \" + {} + \" }}\"", type_name, field_parts.join(" + \", \" + "))
    };

    let source = format!(
        "impl {} {{\n  fn display(self) -> string {{\n    return {}\n  }}\n}}",
        type_name, body_expr
    );

    let tokens = crate::lexer::Lexer::new(&source).tokenize()
        .map_err(|e| HexaError {
            class: ErrorClass::Syntax,
            message: format!("derive(Display) error: {}", e),
            line: 0, col: 0, hint: None,
        })?;
    crate::parser::Parser::new(tokens).parse()
        .map_err(|mut e| {
            e.message = format!("derive(Display) error: {}", e.message);
            e
        })
}

/// Generate Debug impl: fn debug(self) -> string
fn derive_debug(
    type_name: &str,
    fields: &[(String, String, Visibility)],
    _line: usize,
    _col: usize,
) -> Result<Vec<Stmt>, HexaError> {
    let mut field_parts = Vec::new();
    for (name, _, _) in fields {
        field_parts.push(format!("\"{}: \" + to_string(self.{})", name, name));
    }
    let body_expr = if field_parts.is_empty() {
        format!("\"{}{{}}\"", type_name)
    } else {
        format!("\"{}{{ \" + {} + \" }}\"", type_name, field_parts.join(" + \", \" + "))
    };

    let source = format!(
        "impl {} {{\n  fn debug(self) -> string {{\n    return {}\n  }}\n}}",
        type_name, body_expr
    );

    let tokens = crate::lexer::Lexer::new(&source).tokenize()
        .map_err(|e| HexaError {
            class: ErrorClass::Syntax,
            message: format!("derive(Debug) error: {}", e),
            line: 0, col: 0, hint: None,
        })?;
    crate::parser::Parser::new(tokens).parse()
        .map_err(|mut e| {
            e.message = format!("derive(Debug) error: {}", e.message);
            e
        })
}

/// Generate Eq impl: fn eq(self, other) -> bool
fn derive_eq(
    type_name: &str,
    fields: &[(String, String, Visibility)],
    _line: usize,
    _col: usize,
) -> Result<Vec<Stmt>, HexaError> {
    let mut comparisons = Vec::new();
    for (name, _, _) in fields {
        comparisons.push(format!("self.{} == other.{}", name, name));
    }
    let body_expr = if comparisons.is_empty() {
        "true".to_string()
    } else {
        comparisons.join(" && ")
    };

    let source = format!(
        "impl {} {{\n  fn eq(self, other) -> bool {{\n    return {}\n  }}\n}}",
        type_name, body_expr
    );

    let tokens = crate::lexer::Lexer::new(&source).tokenize()
        .map_err(|e| HexaError {
            class: ErrorClass::Syntax,
            message: format!("derive(Eq) error: {}", e),
            line: 0, col: 0, hint: None,
        })?;
    crate::parser::Parser::new(tokens).parse()
        .map_err(|mut e| {
            e.message = format!("derive(Eq) error: {}", e.message);
            e
        })
}

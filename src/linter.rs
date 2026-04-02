#![allow(dead_code)]

//! HEXA-LANG Basic Linter
//!
//! Warnings:
//! - Unused variables (defined but never referenced)
//! - Shadowed variables (same name in same scope)
//! - Empty proof blocks
//! - Unreachable code after return
//! - Info: consciousness builtins available but not used in intent/verify blocks

use std::collections::{HashMap, HashSet};
use crate::ast::*;

/// A single lint warning or info message.
#[derive(Debug, Clone)]
pub struct LintMessage {
    pub level: LintLevel,
    pub message: String,
    pub line: usize,
}

#[derive(Debug, Clone, PartialEq)]
pub enum LintLevel {
    Warn,
    Info,
}

impl std::fmt::Display for LintMessage {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        let prefix = match self.level {
            LintLevel::Warn => "warning",
            LintLevel::Info => "info",
        };
        if self.line > 0 {
            write!(f, "{}: line {}: {}", prefix, self.line, self.message)
        } else {
            write!(f, "{}: {}", prefix, self.message)
        }
    }
}

/// Lint a HEXA source file.
pub fn lint_source(source: &str) -> Result<Vec<LintMessage>, String> {
    let tokens = crate::lexer::Lexer::new(source).tokenize()
        .map_err(|e| format!("Lexer error: {}", e))?;
    let result = crate::parser::Parser::new(tokens).parse_with_spans()
        .map_err(|e| format!("Parse error: {}", e))?;
    Ok(lint_stmts(&result.stmts, &result.spans))
}

fn lint_stmts(stmts: &[Stmt], spans: &[(usize, usize)]) -> Vec<LintMessage> {
    let mut messages = Vec::new();

    // 1. Collect defined variables and referenced variables
    let mut defined: HashMap<String, usize> = HashMap::new(); // name -> line
    let mut referenced: HashSet<String> = HashSet::new();
    let mut scope_defs: HashMap<String, usize> = HashMap::new(); // for shadowing detection

    for (i, stmt) in stmts.iter().enumerate() {
        let line = spans.get(i).map(|s| s.0).unwrap_or(0);
        collect_definitions(stmt, &mut defined, &mut scope_defs, &mut referenced, &mut messages, line);
    }

    // 2. Check for unused variables
    for (name, line) in &defined {
        // Skip variables starting with _ (conventional ignore)
        if name.starts_with('_') {
            continue;
        }
        // Skip function names (they might be used elsewhere)
        if !referenced.contains(name) {
            messages.push(LintMessage {
                level: LintLevel::Warn,
                message: format!("unused variable: `{}`", name),
                line: *line,
            });
        }
    }

    // 3. Check for empty proof blocks
    for (i, stmt) in stmts.iter().enumerate() {
        let line = spans.get(i).map(|s| s.0).unwrap_or(0);
        check_empty_proof(stmt, &mut messages, line);
    }

    // 4. Check for unreachable code after return
    for (i, stmt) in stmts.iter().enumerate() {
        let line = spans.get(i).map(|s| s.0).unwrap_or(0);
        check_unreachable(stmt, &mut messages, line);
    }

    // 5. Check for consciousness builtins info in intent/verify blocks
    check_consciousness_info(stmts, &mut messages);

    messages
}

fn collect_definitions(
    stmt: &Stmt,
    defined: &mut HashMap<String, usize>,
    scope_defs: &mut HashMap<String, usize>,
    referenced: &mut HashSet<String>,
    messages: &mut Vec<LintMessage>,
    line: usize,
) {
    match stmt {
        Stmt::Let(name, _, expr, _) => {
            // Check for shadowing in same scope
            if let Some(prev_line) = scope_defs.get(name) {
                messages.push(LintMessage {
                    level: LintLevel::Warn,
                    message: format!("variable `{}` shadows previous definition at line {}", name, prev_line),
                    line,
                });
            }
            defined.insert(name.clone(), line);
            scope_defs.insert(name.clone(), line);
            if let Some(e) = expr {
                collect_expr_refs(e, referenced);
            }
        }
        Stmt::FnDecl(decl) => {
            // Function name is defined
            defined.insert(decl.name.clone(), line);
            scope_defs.insert(decl.name.clone(), line);
            // Params and body references
            let mut fn_refs = HashSet::new();
            for s in &decl.body {
                collect_stmt_refs(s, &mut fn_refs);
            }
            referenced.extend(fn_refs);
            // Params are referenced within the function
            for p in &decl.params {
                referenced.insert(p.name.clone());
            }
        }
        Stmt::Assign(lhs, rhs) => {
            collect_expr_refs(lhs, referenced);
            collect_expr_refs(rhs, referenced);
        }
        Stmt::Expr(e) => {
            collect_expr_refs(e, referenced);
        }
        Stmt::Return(Some(e)) => {
            collect_expr_refs(e, referenced);
        }
        Stmt::For(var, iter, body) => {
            referenced.insert(var.clone());
            collect_expr_refs(iter, referenced);
            for s in body {
                collect_stmt_refs(s, referenced);
            }
        }
        Stmt::While(cond, body) => {
            collect_expr_refs(cond, referenced);
            for s in body {
                collect_stmt_refs(s, referenced);
            }
        }
        Stmt::Proof(_, body) | Stmt::Loop(body) | Stmt::Spawn(body) => {
            for s in body {
                collect_stmt_refs(s, referenced);
            }
        }
        Stmt::Assert(e) | Stmt::Throw(e) => {
            collect_expr_refs(e, referenced);
        }
        Stmt::Intent(_, fields) => {
            for (_, e) in fields {
                collect_expr_refs(e, referenced);
            }
        }
        Stmt::Verify(_, body) => {
            for s in body {
                collect_stmt_refs(s, referenced);
            }
        }
        Stmt::TryCatch(try_block, _, catch_block) => {
            for s in try_block { collect_stmt_refs(s, referenced); }
            for s in catch_block { collect_stmt_refs(s, referenced); }
        }
        _ => {}
    }
}

fn collect_stmt_refs(stmt: &Stmt, refs: &mut HashSet<String>) {
    match stmt {
        Stmt::Let(_, _, Some(e), _) => collect_expr_refs(e, refs),
        Stmt::Assign(lhs, rhs) => {
            collect_expr_refs(lhs, refs);
            collect_expr_refs(rhs, refs);
        }
        Stmt::Expr(e) | Stmt::Return(Some(e)) | Stmt::Assert(e) | Stmt::Throw(e) => {
            collect_expr_refs(e, refs);
        }
        Stmt::For(_, iter, body) => {
            collect_expr_refs(iter, refs);
            for s in body { collect_stmt_refs(s, refs); }
        }
        Stmt::While(cond, body) => {
            collect_expr_refs(cond, refs);
            for s in body { collect_stmt_refs(s, refs); }
        }
        Stmt::Proof(_, body) | Stmt::Loop(body) | Stmt::Spawn(body) | Stmt::Verify(_, body) => {
            for s in body { collect_stmt_refs(s, refs); }
        }
        Stmt::TryCatch(try_block, _, catch_block) => {
            for s in try_block { collect_stmt_refs(s, refs); }
            for s in catch_block { collect_stmt_refs(s, refs); }
        }
        Stmt::FnDecl(decl) | Stmt::AsyncFnDecl(decl) => {
            for s in &decl.body { collect_stmt_refs(s, refs); }
        }
        Stmt::Intent(_, fields) => {
            for (_, e) in fields { collect_expr_refs(e, refs); }
        }
        _ => {}
    }
}

fn collect_expr_refs(expr: &Expr, refs: &mut HashSet<String>) {
    match expr {
        Expr::Ident(name) => { refs.insert(name.clone()); }
        Expr::Binary(l, _, r) => {
            collect_expr_refs(l, refs);
            collect_expr_refs(r, refs);
        }
        Expr::Unary(_, e) | Expr::Own(e) | Expr::MoveExpr(e) | Expr::Borrow(e) | Expr::Await(e) => {
            collect_expr_refs(e, refs);
        }
        Expr::Call(callee, args) => {
            collect_expr_refs(callee, refs);
            for a in args { collect_expr_refs(a, refs); }
        }
        Expr::Lambda(_, body) => {
            collect_expr_refs(body, refs);
        }
        Expr::Array(items) | Expr::Tuple(items) => {
            for i in items { collect_expr_refs(i, refs); }
        }
        Expr::Field(obj, _) => collect_expr_refs(obj, refs),
        Expr::Index(obj, idx) => {
            collect_expr_refs(obj, refs);
            collect_expr_refs(idx, refs);
        }
        Expr::If(cond, then_block, else_block) => {
            collect_expr_refs(cond, refs);
            for s in then_block { collect_stmt_refs(s, refs); }
            if let Some(eb) = else_block {
                for s in eb { collect_stmt_refs(s, refs); }
            }
        }
        Expr::Match(scrutinee, arms) => {
            collect_expr_refs(scrutinee, refs);
            for arm in arms {
                if let Some(g) = &arm.guard { collect_expr_refs(g, refs); }
                for s in &arm.body { collect_stmt_refs(s, refs); }
            }
        }
        Expr::Block(stmts) => {
            for s in stmts { collect_stmt_refs(s, refs); }
        }
        Expr::Range(s, e, _) => {
            collect_expr_refs(s, refs);
            collect_expr_refs(e, refs);
        }
        Expr::StructInit(_, fields) => {
            for (_, e) in fields { collect_expr_refs(e, refs); }
        }
        Expr::MapLit(pairs) => {
            for (k, v) in pairs {
                collect_expr_refs(k, refs);
                collect_expr_refs(v, refs);
            }
        }
        Expr::EnumPath(_, _, Some(d)) => collect_expr_refs(d, refs),
        _ => {}
    }
}

fn check_empty_proof(stmt: &Stmt, messages: &mut Vec<LintMessage>, line: usize) {
    if let Stmt::Proof(name, body) = stmt {
        if body.is_empty() {
            messages.push(LintMessage {
                level: LintLevel::Warn,
                message: format!("empty proof block: `{}`", name),
                line,
            });
        }
    }
}

fn check_unreachable(stmt: &Stmt, messages: &mut Vec<LintMessage>, line: usize) {
    match stmt {
        Stmt::FnDecl(decl) => check_unreachable_in_body(&decl.body, messages, line),
        Stmt::AsyncFnDecl(decl) => check_unreachable_in_body(&decl.body, messages, line),
        Stmt::Proof(_, body) => check_unreachable_in_body(body, messages, line),
        _ => {}
    }
}

fn check_unreachable_in_body(body: &[Stmt], messages: &mut Vec<LintMessage>, _line: usize) {
    for (i, stmt) in body.iter().enumerate() {
        if matches!(stmt, Stmt::Return(_)) && i + 1 < body.len() {
            messages.push(LintMessage {
                level: LintLevel::Warn,
                message: "unreachable code after return".to_string(),
                line: 0, // We don't have per-stmt line info inside blocks
            });
            break;
        }
    }
}

const CONSCIOUSNESS_BUILTINS: &[&str] = &[
    "consciousness_vector", "psi_coupling", "psi_balance", "psi_steps",
    "psi_entropy", "phi_predict", "laws", "meta_laws",
    "consciousness_phi", "consciousness_max_cells",
];

fn check_consciousness_info(stmts: &[Stmt], messages: &mut Vec<LintMessage>) {
    let has_intent_or_verify = stmts.iter().any(|s| matches!(s, Stmt::Intent(..) | Stmt::Verify(..)));
    if !has_intent_or_verify {
        return;
    }

    // Collect all referenced names in intent/verify blocks
    let mut used_in_cv = HashSet::new();
    for stmt in stmts {
        match stmt {
            Stmt::Intent(_, fields) => {
                for (_, e) in fields { collect_expr_refs(e, &mut used_in_cv); }
            }
            Stmt::Verify(_, body) => {
                for s in body { collect_stmt_refs(s, &mut used_in_cv); }
            }
            _ => {}
        }
    }

    let unused_consciousness: Vec<&&str> = CONSCIOUSNESS_BUILTINS.iter()
        .filter(|name| !used_in_cv.contains(**name))
        .collect();

    if !unused_consciousness.is_empty() && unused_consciousness.len() < CONSCIOUSNESS_BUILTINS.len() {
        // Only show if some are used (meaning user is using consciousness features)
        messages.push(LintMessage {
            level: LintLevel::Info,
            message: format!("consciousness builtins available but not used in intent/verify: {}",
                unused_consciousness.iter().map(|s| format!("`{}`", s)).collect::<Vec<_>>().join(", ")),
            line: 0,
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lint_unused_variable() {
        let msgs = lint_source("let x = 42\nlet y = 10\nprintln(y)").unwrap();
        let warns: Vec<_> = msgs.iter()
            .filter(|m| m.level == LintLevel::Warn && m.message.contains("unused"))
            .collect();
        assert!(warns.iter().any(|w| w.message.contains("`x`")), "should warn about unused x");
    }

    #[test]
    fn test_lint_shadowed_variable() {
        let msgs = lint_source("let x = 1\nlet x = 2\nprintln(x)").unwrap();
        let warns: Vec<_> = msgs.iter()
            .filter(|m| m.level == LintLevel::Warn && m.message.contains("shadows"))
            .collect();
        assert!(!warns.is_empty(), "should warn about shadowed x");
    }

    #[test]
    fn test_lint_empty_proof() {
        let msgs = lint_source("proof empty_test {}").unwrap();
        let warns: Vec<_> = msgs.iter()
            .filter(|m| m.level == LintLevel::Warn && m.message.contains("empty proof"))
            .collect();
        assert!(!warns.is_empty(), "should warn about empty proof block");
    }

    #[test]
    fn test_lint_unreachable_code() {
        let msgs = lint_source("fn foo() {\n    return 1\n    let x = 2\n}").unwrap();
        let warns: Vec<_> = msgs.iter()
            .filter(|m| m.level == LintLevel::Warn && m.message.contains("unreachable"))
            .collect();
        assert!(!warns.is_empty(), "should warn about unreachable code");
    }

    #[test]
    fn test_lint_no_warnings_clean_code() {
        let msgs = lint_source("let x = 42\nprintln(x)").unwrap();
        let warns: Vec<_> = msgs.iter()
            .filter(|m| m.level == LintLevel::Warn)
            .collect();
        assert!(warns.is_empty(), "clean code should have no warnings, got: {:?}", warns);
    }

    #[test]
    fn test_lint_underscore_not_warned() {
        let msgs = lint_source("let _unused = 42\nprintln(1)").unwrap();
        let warns: Vec<_> = msgs.iter()
            .filter(|m| m.level == LintLevel::Warn && m.message.contains("_unused"))
            .collect();
        assert!(warns.is_empty(), "underscore-prefixed vars should not warn");
    }
}

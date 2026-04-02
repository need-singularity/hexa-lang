//! SIMD auto-vectorization hints for HEXA-LANG.
//!
//! Provides the `@simd` annotation that marks loops for vectorization.
//! At the AST level, this adds metadata to `for` loops indicating they
//! should be vectorized when compiled through Cranelift JIT.
//!
//! Usage in HEXA:
//!   @simd
//!   for i in 0..len(arr) {
//!       arr[i] = arr[i] * 2
//!   }
//!
//! The SIMD pass:
//! 1. Identifies loops annotated with @simd
//! 2. Validates the loop body is vectorizable (no branches, simple ops)
//! 3. Emits SimdHint metadata that the JIT backend can use
//!
//! Actual vectorization is done by Cranelift — this module only provides
//! the analysis and annotation infrastructure.

#![allow(dead_code)]

use crate::ast::*;

/// SIMD vectorization hint attached to a loop.
#[derive(Debug, Clone)]
pub struct SimdHint {
    /// The loop variable name.
    pub loop_var: String,
    /// Suggested vector width (elements per SIMD lane). 0 = auto-detect.
    pub width: usize,
    /// Whether the loop body was validated as vectorizable.
    pub is_vectorizable: bool,
    /// Reason if not vectorizable.
    pub reason: Option<String>,
    /// Array variables accessed in the loop body.
    pub accessed_arrays: Vec<String>,
}

/// Annotation types supported by the compiler.
#[derive(Debug, Clone, PartialEq)]
pub enum Annotation {
    /// @simd — mark a loop for SIMD vectorization
    Simd,
    /// @simd(width) — mark with explicit vector width
    SimdWidth(usize),
    /// @inline — hint to inline a function
    Inline,
    /// @noinline — hint to not inline a function
    NoInline,
    /// @cold — mark a function/block as unlikely to be executed
    Cold,
    /// @hot — mark a function/block as frequently executed (PGO hint)
    Hot,
}

/// Parse an annotation string like "@simd" or "@simd(4)".
pub fn parse_annotation(s: &str) -> Option<Annotation> {
    let s = s.trim();
    if s == "@simd" || s == "simd" {
        Some(Annotation::Simd)
    } else if s.starts_with("@simd(") || s.starts_with("simd(") {
        let inner = s.trim_start_matches("@simd(")
            .trim_start_matches("simd(")
            .trim_end_matches(')');
        inner.parse::<usize>().ok().map(Annotation::SimdWidth)
    } else if s == "@inline" || s == "inline" {
        Some(Annotation::Inline)
    } else if s == "@noinline" || s == "noinline" {
        Some(Annotation::NoInline)
    } else if s == "@cold" || s == "cold" {
        Some(Annotation::Cold)
    } else if s == "@hot" || s == "hot" {
        Some(Annotation::Hot)
    } else {
        None
    }
}

/// Analyze a for-loop body and determine if it's vectorizable.
/// Returns a SimdHint with the analysis result.
pub fn analyze_simd_loop(var: &str, body: &[Stmt], width: usize) -> SimdHint {
    let mut hint = SimdHint {
        loop_var: var.to_string(),
        width,
        is_vectorizable: true,
        reason: None,
        accessed_arrays: Vec::new(),
    };

    for stmt in body {
        if !check_stmt_vectorizable(stmt, var, &mut hint) {
            hint.is_vectorizable = false;
            break;
        }
    }

    hint
}

/// Check if a statement is suitable for SIMD vectorization.
fn check_stmt_vectorizable(stmt: &Stmt, loop_var: &str, hint: &mut SimdHint) -> bool {
    match stmt {
        // Simple assignment: arr[i] = expr
        Stmt::Assign(lhs, rhs) => {
            collect_array_accesses(lhs, loop_var, hint);
            collect_array_accesses_expr(rhs, loop_var, hint);
            // Check lhs is array indexing
            if !is_simple_expr(rhs) {
                hint.reason = Some("complex expression in loop body".into());
                return false;
            }
            true
        }
        // Let binding with simple expression
        Stmt::Let(_, _, Some(expr), _) => {
            collect_array_accesses_expr(expr, loop_var, hint);
            is_simple_expr(expr)
        }
        // Expression statement (e.g., function call)
        Stmt::Expr(expr) => {
            // Function calls are not vectorizable in general
            if contains_call(expr) {
                hint.reason = Some("function call in loop body".into());
                return false;
            }
            true
        }
        // Control flow breaks vectorization
        Stmt::Return(_) | Stmt::Throw(_) => {
            hint.reason = Some("control flow in loop body".into());
            false
        }
        // Nested loops not vectorizable
        Stmt::For(_, _, _) | Stmt::While(_, _) | Stmt::Loop(_) => {
            hint.reason = Some("nested loop in body".into());
            false
        }
        _ => {
            hint.reason = Some("unsupported statement type".into());
            false
        }
    }
}

/// Check if an expression is "simple" (arithmetic + array access).
fn is_simple_expr(expr: &Expr) -> bool {
    match expr {
        Expr::IntLit(_) | Expr::FloatLit(_) | Expr::BoolLit(_) => true,
        Expr::Ident(_) => true,
        Expr::Index(_, _) => true,
        Expr::Binary(l, op, r) => {
            matches!(op, BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div
                     | BinOp::Rem | BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor)
                && is_simple_expr(l)
                && is_simple_expr(r)
        }
        Expr::Unary(op, inner) => {
            matches!(op, UnaryOp::Neg | UnaryOp::BitNot) && is_simple_expr(inner)
        }
        _ => false,
    }
}

/// Check if an expression contains a function/method call.
fn contains_call(expr: &Expr) -> bool {
    match expr {
        Expr::Call(_, _) => true,
        Expr::Binary(l, _, r) => contains_call(l) || contains_call(r),
        Expr::Unary(_, inner) => contains_call(inner),
        Expr::Index(a, i) => contains_call(a) || contains_call(i),
        _ => false,
    }
}

/// Collect array variable names accessed via indexing with the loop variable.
fn collect_array_accesses(expr: &Expr, loop_var: &str, hint: &mut SimdHint) {
    collect_array_accesses_expr(expr, loop_var, hint);
}

fn collect_array_accesses_expr(expr: &Expr, loop_var: &str, hint: &mut SimdHint) {
    match expr {
        Expr::Index(arr, idx) => {
            if let Expr::Ident(name) = arr.as_ref() {
                if matches!(idx.as_ref(), Expr::Ident(v) if v == loop_var) {
                    if !hint.accessed_arrays.contains(name) {
                        hint.accessed_arrays.push(name.clone());
                    }
                }
            }
            collect_array_accesses_expr(arr, loop_var, hint);
            collect_array_accesses_expr(idx, loop_var, hint);
        }
        Expr::Binary(l, _, r) => {
            collect_array_accesses_expr(l, loop_var, hint);
            collect_array_accesses_expr(r, loop_var, hint);
        }
        Expr::Unary(_, inner) => {
            collect_array_accesses_expr(inner, loop_var, hint);
        }
        _ => {}
    }
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_simd_annotation() {
        assert_eq!(parse_annotation("@simd"), Some(Annotation::Simd));
        assert_eq!(parse_annotation("@simd(4)"), Some(Annotation::SimdWidth(4)));
        assert_eq!(parse_annotation("@inline"), Some(Annotation::Inline));
        assert_eq!(parse_annotation("@cold"), Some(Annotation::Cold));
        assert_eq!(parse_annotation("@hot"), Some(Annotation::Hot));
        assert_eq!(parse_annotation("@noinline"), Some(Annotation::NoInline));
        assert!(parse_annotation("@unknown").is_none());
    }

    #[test]
    fn test_simple_vectorizable_loop() {
        // for i in 0..n { arr[i] = arr[i] * 2 }
        let body = vec![
            Stmt::Assign(
                Expr::Index(
                    Box::new(Expr::Ident("arr".into())),
                    Box::new(Expr::Ident("i".into())),
                ),
                Expr::Binary(
                    Box::new(Expr::Index(
                        Box::new(Expr::Ident("arr".into())),
                        Box::new(Expr::Ident("i".into())),
                    )),
                    BinOp::Mul,
                    Box::new(Expr::IntLit(2)),
                ),
            ),
        ];
        let hint = analyze_simd_loop("i", &body, 0);
        assert!(hint.is_vectorizable);
        assert!(hint.accessed_arrays.contains(&"arr".to_string()));
    }

    #[test]
    fn test_non_vectorizable_with_call() {
        // for i in 0..n { println(arr[i]) }
        let body = vec![
            Stmt::Expr(Expr::Call(
                Box::new(Expr::Ident("println".into())),
                vec![Expr::Index(
                    Box::new(Expr::Ident("arr".into())),
                    Box::new(Expr::Ident("i".into())),
                )],
            )),
        ];
        let hint = analyze_simd_loop("i", &body, 0);
        assert!(!hint.is_vectorizable);
        assert!(hint.reason.is_some());
    }

    #[test]
    fn test_non_vectorizable_with_return() {
        let body = vec![Stmt::Return(Some(Expr::IntLit(0)))];
        let hint = analyze_simd_loop("i", &body, 0);
        assert!(!hint.is_vectorizable);
    }

    #[test]
    fn test_non_vectorizable_nested_loop() {
        let body = vec![
            Stmt::For(
                "j".into(),
                Expr::Range(
                    Box::new(Expr::IntLit(0)),
                    Box::new(Expr::IntLit(10)),
                    false,
                ),
                vec![Stmt::Expr(Expr::IntLit(0))],
            ),
        ];
        let hint = analyze_simd_loop("i", &body, 0);
        assert!(!hint.is_vectorizable);
    }

    #[test]
    fn test_simd_width_annotation() {
        assert_eq!(parse_annotation("@simd(8)"), Some(Annotation::SimdWidth(8)));
        assert_eq!(parse_annotation("@simd(16)"), Some(Annotation::SimdWidth(16)));
    }

    #[test]
    fn test_vectorizable_let_binding() {
        // for i in 0..n { let x = arr[i] + 1 }
        let body = vec![
            Stmt::Let(
                "x".into(),
                None,
                Some(Expr::Binary(
                    Box::new(Expr::Index(
                        Box::new(Expr::Ident("arr".into())),
                        Box::new(Expr::Ident("i".into())),
                    )),
                    BinOp::Add,
                    Box::new(Expr::IntLit(1)),
                )),
                Visibility::Private,
            ),
        ];
        let hint = analyze_simd_loop("i", &body, 4);
        assert!(hint.is_vectorizable);
        assert_eq!(hint.width, 4);
    }
}

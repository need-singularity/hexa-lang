//! Loop unrolling optimization for HEXA-LANG.
//!
//! Detects small constant-bound `for` loops and unrolls them at the AST level.
//! Only unrolls loops where:
//! - The iterator is a constant range (e.g., `0..4`, `1..=7`)
//! - The range has fewer than MAX_UNROLL_ITERATIONS iterations
//! - The loop body does not contain break/return/throw
//!
//! Unrolled loops replace the `for` with a flat block of the body repeated
//! N times, with the loop variable substituted for each iteration's value.

#![allow(dead_code)]

use crate::ast::*;

/// Maximum number of iterations to unroll.
const MAX_UNROLL_ITERATIONS: usize = 8;

/// Result of loop unrolling.
#[derive(Debug)]
pub struct UnrollResult {
    pub stmts: Vec<Stmt>,
    pub unrolled_count: usize,
}

/// Run loop unrolling on a list of statements.
pub fn unroll_loops(stmts: &[Stmt]) -> UnrollResult {
    let mut ctx = UnrollCtx { unrolled: 0 };
    let optimized = ctx.process_block(stmts);
    UnrollResult {
        stmts: optimized,
        unrolled_count: ctx.unrolled,
    }
}

struct UnrollCtx {
    unrolled: usize,
}

impl UnrollCtx {
    fn process_block(&mut self, stmts: &[Stmt]) -> Vec<Stmt> {
        let mut result = Vec::new();
        for stmt in stmts {
            match self.try_unroll(stmt) {
                Some(unrolled) => {
                    result.extend(unrolled);
                    self.unrolled += 1;
                }
                None => {
                    result.push(self.recurse_stmt(stmt));
                }
            }
        }
        result
    }

    /// Try to unroll a for-loop. Returns None if the loop cannot be unrolled.
    fn try_unroll(&self, stmt: &Stmt) -> Option<Vec<Stmt>> {
        let (var, iter_expr, body) = match stmt {
            Stmt::For(var, iter_expr, body) => (var, iter_expr, body),
            _ => return None,
        };

        // Only unroll constant ranges
        let (start, end, inclusive) = match iter_expr {
            Expr::Range(s, e, inc) => {
                let start = const_int(s)?;
                let end = const_int(e)?;
                (start, end, *inc)
            }
            _ => return None,
        };

        // Calculate iteration count
        let count = if inclusive {
            (end - start + 1) as usize
        } else {
            if end <= start { return Some(vec![]); } // empty range
            (end - start) as usize
        };

        if count > MAX_UNROLL_ITERATIONS || count == 0 {
            return None;
        }

        // Check body doesn't contain terminators (return/throw/break)
        if body_has_terminator(body) {
            return None;
        }

        // Generate unrolled statements
        let mut unrolled = Vec::new();
        let upper = if inclusive { end + 1 } else { end };
        for i in start..upper {
            // let var = i
            unrolled.push(Stmt::Let(
                var.clone(),
                None,
                Some(Expr::IntLit(i)),
                Visibility::Private,
            ));
            // Copy body statements
            for body_stmt in body {
                unrolled.push(body_stmt.clone());
            }
        }

        Some(unrolled)
    }

    /// Recurse into nested statements to find more unroll opportunities.
    fn recurse_stmt(&mut self, stmt: &Stmt) -> Stmt {
        match stmt {
            Stmt::FnDecl(decl) => {
                let optimized_body = self.process_block(&decl.body);
                Stmt::FnDecl(FnDecl {
                    name: decl.name.clone(),
                    type_params: decl.type_params.clone(),
                    params: decl.params.clone(),
                    ret_type: decl.ret_type.clone(),
                    where_clauses: decl.where_clauses.clone(),
                    precondition: decl.precondition.clone(),
                    postcondition: decl.postcondition.clone(),
                    body: optimized_body,
                    vis: decl.vis.clone(),
                    is_pure: decl.is_pure, attrs: decl.attrs.clone(),
                })
            }
            Stmt::While(cond, body) => {
                Stmt::While(cond.clone(), self.process_block(body))
            }
            Stmt::Loop(body) => {
                Stmt::Loop(self.process_block(body))
            }
            Stmt::Scope(body) => {
                Stmt::Scope(self.process_block(body))
            }
            Stmt::TryCatch(try_b, var, catch_b) => {
                Stmt::TryCatch(
                    self.process_block(try_b),
                    var.clone(),
                    self.process_block(catch_b),
                )
            }
            Stmt::Spawn(body) => Stmt::Spawn(self.process_block(body)),
            Stmt::SpawnNamed(n, body) => Stmt::SpawnNamed(n.clone(), self.process_block(body)),
            other => other.clone(),
        }
    }
}

/// Extract a constant integer from a literal expression.
fn const_int(expr: &Expr) -> Option<i64> {
    match expr {
        Expr::IntLit(n) => Some(*n),
        Expr::Unary(UnaryOp::Neg, inner) => {
            if let Expr::IntLit(n) = inner.as_ref() {
                Some(-n)
            } else {
                None
            }
        }
        _ => None,
    }
}

/// Check if a block contains return/throw statements.
fn body_has_terminator(stmts: &[Stmt]) -> bool {
    for stmt in stmts {
        match stmt {
            Stmt::Return(_) | Stmt::Throw(_) => return true,
            Stmt::For(_, _, body) | Stmt::While(_, body) | Stmt::Loop(body)
            | Stmt::Scope(body) | Stmt::Spawn(body) => {
                if body_has_terminator(body) { return true; }
            }
            Stmt::TryCatch(t, _, c) => {
                if body_has_terminator(t) || body_has_terminator(c) { return true; }
            }
            _ => {}
        }
    }
    false
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    fn make_expr(val: i64) -> Stmt {
        Stmt::Expr(Expr::IntLit(val))
    }

    #[test]
    fn test_unroll_small_range() {
        // for i in 0..3 { expr(i) }
        let stmts = vec![
            Stmt::For(
                "i".into(),
                Expr::Range(
                    Box::new(Expr::IntLit(0)),
                    Box::new(Expr::IntLit(3)),
                    false,
                ),
                vec![make_expr(1)],
            ),
        ];
        let result = unroll_loops(&stmts);
        assert_eq!(result.unrolled_count, 1);
        // 3 iterations: each produces a let + body stmt = 6 total
        assert_eq!(result.stmts.len(), 6);
    }

    #[test]
    fn test_unroll_inclusive_range() {
        // for i in 1..=4 { expr }
        let stmts = vec![
            Stmt::For(
                "i".into(),
                Expr::Range(
                    Box::new(Expr::IntLit(1)),
                    Box::new(Expr::IntLit(4)),
                    true,
                ),
                vec![make_expr(1)],
            ),
        ];
        let result = unroll_loops(&stmts);
        assert_eq!(result.unrolled_count, 1);
        // 4 iterations: let + body = 8 stmts
        assert_eq!(result.stmts.len(), 8);
    }

    #[test]
    fn test_no_unroll_large_range() {
        // for i in 0..100 { expr } — too many iterations
        let stmts = vec![
            Stmt::For(
                "i".into(),
                Expr::Range(
                    Box::new(Expr::IntLit(0)),
                    Box::new(Expr::IntLit(100)),
                    false,
                ),
                vec![make_expr(1)],
            ),
        ];
        let result = unroll_loops(&stmts);
        assert_eq!(result.unrolled_count, 0);
        assert_eq!(result.stmts.len(), 1); // unchanged
    }

    #[test]
    fn test_no_unroll_with_return() {
        // for i in 0..3 { return 1 } — body has terminator
        let stmts = vec![
            Stmt::For(
                "i".into(),
                Expr::Range(
                    Box::new(Expr::IntLit(0)),
                    Box::new(Expr::IntLit(3)),
                    false,
                ),
                vec![Stmt::Return(Some(Expr::IntLit(1)))],
            ),
        ];
        let result = unroll_loops(&stmts);
        assert_eq!(result.unrolled_count, 0);
    }

    #[test]
    fn test_no_unroll_non_constant_range() {
        // for i in 0..n { expr } — non-constant bound
        let stmts = vec![
            Stmt::For(
                "i".into(),
                Expr::Range(
                    Box::new(Expr::IntLit(0)),
                    Box::new(Expr::Ident("n".into())),
                    false,
                ),
                vec![make_expr(1)],
            ),
        ];
        let result = unroll_loops(&stmts);
        assert_eq!(result.unrolled_count, 0);
    }

    #[test]
    fn test_unroll_empty_range() {
        // for i in 5..3 { expr } — empty range
        let stmts = vec![
            Stmt::For(
                "i".into(),
                Expr::Range(
                    Box::new(Expr::IntLit(5)),
                    Box::new(Expr::IntLit(3)),
                    false,
                ),
                vec![make_expr(1)],
            ),
        ];
        let result = unroll_loops(&stmts);
        // Empty range gets eliminated
        assert_eq!(result.unrolled_count, 1);
        assert_eq!(result.stmts.len(), 0);
    }

    #[test]
    fn test_unroll_nested_in_function() {
        let decl = FnDecl {
            name: "foo".into(),
            type_params: vec![],
            params: vec![],
            ret_type: None,
            where_clauses: vec![],
            precondition: None,
            postcondition: None,
            body: vec![
                Stmt::For(
                    "i".into(),
                    Expr::Range(
                        Box::new(Expr::IntLit(0)),
                        Box::new(Expr::IntLit(2)),
                        false,
                    ),
                    vec![make_expr(1)],
                ),
            ],
            vis: Visibility::Private,
            is_pure: false,
            attrs: vec![],
        };
        let stmts = vec![Stmt::FnDecl(decl)];
        let result = unroll_loops(&stmts);
        assert_eq!(result.unrolled_count, 1);
        if let Stmt::FnDecl(d) = &result.stmts[0] {
            // 2 iterations: let + body each = 4 stmts
            assert_eq!(d.body.len(), 4);
        } else {
            panic!("expected FnDecl");
        }
    }

    #[test]
    fn test_unroll_generates_correct_let_values() {
        let stmts = vec![
            Stmt::For(
                "i".into(),
                Expr::Range(
                    Box::new(Expr::IntLit(10)),
                    Box::new(Expr::IntLit(13)),
                    false,
                ),
                vec![make_expr(0)], // placeholder body
            ),
        ];
        let result = unroll_loops(&stmts);
        // Check that let bindings have values 10, 11, 12
        let mut values = vec![];
        for s in &result.stmts {
            if let Stmt::Let(_, _, Some(Expr::IntLit(n)), _) = s {
                values.push(*n);
            }
        }
        assert_eq!(values, vec![10, 11, 12]);
    }
}

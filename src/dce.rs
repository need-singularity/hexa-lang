//! Dead Code Elimination (DCE) for HEXA-LANG AST.
//!
//! Walks the AST and marks/removes unreachable code:
//! - Statements after an unconditional `return` in a block
//! - `if false { ... }` branches (when condition is a literal)
//! - `if true { ... } else { ... }` — the else branch is dead
//! - `while false { ... }` — the entire loop is dead
//!
//! This pass is separate from the bytecode-level DCE in compiler.rs.
//! It operates at the AST level, which means the compiler and
//! interpreter both benefit from smaller AST trees.

#![allow(dead_code)]

use crate::ast::*;

/// Result of dead code elimination: the optimized statements plus
/// a count of how many nodes were eliminated.
#[derive(Debug)]
pub struct DceResult {
    pub stmts: Vec<Stmt>,
    pub eliminated: usize,
}

/// Run dead code elimination on a list of statements.
pub fn eliminate_dead_code(stmts: &[Stmt]) -> DceResult {
    let mut ctx = DceCtx { eliminated: 0 };
    let optimized = ctx.optimize_block(stmts);
    DceResult {
        stmts: optimized,
        eliminated: ctx.eliminated,
    }
}

struct DceCtx {
    eliminated: usize,
}

impl DceCtx {
    /// Optimize a block of statements, removing dead code.
    fn optimize_block(&mut self, stmts: &[Stmt]) -> Vec<Stmt> {
        let mut result = Vec::new();
        let mut reached_terminator = false;

        for stmt in stmts {
            if reached_terminator {
                // Everything after a return/throw is dead
                self.eliminated += 1;
                continue;
            }

            let optimized = self.optimize_stmt(stmt);

            // Check if this is a no-op that can be skipped entirely
            if self.is_noop(&optimized) {
                self.eliminated += 1;
                continue;
            }

            // Check if this statement is a terminator
            if self.is_terminator(&optimized) {
                reached_terminator = true;
            }

            result.push(optimized);
        }

        result
    }

    /// Optimize a single statement.
    fn optimize_stmt(&mut self, stmt: &Stmt) -> Stmt {
        match stmt {
            // Recurse into function bodies
            Stmt::FnDecl(decl) => {
                let optimized_body = self.optimize_block(&decl.body);
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

            Stmt::AsyncFnDecl(decl) => {
                let optimized_body = self.optimize_block(&decl.body);
                Stmt::AsyncFnDecl(FnDecl {
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

            // Optimize for-loop body
            Stmt::For(var, iter_expr, body) => {
                let optimized_body = self.optimize_block(body);
                Stmt::For(var.clone(), iter_expr.clone(), optimized_body)
            }

            // Dead while loop: `while false { ... }` -> eliminate
            Stmt::While(cond, body) => {
                if let Expr::BoolLit(false) = cond {
                    self.eliminated += 1;
                    // Return a no-op expression
                    Stmt::Expr(Expr::BoolLit(false))
                } else {
                    let optimized_body = self.optimize_block(body);
                    Stmt::While(cond.clone(), optimized_body)
                }
            }

            // Optimize loop body
            Stmt::Loop(body) => {
                let optimized_body = self.optimize_block(body);
                Stmt::Loop(optimized_body)
            }

            // Optimize scope body
            Stmt::Scope(body) => {
                let optimized_body = self.optimize_block(body);
                Stmt::Scope(optimized_body)
            }

            // Optimize try-catch bodies
            Stmt::TryCatch(try_body, var, catch_body) => {
                let opt_try = self.optimize_block(try_body);
                let opt_catch = self.optimize_block(catch_body);
                Stmt::TryCatch(opt_try, var.clone(), opt_catch)
            }

            // Optimize spawn bodies
            Stmt::Spawn(body) => {
                let opt_body = self.optimize_block(body);
                Stmt::Spawn(opt_body)
            }

            Stmt::SpawnNamed(name, body) => {
                let opt_body = self.optimize_block(body);
                Stmt::SpawnNamed(name.clone(), opt_body)
            }

            // Optimize if-expressions at the statement level
            Stmt::Expr(Expr::If(cond, then_block, else_block)) => {
                match cond.as_ref() {
                    // `if true { ... } else { ... }` -> just the then block
                    Expr::BoolLit(true) => {
                        if let Some(else_b) = else_block {
                            self.eliminated += else_b.len();
                        }
                        let optimized_then = self.optimize_block(then_block);
                        // Wrap in a block expression
                        Stmt::Expr(Expr::Block(optimized_then))
                    }
                    // `if false { ... } else { ... }` -> just the else block
                    Expr::BoolLit(false) => {
                        self.eliminated += then_block.len();
                        if let Some(else_b) = else_block {
                            let optimized_else = self.optimize_block(else_b);
                            Stmt::Expr(Expr::Block(optimized_else))
                        } else {
                            self.eliminated += 1;
                            Stmt::Expr(Expr::BoolLit(false))
                        }
                    }
                    _ => {
                        let opt_then = self.optimize_block(then_block);
                        let opt_else = else_block.as_ref().map(|e| self.optimize_block(e));
                        Stmt::Expr(Expr::If(cond.clone(), opt_then, opt_else))
                    }
                }
            }

            // Everything else passes through
            other => other.clone(),
        }
    }

    /// Check if a statement is a no-op that can be removed.
    fn is_noop(&self, stmt: &Stmt) -> bool {
        match stmt {
            // A lone false literal as a statement (our dead-code marker)
            Stmt::Expr(Expr::BoolLit(false)) => false, // keep, it's not truly a no-op
            Stmt::Expr(Expr::Block(stmts)) if stmts.is_empty() => true,
            _ => false,
        }
    }

    /// Check if a statement is a terminator (return, throw).
    fn is_terminator(&self, stmt: &Stmt) -> bool {
        matches!(stmt, Stmt::Return(_) | Stmt::Throw(_))
    }
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    fn make_return(val: i64) -> Stmt {
        Stmt::Return(Some(Expr::IntLit(val)))
    }

    fn make_expr(val: i64) -> Stmt {
        Stmt::Expr(Expr::IntLit(val))
    }

    #[test]
    fn test_remove_after_return() {
        let stmts = vec![
            make_expr(1),
            make_return(42),
            make_expr(2), // dead
            make_expr(3), // dead
        ];
        let result = eliminate_dead_code(&stmts);
        assert_eq!(result.stmts.len(), 2);
        assert_eq!(result.eliminated, 2);
    }

    #[test]
    fn test_if_true_eliminates_else() {
        let stmts = vec![
            Stmt::Expr(Expr::If(
                Box::new(Expr::BoolLit(true)),
                vec![make_expr(1)],
                Some(vec![make_expr(2), make_expr(3)]),
            )),
        ];
        let result = eliminate_dead_code(&stmts);
        assert_eq!(result.stmts.len(), 1);
        // The else branch (2 stmts) should be eliminated
        assert!(result.eliminated >= 2);
    }

    #[test]
    fn test_if_false_eliminates_then() {
        let stmts = vec![
            Stmt::Expr(Expr::If(
                Box::new(Expr::BoolLit(false)),
                vec![make_expr(1), make_expr(2)],
                Some(vec![make_expr(3)]),
            )),
        ];
        let result = eliminate_dead_code(&stmts);
        assert_eq!(result.stmts.len(), 1);
        // The then branch (2 stmts) should be eliminated
        assert!(result.eliminated >= 2);
    }

    #[test]
    fn test_while_false_eliminated() {
        let stmts = vec![
            Stmt::While(Expr::BoolLit(false), vec![make_expr(1), make_expr(2)]),
        ];
        let result = eliminate_dead_code(&stmts);
        // while false becomes a no-op expression
        assert_eq!(result.stmts.len(), 1);
        assert!(result.eliminated >= 1);
    }

    #[test]
    fn test_function_body_optimized() {
        let decl = FnDecl {
            name: "foo".into(),
            type_params: vec![],
            params: vec![],
            ret_type: None,
            where_clauses: vec![],
            precondition: None,
            postcondition: None,
            body: vec![
                make_return(42),
                make_expr(99), // dead
            ],
            vis: Visibility::Private,
            is_pure: false,
            attrs: vec![],
        };
        let stmts = vec![Stmt::FnDecl(decl)];
        let result = eliminate_dead_code(&stmts);
        if let Stmt::FnDecl(d) = &result.stmts[0] {
            assert_eq!(d.body.len(), 1); // only the return remains
        } else {
            panic!("expected FnDecl");
        }
        assert_eq!(result.eliminated, 1);
    }

    #[test]
    fn test_no_dead_code() {
        let stmts = vec![
            make_expr(1),
            make_expr(2),
            make_expr(3),
        ];
        let result = eliminate_dead_code(&stmts);
        assert_eq!(result.stmts.len(), 3);
        assert_eq!(result.eliminated, 0);
    }

    #[test]
    fn test_nested_scope_optimized() {
        let stmts = vec![
            Stmt::Scope(vec![
                make_return(1),
                make_expr(99), // dead
            ]),
        ];
        let result = eliminate_dead_code(&stmts);
        if let Stmt::Scope(body) = &result.stmts[0] {
            assert_eq!(body.len(), 1);
        } else {
            panic!("expected Scope");
        }
    }
}

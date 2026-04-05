//! Escape analysis for HEXA-LANG JIT compiler.
//!
//! Walks the AST before JIT compilation and classifies each variable in a
//! function body as `Stack`, `Heap`, or `Unknown`.  Non-escaping values
//! (structs, arrays, enums) that stay within the declaring scope can be
//! stack-allocated, avoiding the overhead of `hexa_alloc` / `hexa_free`.
//!
//! A value "escapes" when it is:
//!   - returned from the function,
//!   - stored into a data structure (struct field, array element),
//!   - captured by a closure / lambda,
//!   - passed as an argument to a call that is not inlined.

use std::collections::{HashMap, HashSet};

use crate::ast::*;

// ── Public types ───────────────────────────────────────────────

/// Where a variable's backing storage can live.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Allocation {
    /// Safe to place on the Cranelift stack slot (no escape).
    Stack,
    /// Must be heap-allocated (escapes its scope).
    Heap,
    /// Could not be determined — fall back to heap for safety.
    Unknown,
}

/// Per-function escape information: variable name -> allocation decision.
#[derive(Debug, Clone)]
pub struct EscapeInfo {
    pub allocations: HashMap<String, Allocation>,
}

impl EscapeInfo {
    /// Look up a variable.  Returns `Heap` if not found (safe default).
    pub fn get(&self, name: &str) -> Allocation {
        self.allocations.get(name).copied().unwrap_or(Allocation::Heap)
    }
}

// ── Analysis entry point ───────────────────────────────────────

/// Run escape analysis on the body of a single function.
///
/// `params` are the function's parameter names (always treated as `Unknown`
/// because the caller might hold references).
pub fn analyze_escapes(params: &[String], body: &[Stmt]) -> EscapeInfo {
    let mut ctx = EscapeCtx::new();

    // Parameters are conservatively Unknown — we don't know how the caller
    // will use the returned value.
    for p in params {
        ctx.allocations.insert(p.clone(), Allocation::Unknown);
    }

    // Pass 1: discover all `let` bindings that create aggregate values
    // (structs, arrays, enums).  Default them to `Stack`.
    for stmt in body {
        discover_lets(stmt, &mut ctx);
    }

    // Pass 2: walk the body looking for escaping uses.  Anything that
    // escapes gets promoted to `Heap`.
    let mut escaped: HashSet<String> = HashSet::new();
    for stmt in body {
        find_escapes_stmt(stmt, &mut escaped, &mut ctx);
    }

    // Apply escape set.
    for name in &escaped {
        if let Some(alloc) = ctx.allocations.get_mut(name) {
            *alloc = Allocation::Heap;
        }
    }

    EscapeInfo {
        allocations: ctx.allocations,
    }
}

/// Convenience: run escape analysis over every top-level function in a
/// program and return a map of `fn_name -> EscapeInfo`.
pub fn analyze_program(stmts: &[Stmt]) -> HashMap<String, EscapeInfo> {
    let mut result = HashMap::new();

    for stmt in stmts {
        if let Stmt::FnDecl(decl) = stmt {
            let params: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
            let info = analyze_escapes(&params, &decl.body);
            result.insert(decl.name.clone(), info);
        }
        if let Stmt::ImplBlock(ib) = stmt {
            for method in &ib.methods {
                let params: Vec<String> = method.params.iter().map(|p| p.name.clone()).collect();
                let info = analyze_escapes(&params, &method.body);
                let mangled = format!("{}_{}", ib.target, method.name);
                result.insert(mangled, info);
            }
        }
    }

    // Top-level statements (treated as __hexa_main__).
    let top_stmts: Vec<&Stmt> = stmts
        .iter()
        .filter(|s| !matches!(s, Stmt::FnDecl(_) | Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::ImplBlock(_)))
        .collect();
    if !top_stmts.is_empty() {
        let refs: Vec<Stmt> = top_stmts.into_iter().cloned().collect();
        let info = analyze_escapes(&[], &refs);
        result.insert("__hexa_main__".to_string(), info);
    }

    result
}

// ── Internal helpers ───────────────────────────────────────────

struct EscapeCtx {
    allocations: HashMap<String, Allocation>,
    /// Track which variables hold aggregate (struct/array/enum) values.
    aggregates: HashSet<String>,
}

impl EscapeCtx {
    fn new() -> Self {
        Self {
            allocations: HashMap::new(),
            aggregates: HashSet::new(),
        }
    }
}

/// Discover `let` bindings that create aggregate values.
fn discover_lets(stmt: &Stmt, ctx: &mut EscapeCtx) {
    match stmt {
        Stmt::Let(name, _typ, Some(expr), _vis) => {
            if is_aggregate_expr(expr) {
                ctx.allocations.insert(name.clone(), Allocation::Stack);
                ctx.aggregates.insert(name.clone());
            }
        }
        Stmt::While(_cond, body) => {
            for s in body {
                discover_lets(s, ctx);
            }
        }
        Stmt::FnDecl(decl) => {
            for s in &decl.body {
                discover_lets(s, ctx);
            }
        }
        _ => {}
    }
}

/// Is this expression creating a heap-allocatable aggregate?
fn is_aggregate_expr(expr: &Expr) -> bool {
    matches!(expr, Expr::StructInit(..) | Expr::Array(..) | Expr::EnumPath(..))
}

/// Walk a statement looking for escaping uses of aggregate variables.
fn find_escapes_stmt(stmt: &Stmt, escaped: &mut HashSet<String>, ctx: &mut EscapeCtx) {
    match stmt {
        Stmt::Return(Some(expr)) => {
            // Anything returned escapes.
            collect_escaping_names(expr, escaped, ctx);
        }
        Stmt::Let(_name, _typ, Some(expr), _vis) => {
            find_escapes_expr(expr, escaped, ctx);
        }
        Stmt::Assign(lhs, rhs) => {
            // Storing into a field/index means the RHS escapes
            // (the container outlives the current knowledge).
            if matches!(lhs, Expr::Field(..) | Expr::Index(..)) {
                collect_escaping_names(rhs, escaped, ctx);
            }
            find_escapes_expr(lhs, escaped, ctx);
            find_escapes_expr(rhs, escaped, ctx);
        }
        Stmt::Expr(expr) => {
            find_escapes_expr(expr, escaped, ctx);
        }
        Stmt::While(cond, body) => {
            find_escapes_expr(cond, escaped, ctx);
            for s in body {
                find_escapes_stmt(s, escaped, ctx);
            }
        }
        _ => {}
    }
}

/// Walk an expression looking for escaping uses.
fn find_escapes_expr(expr: &Expr, escaped: &mut HashSet<String>, ctx: &mut EscapeCtx) {
    match expr {
        Expr::Call(_callee, args) => {
            // Arguments passed to calls escape (conservative).
            for arg in args {
                collect_escaping_names(arg, escaped, ctx);
            }
        }
        Expr::Lambda(params, body) => {
            // Free variables captured by a lambda escape.
            let param_names: Vec<String> = params.iter().map(|p| p.name.clone()).collect();
            let free = crate::jit::collect_free_vars_for_escape(body, &param_names);
            for name in free {
                if ctx.aggregates.contains(&name) {
                    escaped.insert(name);
                }
            }
        }
        Expr::StructInit(_, fields) => {
            // Values stored into a struct field escape.
            for (_, fexpr) in fields {
                collect_escaping_names(fexpr, escaped, ctx);
            }
        }
        Expr::Array(items) => {
            for item in items {
                collect_escaping_names(item, escaped, ctx);
            }
        }
        Expr::Binary(l, _, r) => {
            find_escapes_expr(l, escaped, ctx);
            find_escapes_expr(r, escaped, ctx);
        }
        Expr::Unary(_, e) => find_escapes_expr(e, escaped, ctx),
        Expr::If(cond, then_b, else_b) => {
            find_escapes_expr(cond, escaped, ctx);
            for s in then_b {
                find_escapes_stmt(s, escaped, ctx);
            }
            if let Some(eb) = else_b {
                for s in eb {
                    find_escapes_stmt(s, escaped, ctx);
                }
            }
        }
        Expr::Block(stmts) => {
            for s in stmts {
                find_escapes_stmt(s, escaped, ctx);
            }
        }
        Expr::Field(obj, _) => find_escapes_expr(obj, escaped, ctx),
        Expr::Index(a, b) => {
            find_escapes_expr(a, escaped, ctx);
            find_escapes_expr(b, escaped, ctx);
        }
        Expr::Match(scrut, arms) => {
            find_escapes_expr(scrut, escaped, ctx);
            for arm in arms {
                for s in &arm.body {
                    find_escapes_stmt(s, escaped, ctx);
                }
            }
        }
        _ => {}
    }
}

/// Collect names of aggregate variables that appear in an expression.
/// These names are considered to escape their scope.
fn collect_escaping_names(expr: &Expr, escaped: &mut HashSet<String>, ctx: &EscapeCtx) {
    match expr {
        Expr::Ident(name) => {
            if ctx.aggregates.contains(name) {
                escaped.insert(name.clone());
            }
        }
        Expr::Binary(l, _, r) => {
            collect_escaping_names(l, escaped, ctx);
            collect_escaping_names(r, escaped, ctx);
        }
        Expr::Unary(_, e) => collect_escaping_names(e, escaped, ctx),
        Expr::Call(callee, args) => {
            collect_escaping_names(callee, escaped, ctx);
            for a in args {
                collect_escaping_names(a, escaped, ctx);
            }
        }
        Expr::Field(obj, _) => collect_escaping_names(obj, escaped, ctx),
        Expr::Index(a, b) => {
            collect_escaping_names(a, escaped, ctx);
            collect_escaping_names(b, escaped, ctx);
        }
        Expr::StructInit(_, fields) => {
            for (_, e) in fields {
                collect_escaping_names(e, escaped, ctx);
            }
        }
        Expr::Array(items) => {
            for item in items {
                collect_escaping_names(item, escaped, ctx);
            }
        }
        Expr::If(c, then_b, else_b) => {
            collect_escaping_names(c, escaped, ctx);
            for s in then_b {
                find_escapes_stmt(s, escaped, &mut EscapeCtx {
                    allocations: ctx.allocations.clone(),
                    aggregates: ctx.aggregates.clone(),
                });
            }
            if let Some(eb) = else_b {
                for s in eb {
                    find_escapes_stmt(s, escaped, &mut EscapeCtx {
                        allocations: ctx.allocations.clone(),
                        aggregates: ctx.aggregates.clone(),
                    });
                }
            }
        }
        _ => {}
    }
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    #[allow(unused_imports)]
    use crate::ast::*;

    fn make_let_struct(name: &str, struct_name: &str) -> Stmt {
        Stmt::Let(
            name.to_string(),
            None,
            Some(Expr::StructInit(struct_name.to_string(), vec![])),
            Visibility::Private,
        )
    }

    fn make_let_array(name: &str) -> Stmt {
        Stmt::Let(
            name.to_string(),
            None,
            Some(Expr::Array(vec![Expr::IntLit(1), Expr::IntLit(2)])),
            Visibility::Private,
        )
    }

    fn make_return(name: &str) -> Stmt {
        Stmt::Return(Some(Expr::Ident(name.to_string())))
    }

    #[test]
    fn test_non_escaping_struct_is_stack() {
        // let p = Point {}
        // p.x + 1
        let body = vec![
            make_let_struct("p", "Point"),
            Stmt::Expr(Expr::Binary(
                Box::new(Expr::Field(Box::new(Expr::Ident("p".into())), "x".into())),
                BinOp::Add,
                Box::new(Expr::IntLit(1)),
            )),
        ];
        let info = analyze_escapes(&[], &body);
        assert_eq!(info.get("p"), Allocation::Stack);
    }

    #[test]
    fn test_returned_struct_is_heap() {
        // let p = Point {}
        // return p
        let body = vec![
            make_let_struct("p", "Point"),
            make_return("p"),
        ];
        let info = analyze_escapes(&[], &body);
        assert_eq!(info.get("p"), Allocation::Heap);
    }

    #[test]
    fn test_array_passed_to_call_escapes() {
        // let a = [1, 2]
        // foo(a)
        let body = vec![
            make_let_array("a"),
            Stmt::Expr(Expr::Call(
                Box::new(Expr::Ident("foo".into())),
                vec![Expr::Ident("a".into())],
            )),
        ];
        let info = analyze_escapes(&[], &body);
        assert_eq!(info.get("a"), Allocation::Heap);
    }

    #[test]
    fn test_non_escaping_array_is_stack() {
        // let a = [1, 2]
        // a[0] + a[1]
        let body = vec![
            make_let_array("a"),
            Stmt::Expr(Expr::Binary(
                Box::new(Expr::Index(
                    Box::new(Expr::Ident("a".into())),
                    Box::new(Expr::IntLit(0)),
                )),
                BinOp::Add,
                Box::new(Expr::Index(
                    Box::new(Expr::Ident("a".into())),
                    Box::new(Expr::IntLit(1)),
                )),
            )),
        ];
        let info = analyze_escapes(&[], &body);
        assert_eq!(info.get("a"), Allocation::Stack);
    }

    #[test]
    fn test_unknown_var_defaults_heap() {
        let info = analyze_escapes(&[], &[]);
        assert_eq!(info.get("nonexistent"), Allocation::Heap);
    }

    #[test]
    fn test_stored_into_field_escapes() {
        // let p = Point {}
        // let q = Point {}
        // q.inner = p       <-- p escapes
        let body = vec![
            make_let_struct("p", "Point"),
            make_let_struct("q", "Point"),
            Stmt::Assign(
                Expr::Field(Box::new(Expr::Ident("q".into())), "inner".into()),
                Expr::Ident("p".into()),
            ),
        ];
        let info = analyze_escapes(&[], &body);
        assert_eq!(info.get("p"), Allocation::Heap);
        // q itself is not returned or passed, so stays Stack.
        assert_eq!(info.get("q"), Allocation::Stack);
    }

    #[test]
    fn test_analyze_program() {
        let stmts = vec![
            Stmt::FnDecl(FnDecl {
                name: "make_point".into(),
                type_params: vec![],
                params: vec![],
                ret_type: None,
                where_clauses: vec![],
                precondition: None,
                postcondition: None,
                body: vec![
                    make_let_struct("p", "Point"),
                    make_return("p"),
                ],
                vis: Visibility::Private,
                is_pure: false,
            }),
        ];
        let map = analyze_program(&stmts);
        let info = map.get("make_point").unwrap();
        assert_eq!(info.get("p"), Allocation::Heap);
    }
}

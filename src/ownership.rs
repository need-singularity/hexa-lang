// ⛔ CORE — L0 불변식 (소유권 시스템. 수정 전 유저 승인 필수)
//! Compile-time ownership analysis for Hexa.
//!
//! Walks the AST before interpretation to detect ownership violations
//! at compile time rather than at runtime:
//!   - Use after move
//!   - Use after drop
//!   - Move while borrowed
//!   - Double drop
//!   - Borrow outliving owner scope

#![allow(dead_code)]

use std::collections::HashMap;
use crate::ast::*;
use crate::error::{HexaError, ErrorClass};

/// Ownership state of a variable during static analysis.
#[derive(Debug, Clone, PartialEq)]
enum OwnerState {
    /// Created with `own` — fully owned.
    Owned,
    /// Ownership transferred away via `move`.
    Moved,
    /// Currently borrowed N times (borrow count).
    Borrowed(usize),
    /// Explicitly dropped via `drop`.
    Dropped,
}

/// An error detected during compile-time ownership analysis.
#[derive(Debug, Clone)]
pub struct OwnershipError {
    pub message: String,
    pub line: usize,
    pub col: usize,
    pub hint: Option<String>,
}

impl OwnershipError {
    fn new(line: usize, col: usize, message: String) -> Self {
        Self { message, line, col, hint: None }
    }

    fn with_hint(mut self, hint: String) -> Self {
        self.hint = Some(hint);
        self
    }

    /// Convert to HexaError for uniform error reporting.
    pub fn into_hexa_error(self) -> HexaError {
        HexaError {
            class: ErrorClass::Type,
            message: self.message,
            line: self.line,
            col: self.col,
            hint: self.hint,
        }
    }
}

/// Tracks which variables have active borrows, and from which scope depth.
#[derive(Debug, Clone)]
struct BorrowInfo {
    /// The variable being borrowed.
    source: String,
    /// Scope depth at which the borrow was created.
    scope_depth: usize,
}

/// A single scope frame for ownership tracking.
#[derive(Debug, Clone)]
struct ScopeFrame {
    /// Variable name -> ownership state.
    vars: HashMap<String, OwnerState>,
    /// Variables that hold borrows: borrow_var -> source variable name.
    borrows: Vec<BorrowInfo>,
}

impl ScopeFrame {
    fn new() -> Self {
        Self {
            vars: HashMap::new(),
            borrows: Vec::new(),
        }
    }
}

/// The ownership analyzer: walks AST statements and tracks ownership state.
pub struct OwnershipAnalyzer {
    scopes: Vec<ScopeFrame>,
    /// Known function declarations (name -> params with borrow annotations).
    fn_decls: HashMap<String, Vec<ParamOwnership>>,
    errors: Vec<OwnershipError>,
}

/// Ownership annotation on a function parameter.
#[derive(Debug, Clone)]
enum ParamOwnership {
    /// Default: parameter takes ownership.
    Owned,
    /// Parameter is borrowed (read-only reference).
    Borrowed,
}

impl OwnershipAnalyzer {
    pub fn new() -> Self {
        Self {
            scopes: vec![ScopeFrame::new()],
            fn_decls: HashMap::new(),
            errors: Vec::new(),
        }
    }

    /// Run ownership analysis on the AST.
    /// Returns a list of ownership errors (empty = all clear).
    pub fn analyze(&mut self, stmts: &[Stmt], spans: &[(usize, usize)]) -> Vec<OwnershipError> {
        // First pass: collect function declarations.
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                self.register_fn(decl);
            }
            if let Stmt::AsyncFnDecl(decl) = stmt {
                self.register_fn(decl);
            }
        }
        // Second pass: analyze statements.
        for (i, stmt) in stmts.iter().enumerate() {
            let (line, col) = spans.get(i).copied().unwrap_or((0, 0));
            self.analyze_stmt(stmt, line, col);
        }
        self.errors.clone()
    }

    // ── Scope management ────────────────────────────────────

    fn push_scope(&mut self) {
        self.scopes.push(ScopeFrame::new());
    }

    fn pop_scope(&mut self, line: usize, col: usize) {
        if let Some(frame) = self.scopes.last() {
            // Check for borrows that were created in this scope and reference
            // variables from outer scopes. The borrows expire when this scope ends.
            // We need to decrement borrow counts on source variables.
            let borrows_to_release: Vec<String> = frame.borrows
                .iter()
                .map(|b| b.source.clone())
                .collect();

            // Check for owned values that are still alive in this scope.
            // This is a warning (not error) — values going out of scope are dropped.
            for (name, state) in &frame.vars {
                if let OwnerState::Borrowed(n) = state {
                    if *n > 0 {
                        self.errors.push(
                            OwnershipError::new(line, col,
                                format!("variable `{}` still has {} active borrow(s) when scope ends", name, n))
                                .with_hint("borrows must be returned before the owner goes out of scope".to_string())
                        );
                    }
                }
            }

            // Release borrows from this scope.
            for source in &borrows_to_release {
                self.release_borrow(source);
            }
        }
        self.scopes.pop();
    }

    fn scope_depth(&self) -> usize {
        self.scopes.len()
    }

    /// Define a variable with an ownership state in the current scope.
    fn define_var(&mut self, name: &str, state: OwnerState) {
        if let Some(frame) = self.scopes.last_mut() {
            frame.vars.insert(name.to_string(), state);
        }
    }

    /// Look up the ownership state of a variable across scopes.
    fn lookup_state(&self, name: &str) -> Option<&OwnerState> {
        for frame in self.scopes.iter().rev() {
            if let Some(state) = frame.vars.get(name) {
                return Some(state);
            }
        }
        None
    }

    /// Set the ownership state of an existing variable (searches all scopes).
    fn set_state(&mut self, name: &str, state: OwnerState) {
        for frame in self.scopes.iter_mut().rev() {
            if frame.vars.contains_key(name) {
                frame.vars.insert(name.to_string(), state);
                return;
            }
        }
    }

    /// Check if a variable is currently tracked (has any ownership state).
    fn is_tracked(&self, name: &str) -> bool {
        self.lookup_state(name).is_some()
    }

    /// Increment borrow count on a source variable.
    fn add_borrow(&mut self, source: &str) {
        for frame in self.scopes.iter_mut().rev() {
            if let Some(state) = frame.vars.get_mut(source) {
                match state {
                    OwnerState::Owned => {
                        *state = OwnerState::Borrowed(1);
                    }
                    OwnerState::Borrowed(n) => {
                        *n += 1;
                    }
                    _ => {}
                }
                return;
            }
        }
    }

    /// Record a borrow in the current scope frame.
    fn record_borrow(&mut self, source: &str) {
        let depth = self.scope_depth();
        if let Some(frame) = self.scopes.last_mut() {
            frame.borrows.push(BorrowInfo {
                source: source.to_string(),
                scope_depth: depth,
            });
        }
    }

    /// Decrement borrow count on a source variable.
    fn release_borrow(&mut self, source: &str) {
        for frame in self.scopes.iter_mut().rev() {
            if let Some(state) = frame.vars.get_mut(source) {
                match state {
                    OwnerState::Borrowed(n) if *n > 1 => {
                        *n -= 1;
                    }
                    OwnerState::Borrowed(1) => {
                        *state = OwnerState::Owned;
                    }
                    _ => {}
                }
                return;
            }
        }
    }

    /// Register a function declaration to track parameter ownership.
    fn register_fn(&mut self, decl: &FnDecl) {
        let params: Vec<ParamOwnership> = decl.params.iter().map(|_p| {
            // In the future, we could check for `borrow` annotation in param type.
            // For now, all params are owned by default.
            ParamOwnership::Owned
        }).collect();
        self.fn_decls.insert(decl.name.clone(), params);
    }

    // ── Statement analysis ──────────────────────────────────

    fn analyze_stmt(&mut self, stmt: &Stmt, line: usize, col: usize) {
        match stmt {
            Stmt::Let(name, _typ, expr, _vis) => {
                if let Some(e) = expr {
                    // Check if the initializer uses ownership constructs.
                    match e {
                        Expr::Own(inner) => {
                            self.check_expr_use(inner, line, col);
                            self.define_var(name, OwnerState::Owned);
                        }
                        Expr::MoveExpr(inner) => {
                            self.analyze_move(inner, name, line, col);
                        }
                        Expr::Borrow(inner) => {
                            self.analyze_borrow(inner, name, line, col);
                        }
                        _ => {
                            self.check_expr_use(e, line, col);
                            // Normal let binding: not tracked for ownership
                            // unless it references a tracked variable.
                        }
                    }
                }
            }
            Stmt::Assign(lhs, rhs) => {
                // Check that the LHS isn't moved/dropped, and the RHS is valid.
                if let Expr::Ident(name) = lhs {
                    if let Some(state) = self.lookup_state(name) {
                        match state {
                            OwnerState::Moved => {
                                self.errors.push(
                                    OwnershipError::new(line, col,
                                        format!("cannot assign to `{}`: value has been moved", name))
                                        .with_hint("consider creating a new binding instead".to_string())
                                );
                            }
                            OwnerState::Dropped => {
                                self.errors.push(
                                    OwnershipError::new(line, col,
                                        format!("cannot assign to `{}`: value has been dropped", name))
                                );
                            }
                            OwnerState::Borrowed(_) => {
                                self.errors.push(
                                    OwnershipError::new(line, col,
                                        format!("cannot mutate `{}`: value is borrowed (read-only)", name))
                                        .with_hint("wait for all borrows to be released before mutating".to_string())
                                );
                            }
                            _ => {}
                        }
                    }
                }
                self.check_expr_use(rhs, line, col);
            }
            Stmt::Expr(expr) => {
                self.analyze_expr(expr, line, col);
            }
            Stmt::Return(expr) => {
                if let Some(e) = expr {
                    self.analyze_expr(e, line, col);
                }
            }
            Stmt::DropStmt(name) => {
                self.analyze_drop(name, line, col);
            }
            Stmt::FnDecl(decl) => {
                self.analyze_fn_body(decl, line, col);
            }
            Stmt::AsyncFnDecl(decl) => {
                self.analyze_fn_body(decl, line, col);
            }
            Stmt::For(_var, iter_expr, body) => {
                self.check_expr_use(iter_expr, line, col);
                self.push_scope();
                // Loop variable is not ownership-tracked.
                for s in body {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
            }
            Stmt::While(cond, body) => {
                self.check_expr_use(cond, line, col);
                self.push_scope();
                for s in body {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
            }
            Stmt::Loop(body) => {
                self.push_scope();
                for s in body {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
            }
            Stmt::TryCatch(try_block, _err_name, catch_block) => {
                self.push_scope();
                for s in try_block {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
                self.push_scope();
                for s in catch_block {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
            }
            Stmt::Spawn(body) | Stmt::SpawnNamed(_, body) => {
                self.push_scope();
                for s in body {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
            }
            Stmt::Proof(_, body) => {
                self.push_scope();
                for s in body {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
            }
            Stmt::Throw(expr) => {
                self.check_expr_use(expr, line, col);
            }
            Stmt::Assert(expr) | Stmt::ContractAssert(_, _, expr) => {
                self.check_expr_use(expr, line, col);
            }
            // Declarations that don't affect ownership.
            Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::TraitDecl(_)
            | Stmt::ImplBlock(_) | Stmt::Intent(_, _) | Stmt::Verify(_, _)
            | Stmt::Mod(_, _) | Stmt::Use(_) | Stmt::Select(_, _)
            | Stmt::Generate(_) | Stmt::Optimize(_) | Stmt::Const(..)
            | Stmt::Static(..) | Stmt::MacroDef(_) | Stmt::DeriveDecl(..)
            | Stmt::ComptimeFn(_) | Stmt::EffectDecl(_) | Stmt::ConsciousnessBlock(_, _) | Stmt::EvolveFn(_)
            | Stmt::Scope(_) | Stmt::ProofBlock(..)
            | Stmt::TypeAlias(..) | Stmt::AtomicLet(..) | Stmt::Panic(..) | Stmt::Theorem(..) | Stmt::Break | Stmt::Continue | Stmt::Extern(_) | Stmt::LetTuple(..) => {}
        }
    }

    /// Analyze a `move` expression: `let y = move x`.
    fn analyze_move(&mut self, inner: &Expr, target: &str, line: usize, col: usize) {
        if let Expr::Ident(source) = inner {
            if let Some(state) = self.lookup_state(source).cloned() {
                match state {
                    OwnerState::Moved => {
                        self.errors.push(
                            OwnershipError::new(line, col,
                                format!("cannot use `{}` after move", source))
                                .with_hint(format!("value was previously moved out of `{}`", source))
                        );
                        return;
                    }
                    OwnerState::Dropped => {
                        self.errors.push(
                            OwnershipError::new(line, col,
                                format!("cannot use `{}` after drop", source))
                                .with_hint(format!("`{}` was explicitly dropped", source))
                        );
                        return;
                    }
                    OwnerState::Borrowed(n) if n > 0 => {
                        self.errors.push(
                            OwnershipError::new(line, col,
                                format!("cannot move `{}` while borrowed", source))
                                .with_hint(format!("`{}` has {} active borrow(s)", source, n))
                        );
                        return;
                    }
                    _ => {
                        // Valid move: source becomes Moved, target becomes Owned.
                        self.set_state(source, OwnerState::Moved);
                        self.define_var(target, OwnerState::Owned);
                    }
                }
            } else {
                // Moving a non-tracked variable — just define the target as owned.
                self.define_var(target, OwnerState::Owned);
            }
        } else {
            // Moving a complex expression — just check usage and define target.
            self.check_expr_use(inner, line, col);
            self.define_var(target, OwnerState::Owned);
        }
    }

    /// Analyze a `borrow` expression: `let y = borrow x`.
    fn analyze_borrow(&mut self, inner: &Expr, _target: &str, line: usize, col: usize) {
        if let Expr::Ident(source) = inner {
            if let Some(state) = self.lookup_state(source).cloned() {
                match state {
                    OwnerState::Moved => {
                        self.errors.push(
                            OwnershipError::new(line, col,
                                format!("cannot borrow `{}`: value has been moved", source))
                        );
                        return;
                    }
                    OwnerState::Dropped => {
                        self.errors.push(
                            OwnershipError::new(line, col,
                                format!("cannot borrow `{}`: value has been dropped", source))
                        );
                        return;
                    }
                    _ => {
                        // Valid borrow: increment borrow count on source.
                        self.add_borrow(source);
                        self.record_borrow(source);
                        // Target variable is not "owned" — it's a borrow reference.
                        // We don't track the target as owned; it will expire with scope.
                    }
                }
            }
            // Non-tracked variable borrowing is fine — no ownership semantics.
        } else {
            self.check_expr_use(inner, line, col);
        }
    }

    /// Analyze a `drop` statement.
    fn analyze_drop(&mut self, name: &str, line: usize, col: usize) {
        if let Some(state) = self.lookup_state(name).cloned() {
            match state {
                OwnerState::Moved => {
                    self.errors.push(
                        OwnershipError::new(line, col,
                            format!("cannot drop `{}`: value has been moved", name))
                            .with_hint(format!("`{}` was previously moved", name))
                    );
                }
                OwnerState::Dropped => {
                    self.errors.push(
                        OwnershipError::new(line, col,
                            format!("cannot drop `{}` -- already dropped", name))
                            .with_hint("each value can only be dropped once".to_string())
                    );
                }
                OwnerState::Borrowed(n) if n > 0 => {
                    self.errors.push(
                        OwnershipError::new(line, col,
                            format!("cannot drop `{}` while it has {} active borrow(s)", name, n))
                            .with_hint("release all borrows before dropping".to_string())
                    );
                }
                _ => {
                    self.set_state(name, OwnerState::Dropped);
                }
            }
        } else {
            // Dropping a non-tracked variable: that's fine, just mark it dropped
            // so future use-after-drop is caught.
            self.define_var(name, OwnerState::Dropped);
        }
    }

    /// Analyze a function body.
    fn analyze_fn_body(&mut self, decl: &FnDecl, line: usize, col: usize) {
        self.push_scope();
        // Function parameters are owned by default.
        for param in &decl.params {
            // Parameters are not tracked unless annotated with `own`.
            // We could extend this to track borrowed params.
            let _ = param;
        }
        for s in &decl.body {
            self.analyze_stmt(s, line, col);
        }
        self.pop_scope(line, col);
    }

    /// Analyze an expression that may contain ownership operations.
    fn analyze_expr(&mut self, expr: &Expr, line: usize, col: usize) {
        match expr {
            Expr::Own(inner) => {
                self.check_expr_use(inner, line, col);
            }
            Expr::MoveExpr(inner) => {
                // Standalone move expression (not in a let binding).
                if let Expr::Ident(source) = inner.as_ref() {
                    if let Some(state) = self.lookup_state(source).cloned() {
                        match state {
                            OwnerState::Moved => {
                                self.errors.push(
                                    OwnershipError::new(line, col,
                                        format!("cannot use `{}` after move", source))
                                );
                            }
                            OwnerState::Dropped => {
                                self.errors.push(
                                    OwnershipError::new(line, col,
                                        format!("cannot use `{}` after drop", source))
                                );
                            }
                            OwnerState::Borrowed(n) if n > 0 => {
                                self.errors.push(
                                    OwnershipError::new(line, col,
                                        format!("cannot move `{}` while borrowed", source))
                                );
                            }
                            _ => {
                                self.set_state(source, OwnerState::Moved);
                            }
                        }
                    }
                } else {
                    self.check_expr_use(inner, line, col);
                }
            }
            Expr::Borrow(inner) => {
                if let Expr::Ident(source) = inner.as_ref() {
                    if let Some(state) = self.lookup_state(source).cloned() {
                        match state {
                            OwnerState::Moved => {
                                self.errors.push(
                                    OwnershipError::new(line, col,
                                        format!("cannot borrow `{}`: value has been moved", source))
                                );
                            }
                            OwnerState::Dropped => {
                                self.errors.push(
                                    OwnershipError::new(line, col,
                                        format!("cannot borrow `{}`: value has been dropped", source))
                                );
                            }
                            _ => {
                                self.add_borrow(source);
                                self.record_borrow(source);
                            }
                        }
                    }
                } else {
                    self.check_expr_use(inner, line, col);
                }
            }
            Expr::Call(callee, args) => {
                self.check_expr_use(callee, line, col);
                for arg in args {
                    self.analyze_expr(arg, line, col);
                }
            }
            Expr::Binary(left, _, right) => {
                self.check_expr_use(left, line, col);
                self.check_expr_use(right, line, col);
            }
            Expr::Unary(_, operand) => {
                self.check_expr_use(operand, line, col);
            }
            Expr::If(cond, then_block, else_block) => {
                self.check_expr_use(cond, line, col);
                self.push_scope();
                for s in then_block {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
                if let Some(eb) = else_block {
                    self.push_scope();
                    for s in eb {
                        self.analyze_stmt(s, line, col);
                    }
                    self.pop_scope(line, col);
                }
            }
            Expr::Block(stmts) => {
                self.push_scope();
                for s in stmts {
                    self.analyze_stmt(s, line, col);
                }
                self.pop_scope(line, col);
            }
            Expr::Array(items) => {
                for item in items {
                    self.check_expr_use(item, line, col);
                }
            }
            Expr::Lambda(_, body) => {
                self.push_scope();
                self.check_expr_use(body, line, col);
                self.pop_scope(line, col);
            }
            Expr::Index(arr, idx) => {
                self.check_expr_use(arr, line, col);
                self.check_expr_use(idx, line, col);
            }
            Expr::Field(obj, _) => {
                self.check_expr_use(obj, line, col);
            }
            Expr::Match(scrutinee, arms) => {
                self.check_expr_use(scrutinee, line, col);
                for arm in arms {
                    self.push_scope();
                    for s in &arm.body {
                        self.analyze_stmt(s, line, col);
                    }
                    self.pop_scope(line, col);
                }
            }
            _ => {
                // Literals, identifiers (checked via check_expr_use when needed),
                // and other expressions that don't affect ownership.
                self.check_expr_use(expr, line, col);
            }
        }
    }

    /// Check that an expression doesn't reference a moved or dropped variable.
    fn check_expr_use(&mut self, expr: &Expr, line: usize, col: usize) {
        match expr {
            Expr::Ident(name) => {
                if let Some(state) = self.lookup_state(name) {
                    match state {
                        OwnerState::Moved => {
                            self.errors.push(
                                OwnershipError::new(line, col,
                                    format!("cannot use `{}` after move", name))
                                    .with_hint(format!("value was moved out of `{}`", name))
                            );
                        }
                        OwnerState::Dropped => {
                            self.errors.push(
                                OwnershipError::new(line, col,
                                    format!("cannot use `{}` after drop", name))
                                    .with_hint(format!("`{}` was explicitly dropped", name))
                            );
                        }
                        _ => {}
                    }
                }
            }
            Expr::Binary(left, _, right) => {
                self.check_expr_use(left, line, col);
                self.check_expr_use(right, line, col);
            }
            Expr::Unary(_, operand) => {
                self.check_expr_use(operand, line, col);
            }
            Expr::Call(callee, args) => {
                self.check_expr_use(callee, line, col);
                for arg in args {
                    self.check_expr_use(arg, line, col);
                }
            }
            Expr::Field(obj, _) => {
                self.check_expr_use(obj, line, col);
            }
            Expr::Index(arr, idx) => {
                self.check_expr_use(arr, line, col);
                self.check_expr_use(idx, line, col);
            }
            Expr::Array(items) => {
                for item in items {
                    self.check_expr_use(item, line, col);
                }
            }
            Expr::Tuple(items) => {
                for item in items {
                    self.check_expr_use(item, line, col);
                }
            }
            Expr::If(cond, then_block, else_block) => {
                self.check_expr_use(cond, line, col);
                for s in then_block {
                    if let Stmt::Expr(e) = s {
                        self.check_expr_use(e, line, col);
                    }
                }
                if let Some(eb) = else_block {
                    for s in eb {
                        if let Stmt::Expr(e) = s {
                            self.check_expr_use(e, line, col);
                        }
                    }
                }
            }
            Expr::Own(inner) | Expr::MoveExpr(inner) | Expr::Borrow(inner) | Expr::Await(inner) => {
                self.check_expr_use(inner, line, col);
            }
            Expr::Block(stmts) => {
                for s in stmts {
                    if let Stmt::Expr(e) = s {
                        self.check_expr_use(e, line, col);
                    }
                }
            }
            Expr::Lambda(_, body) => {
                self.check_expr_use(body, line, col);
            }
            Expr::Range(start, end, _) => {
                self.check_expr_use(start, line, col);
                self.check_expr_use(end, line, col);
            }
            Expr::StructInit(_, fields) => {
                for (_, val) in fields {
                    self.check_expr_use(val, line, col);
                }
            }
            Expr::MapLit(pairs) => {
                for (k, v) in pairs {
                    self.check_expr_use(k, line, col);
                    self.check_expr_use(v, line, col);
                }
            }
            Expr::Match(scrutinee, arms) => {
                self.check_expr_use(scrutinee, line, col);
                for arm in arms {
                    for s in &arm.body {
                        if let Stmt::Expr(e) = s {
                            self.check_expr_use(e, line, col);
                        }
                    }
                }
            }
            // Literals and other terminal expressions: no variables to check.
            Expr::IntLit(_) | Expr::FloatLit(_) | Expr::BoolLit(_)
            | Expr::StringLit(_) | Expr::CharLit(_) | Expr::Wildcard | Expr::ArrayPattern(_, _)
            | Expr::EnumPath(_, _, _) | Expr::MacroInvoc(_) | Expr::Comptime(_)
            | Expr::HandleWith(_, _) | Expr::EffectCall(_, _, _) | Expr::Resume(_)
            | Expr::Template(_) | Expr::TryCatch(_, _, _) => {}
            Expr::DynCast(_, inner) | Expr::Yield(inner) => {
                self.check_expr_use(inner, line, col);
            }
        }
    }
}

/// Public API: analyze ownership in a list of statements.
/// Returns an empty vec if no ownership errors are found.
pub fn analyze_ownership(stmts: &[Stmt], spans: &[(usize, usize)]) -> Vec<OwnershipError> {
    let mut analyzer = OwnershipAnalyzer::new();
    analyzer.analyze(stmts, spans)
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    /// Helper: parse source and run ownership analysis, returning errors.
    fn ownership_errors(src: &str) -> Vec<OwnershipError> {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let result = Parser::new(tokens).parse_with_spans().unwrap();
        analyze_ownership(&result.stmts, &result.spans)
    }

    /// Helper: assert that source has no ownership errors.
    fn assert_ok(src: &str) {
        let errs = ownership_errors(src);
        assert!(errs.is_empty(), "Expected no errors, got: {:?}", errs);
    }

    /// Helper: assert that source has at least one error containing the substring.
    fn assert_err_contains(src: &str, substring: &str) {
        let errs = ownership_errors(src);
        assert!(
            errs.iter().any(|e| e.message.contains(substring)),
            "Expected error containing '{}', got: {:?}", substring, errs
        );
    }

    // ── Use after move ──────────────────────────────────────

    #[test]
    fn test_use_after_move() {
        let src = r#"
let x = own 42
let y = move x
println(x)
"#;
        assert_err_contains(src, "cannot use `x` after move");
    }

    #[test]
    fn test_valid_move_chain() {
        let src = r#"
let a = own 10
let b = move a
let c = move b
println(c)
"#;
        assert_ok(src);
    }

    #[test]
    fn test_double_move() {
        let src = r#"
let x = own 1
let y = move x
let z = move x
"#;
        assert_err_contains(src, "cannot use `x` after move");
    }

    // ── Use after drop ──────────────────────────────────────

    #[test]
    fn test_use_after_drop() {
        let src = r#"
let x = own 99
drop x
println(x)
"#;
        assert_err_contains(src, "cannot use `x` after drop");
    }

    #[test]
    fn test_double_drop() {
        let src = r#"
let x = own 5
drop x
drop x
"#;
        assert_err_contains(src, "already dropped");
    }

    // ── Borrow checks ──────────────────────────────────────

    #[test]
    fn test_move_while_borrowed() {
        let src = r#"
let x = own 42
let y = borrow x
let z = move x
"#;
        assert_err_contains(src, "cannot move `x` while borrowed");
    }

    #[test]
    fn test_borrow_after_move() {
        let src = r#"
let x = own 42
let y = move x
let z = borrow x
"#;
        assert_err_contains(src, "cannot borrow `x`");
    }

    #[test]
    fn test_borrow_after_drop() {
        let src = r#"
let x = own 42
drop x
let y = borrow x
"#;
        assert_err_contains(src, "cannot borrow `x`");
    }

    // ── Scope-based borrow expiry ───────────────────────────

    #[test]
    fn test_valid_own_and_drop() {
        let src = r#"
let x = own 42
println(x)
drop x
"#;
        assert_ok(src);
    }

    #[test]
    fn test_drop_after_move() {
        let src = r#"
let x = own 42
let y = move x
drop x
"#;
        assert_err_contains(src, "cannot drop `x`");
    }

    // ── Assignment checks ───────────────────────────────────

    #[test]
    fn test_assign_to_moved() {
        let src = r#"
let x = own 42
let y = move x
x = 10
"#;
        assert_err_contains(src, "cannot assign to `x`");
    }

    #[test]
    fn test_assign_to_dropped() {
        let src = r#"
let x = own 42
drop x
x = 10
"#;
        assert_err_contains(src, "cannot assign to `x`");
    }

    #[test]
    fn test_mutate_borrowed() {
        let src = r#"
let x = own 42
let y = borrow x
x = 10
"#;
        assert_err_contains(src, "cannot mutate `x`");
    }

    // ── Normal (non-owned) variables are not tracked ────────

    #[test]
    fn test_normal_variables_unaffected() {
        let src = r#"
let x = 42
let y = x + 1
println(y)
"#;
        assert_ok(src);
    }

    // ── Function body analysis ──────────────────────────────

    #[test]
    fn test_use_after_move_in_function() {
        let src = r#"
fn foo() {
    let x = own 10
    let y = move x
    println(x)
}
"#;
        assert_err_contains(src, "cannot use `x` after move");
    }

    #[test]
    fn test_valid_ownership_in_function() {
        let src = r#"
fn bar() {
    let a = own 5
    let b = move a
    println(b)
    drop b
}
"#;
        assert_ok(src);
    }

    // ── Complex expressions ─────────────────────────────────

    #[test]
    fn test_use_after_move_in_binary_expr() {
        let src = r#"
let x = own 42
let y = move x
let z = x + 1
"#;
        assert_err_contains(src, "cannot use `x` after move");
    }

    #[test]
    fn test_drop_non_owned_variable() {
        // Dropping a non-tracked variable just marks it as dropped.
        let src = r#"
let x = 42
drop x
println(x)
"#;
        assert_err_contains(src, "cannot use `x` after drop");
    }
}

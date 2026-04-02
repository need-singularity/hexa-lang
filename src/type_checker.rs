#![allow(dead_code)]

use std::collections::HashMap;
use crate::ast::*;
use crate::error::{HexaError, ErrorClass};

/// Resolved type used during type checking.
#[derive(Debug, Clone, PartialEq)]
pub enum CheckType {
    Int,
    Float,
    Bool,
    Char,
    Str,
    Byte,
    Void,
    Any,
    Array(Box<CheckType>),
    Tuple(Vec<CheckType>),
    Fn(Vec<CheckType>, Box<CheckType>), // param types, return type
    Struct(String),
    Enum(String),
    Map,
    Error,
    Generic(String), // type parameter (e.g. T)
    Unknown, // inferred later or unresolvable
}

impl CheckType {
    /// Parse a type annotation string into a CheckType.
    pub fn from_annotation(s: &str) -> Self {
        match s {
            "int" => CheckType::Int,
            "float" => CheckType::Float,
            "bool" => CheckType::Bool,
            "char" => CheckType::Char,
            "string" => CheckType::Str,
            "byte" => CheckType::Byte,
            "void" => CheckType::Void,
            "any" => CheckType::Any,
            "array" => CheckType::Array(Box::new(CheckType::Unknown)),
            "map" => CheckType::Map,
            other => {
                // Could be a struct/enum name
                if other.chars().next().map_or(false, |c| c.is_uppercase()) {
                    CheckType::Struct(other.to_string())
                } else {
                    CheckType::Unknown
                }
            }
        }
    }

    /// Check if a value type is assignable to this declared type.
    /// Returns true if assignment is valid (including implicit coercions).
    pub fn accepts(&self, value_type: &CheckType) -> bool {
        if self == value_type {
            return true;
        }
        // Any accepts everything
        if matches!(self, CheckType::Any) || matches!(value_type, CheckType::Any) {
            return true;
        }
        // Unknown is permissive (no annotation or unresolvable)
        if matches!(self, CheckType::Unknown) || matches!(value_type, CheckType::Unknown) {
            return true;
        }
        // Generic type parameters accept anything (erased at runtime)
        if matches!(self, CheckType::Generic(_)) || matches!(value_type, CheckType::Generic(_)) {
            return true;
        }
        // int -> float implicit coercion
        if matches!(self, CheckType::Float) && matches!(value_type, CheckType::Int) {
            return true;
        }
        // Array with unknown element type accepts any array
        if let (CheckType::Array(a), CheckType::Array(b)) = (self, value_type) {
            return a.accepts(b);
        }
        // Struct names must match exactly
        if let (CheckType::Struct(a), CheckType::Struct(b)) = (self, value_type) {
            return a == b;
        }
        false
    }

    /// Human-readable name for error messages.
    pub fn display_name(&self) -> String {
        match self {
            CheckType::Int => "int".to_string(),
            CheckType::Float => "float".to_string(),
            CheckType::Bool => "bool".to_string(),
            CheckType::Char => "char".to_string(),
            CheckType::Str => "string".to_string(),
            CheckType::Byte => "byte".to_string(),
            CheckType::Void => "void".to_string(),
            CheckType::Any => "any".to_string(),
            CheckType::Array(inner) => format!("array<{}>", inner.display_name()),
            CheckType::Tuple(items) => {
                let names: Vec<String> = items.iter().map(|t| t.display_name()).collect();
                format!("({})", names.join(", "))
            }
            CheckType::Fn(params, ret) => {
                let p: Vec<String> = params.iter().map(|t| t.display_name()).collect();
                format!("fn({}) -> {}", p.join(", "), ret.display_name())
            }
            CheckType::Struct(name) => name.clone(),
            CheckType::Enum(name) => name.clone(),
            CheckType::Map => "map".to_string(),
            CheckType::Error => "error".to_string(),
            CheckType::Generic(name) => name.clone(),
            CheckType::Unknown => "unknown".to_string(),
        }
    }
}

/// Stored function signature for type checking call sites.
#[derive(Debug, Clone)]
struct FnSig {
    param_types: Vec<Option<CheckType>>, // None = no annotation
    ret_type: Option<CheckType>,         // None = no annotation
}

/// Stored generic function signature for type checking call sites.
#[derive(Debug, Clone)]
struct GenericFnSig {
    type_params: Vec<String>,  // type parameter names (T, U, etc.)
    trait_bounds: HashMap<String, Vec<String>>,  // type param -> required traits
    param_types: Vec<Option<CheckType>>,
    ret_type: Option<CheckType>,
    body: Vec<crate::ast::Stmt>,  // AST body for monomorphized checking
    params: Vec<crate::ast::Param>,  // original param declarations
}

/// Key for specialization cache: (function_name, concrete_types).
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct SpecKey {
    pub fn_name: String,
    pub concrete_types: Vec<String>,  // e.g. ["int", "string"]
}

/// A monomorphized (specialized) function signature with resolved types.
#[derive(Debug, Clone)]
pub struct SpecializedFn {
    pub mangled_name: String,  // e.g. "identity_int"
    pub param_types: Vec<CheckType>,
    pub ret_type: CheckType,
    pub type_map: HashMap<String, CheckType>,  // T -> int, U -> string, etc.
}

pub struct TypeChecker {
    /// Variable types in nested scopes.
    scopes: Vec<HashMap<String, CheckType>>,
    /// Known function signatures.
    fn_sigs: HashMap<String, FnSig>,
    /// Known generic function signatures.
    generic_fn_sigs: HashMap<String, GenericFnSig>,
    /// Known struct declarations.
    struct_defs: HashMap<String, Vec<(String, String)>>, // field name, field type
    /// Known trait definitions: trait_name -> method names
    trait_defs: HashMap<String, Vec<String>>,
    /// Known trait implementations: (type_name, trait_name)
    trait_impls: std::collections::HashSet<(String, String)>,
    /// Currently active type parameters (in scope during generic function checking)
    active_type_params: Vec<String>,
    /// Specialization cache: avoids re-checking the same generic instantiation.
    spec_cache: HashMap<SpecKey, SpecializedFn>,
    /// Generic struct definitions: struct_name -> (type_params, fields)
    generic_struct_defs: HashMap<String, (Vec<String>, Vec<(String, String)>)>,
    /// Collected errors.
    errors: Vec<HexaError>,
}

impl TypeChecker {
    pub fn new() -> Self {
        Self {
            scopes: vec![HashMap::new()],
            fn_sigs: HashMap::new(),
            generic_fn_sigs: HashMap::new(),
            struct_defs: HashMap::new(),
            trait_defs: HashMap::new(),
            trait_impls: std::collections::HashSet::new(),
            active_type_params: Vec::new(),
            spec_cache: HashMap::new(),
            generic_struct_defs: HashMap::new(),
            errors: Vec::new(),
        }
    }

    /// Get the specialization cache (for the interpreter to use).
    pub fn specializations(&self) -> &HashMap<SpecKey, SpecializedFn> {
        &self.spec_cache
    }

    /// Run type checking on the parsed AST with span info.
    /// Returns Ok(()) if no type errors, or Err with the first type error.
    pub fn check(&mut self, stmts: &[Stmt], spans: &[(usize, usize)]) -> Result<(), HexaError> {
        // First pass: collect function signatures, struct definitions, traits, impls
        for stmt in stmts {
            match stmt {
                Stmt::FnDecl(decl) => {
                    if decl.type_params.is_empty() {
                        let sig = FnSig {
                            param_types: decl.params.iter().map(|p| {
                                p.typ.as_ref().map(|t| CheckType::from_annotation(t))
                            }).collect(),
                            ret_type: decl.ret_type.as_ref().map(|t| CheckType::from_annotation(t)),
                        };
                        self.fn_sigs.insert(decl.name.clone(), sig);
                    } else {
                        // Generic function: store type params, bounds, and body for monomorphization
                        let type_param_names: Vec<String> = decl.type_params.iter().map(|tp| tp.name.clone()).collect();
                        // Collect trait bounds from type params and where clauses
                        let mut trait_bounds: HashMap<String, Vec<String>> = HashMap::new();
                        for tp in &decl.type_params {
                            if let Some(bound) = &tp.bound {
                                trait_bounds.entry(tp.name.clone()).or_default().push(bound.clone());
                            }
                        }
                        for wc in &decl.where_clauses {
                            trait_bounds.entry(wc.type_name.clone()).or_default().push(wc.bound.clone());
                        }
                        let sig = GenericFnSig {
                            type_params: type_param_names.clone(),
                            trait_bounds,
                            param_types: decl.params.iter().map(|p| {
                                p.typ.as_ref().map(|t| {
                                    if type_param_names.contains(t) {
                                        CheckType::Generic(t.clone())
                                    } else {
                                        CheckType::from_annotation(t)
                                    }
                                })
                            }).collect(),
                            ret_type: decl.ret_type.as_ref().map(|t| {
                                if type_param_names.contains(t) {
                                    CheckType::Generic(t.clone())
                                } else {
                                    CheckType::from_annotation(t)
                                }
                            }),
                            body: decl.body.clone(),
                            params: decl.params.clone(),
                        };
                        self.generic_fn_sigs.insert(decl.name.clone(), sig);
                    }
                }
                Stmt::StructDecl(decl) => {
                    let fields: Vec<(String, String)> = decl.fields.iter()
                        .map(|(name, typ, _vis)| (name.clone(), typ.clone()))
                        .collect();
                    if !decl.type_params.is_empty() {
                        let tp_names: Vec<String> = decl.type_params.iter().map(|tp| tp.name.clone()).collect();
                        self.generic_struct_defs.insert(decl.name.clone(), (tp_names, fields.clone()));
                    }
                    self.struct_defs.insert(decl.name.clone(), fields);
                }
                Stmt::TraitDecl(decl) => {
                    let method_names: Vec<String> = decl.methods.iter().map(|m| m.name.clone()).collect();
                    self.trait_defs.insert(decl.name.clone(), method_names);
                }
                Stmt::ImplBlock(impl_block) => {
                    if let Some(trait_name) = &impl_block.trait_name {
                        self.trait_impls.insert((impl_block.target.clone(), trait_name.clone()));
                    }
                }
                _ => {}
            }
        }

        // Second pass: check statements
        for (i, stmt) in stmts.iter().enumerate() {
            let (line, col) = spans.get(i).copied().unwrap_or((0, 0));
            self.check_stmt(stmt, line, col);
        }

        if !self.errors.is_empty() {
            return Err(self.errors.remove(0));
        }
        Ok(())
    }

    /// Run type checking (without spans -- uses line 0).
    pub fn check_no_spans(&mut self, stmts: &[Stmt]) -> Result<(), HexaError> {
        let spans: Vec<(usize, usize)> = stmts.iter().map(|_| (0, 0)).collect();
        self.check(stmts, &spans)
    }

    // ── Scope management ────────────────────────────────────

    fn push_scope(&mut self) {
        self.scopes.push(HashMap::new());
    }

    fn pop_scope(&mut self) {
        self.scopes.pop();
    }

    fn define(&mut self, name: &str, typ: CheckType) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(name.to_string(), typ);
        }
    }

    fn lookup(&self, name: &str) -> Option<CheckType> {
        for scope in self.scopes.iter().rev() {
            if let Some(typ) = scope.get(name) {
                return Some(typ.clone());
            }
        }
        None
    }

    fn emit_error(&mut self, line: usize, col: usize, message: String) {
        self.errors.push(HexaError {
            class: ErrorClass::Type,
            message,
            line,
            col,
            hint: None,
        });
    }

    // ── Statement checking ──────────────────────────────────

    fn check_stmt(&mut self, stmt: &Stmt, line: usize, col: usize) {
        match stmt {
            Stmt::Let(name, typ_ann, expr, _vis) => {
                let value_type = expr.as_ref()
                    .map(|e| self.infer_expr(e))
                    .unwrap_or(CheckType::Void);

                if let Some(ref ann) = typ_ann {
                    let declared = CheckType::from_annotation(ann);
                    if !declared.accepts(&value_type) {
                        self.emit_error(line, col, format!(
                            "type mismatch: variable '{}' declared as {} but assigned {}",
                            name, declared.display_name(), value_type.display_name()
                        ));
                        // Still define with declared type for further checking
                        self.define(name, declared);
                        return;
                    }
                    self.define(name, declared);
                } else {
                    self.define(name, value_type);
                }
            }
            Stmt::FnDecl(decl) => {
                self.check_fn_decl(decl, line, col);
                // Define the function name in scope
                let param_types: Vec<CheckType> = decl.params.iter()
                    .map(|p| p.typ.as_ref()
                        .map(|t| CheckType::from_annotation(t))
                        .unwrap_or(CheckType::Unknown))
                    .collect();
                let ret = decl.ret_type.as_ref()
                    .map(|t| CheckType::from_annotation(t))
                    .unwrap_or(CheckType::Unknown);
                self.define(&decl.name, CheckType::Fn(param_types, Box::new(ret)));
            }
            Stmt::Expr(expr) => {
                self.check_expr(expr, line, col);
            }
            Stmt::Return(expr) => {
                if let Some(e) = expr {
                    self.check_expr(e, line, col);
                }
            }
            Stmt::Assign(_lhs, rhs) => {
                self.check_expr(rhs, line, col);
            }
            Stmt::For(var, iter_expr, body) => {
                self.check_expr(iter_expr, line, col);
                self.push_scope();
                // Infer loop variable type from iterable
                let iter_type = self.infer_expr(iter_expr);
                let elem_type = match iter_type {
                    CheckType::Array(inner) => *inner,
                    _ => CheckType::Unknown,
                };
                self.define(var, elem_type);
                for s in body {
                    self.check_stmt(s, line, col);
                }
                self.pop_scope();
            }
            Stmt::While(cond, body) => {
                self.check_expr(cond, line, col);
                self.push_scope();
                for s in body {
                    self.check_stmt(s, line, col);
                }
                self.pop_scope();
            }
            Stmt::Loop(body) => {
                self.push_scope();
                for s in body {
                    self.check_stmt(s, line, col);
                }
                self.pop_scope();
            }
            Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::TraitDecl(_)
            | Stmt::ImplBlock(_) | Stmt::Intent(_, _) | Stmt::Verify(_, _)
            | Stmt::Mod(_, _) | Stmt::Use(_) | Stmt::DropStmt(_)
            | Stmt::SpawnNamed(_, _) | Stmt::Select(_, _)
            | Stmt::Generate(_) | Stmt::Optimize(_)
            | Stmt::Const(..) | Stmt::Static(..)
            | Stmt::MacroDef(_) | Stmt::DeriveDecl(..) => {}
            Stmt::Assert(expr) => {
                self.check_expr(expr, line, col);
            }
            Stmt::Proof(_name, body) => {
                self.push_scope();
                for s in body {
                    self.check_stmt(s, line, col);
                }
                self.pop_scope();
            }
            Stmt::TryCatch(try_block, err_name, catch_block) => {
                self.push_scope();
                for s in try_block {
                    self.check_stmt(s, line, col);
                }
                self.pop_scope();
                self.push_scope();
                self.define(err_name, CheckType::Error);
                for s in catch_block {
                    self.check_stmt(s, line, col);
                }
                self.pop_scope();
            }
            Stmt::Throw(expr) => {
                self.check_expr(expr, line, col);
            }
            Stmt::Spawn(body) => {
                self.push_scope();
                for s in body {
                    self.check_stmt(s, line, col);
                }
                self.pop_scope();
            }
            Stmt::AsyncFnDecl(decl) => {
                self.check_fn_decl(decl, line, col);
                let param_types: Vec<CheckType> = decl.params.iter()
                    .map(|p| p.typ.as_ref()
                        .map(|t| CheckType::from_annotation(t))
                        .unwrap_or(CheckType::Unknown))
                    .collect();
                let ret = decl.ret_type.as_ref()
                    .map(|t| CheckType::from_annotation(t))
                    .unwrap_or(CheckType::Unknown);
                self.define(&decl.name, CheckType::Fn(param_types, Box::new(ret)));
            }
            Stmt::ComptimeFn(decl) => {
                let param_types: Vec<CheckType> = decl.params.iter()
                    .map(|p| p.typ.as_ref()
                        .map(|t| CheckType::from_annotation(t))
                        .unwrap_or(CheckType::Unknown))
                    .collect();
                let ret = decl.ret_type.as_ref()
                    .map(|t| CheckType::from_annotation(t))
                    .unwrap_or(CheckType::Unknown);
                self.define(&decl.name, CheckType::Fn(param_types, Box::new(ret)));
            }
            Stmt::EffectDecl(_) => {}
        }
    }

    fn check_fn_decl(&mut self, decl: &FnDecl, line: usize, col: usize) {
        self.push_scope();

        // Define parameter types in the function scope
        for param in &decl.params {
            let typ = param.typ.as_ref()
                .map(|t| CheckType::from_annotation(t))
                .unwrap_or(CheckType::Unknown);
            self.define(&param.name, typ);
        }

        // Check body and collect return types
        let mut return_types: Vec<CheckType> = Vec::new();
        self.collect_return_types(&decl.body, &mut return_types);

        // Check return type if annotated
        if let Some(ret_ann) = &decl.ret_type {
            let declared_ret = CheckType::from_annotation(ret_ann);
            for ret_type in &return_types {
                if !declared_ret.accepts(ret_type) {
                    self.emit_error(line, col, format!(
                        "type mismatch: function '{}' declared to return {} but returns {}",
                        decl.name, declared_ret.display_name(), ret_type.display_name()
                    ));
                    break;
                }
            }
        }

        // Check body statements
        for s in &decl.body {
            self.check_stmt(s, line, col);
        }

        self.pop_scope();
    }

    /// Recursively collect return expression types from a block of statements.
    fn collect_return_types(&self, stmts: &[Stmt], out: &mut Vec<CheckType>) {
        for stmt in stmts {
            match stmt {
                Stmt::Return(Some(expr)) => {
                    out.push(self.infer_expr(expr));
                }
                Stmt::Return(None) => {
                    out.push(CheckType::Void);
                }
                // Recurse into blocks
                Stmt::For(_, _, body) | Stmt::While(_, body) | Stmt::Loop(body)
                | Stmt::Proof(_, body) | Stmt::Spawn(body) | Stmt::SpawnNamed(_, body) => {
                    self.collect_return_types(body, out);
                }
                Stmt::Expr(Expr::If(_, then_block, else_block)) => {
                    self.collect_return_types(then_block, out);
                    if let Some(eb) = else_block {
                        self.collect_return_types(eb, out);
                    }
                }
                Stmt::TryCatch(try_block, _, catch_block) => {
                    self.collect_return_types(try_block, out);
                    self.collect_return_types(catch_block, out);
                }
                _ => {}
            }
        }
    }

    // ── Expression checking ─────────────────────────────────

    fn check_expr(&mut self, expr: &Expr, line: usize, col: usize) {
        match expr {
            Expr::Call(callee, args) => {
                // Check if calling a known function with type annotations
                if let Expr::Ident(fn_name) = callee.as_ref() {
                    if let Some(sig) = self.fn_sigs.get(fn_name).cloned() {
                        // Non-generic function: check argument types against parameter types
                        for (i, (arg, param_type)) in args.iter().zip(sig.param_types.iter()).enumerate() {
                            if let Some(expected) = param_type {
                                let actual = self.infer_expr(arg);
                                if !expected.accepts(&actual) {
                                    self.emit_error(line, col, format!(
                                        "type mismatch: argument {} of '{}' expected {} but got {}",
                                        i + 1, fn_name, expected.display_name(), actual.display_name()
                                    ));
                                }
                            }
                        }
                    } else if let Some(gsig) = self.generic_fn_sigs.get(fn_name).cloned() {
                        // Generic function: monomorphize -- infer concrete types from arguments
                        self.monomorphize_call(fn_name, &gsig, args, line, col);
                    }
                }
                // Recurse into subexpressions
                self.check_expr(callee, line, col);
                for arg in args {
                    self.check_expr(arg, line, col);
                }
            }
            Expr::Binary(left, _, right) => {
                self.check_expr(left, line, col);
                self.check_expr(right, line, col);
            }
            Expr::Unary(_, operand) => {
                self.check_expr(operand, line, col);
            }
            Expr::If(cond, then_block, else_block) => {
                self.check_expr(cond, line, col);
                for s in then_block {
                    self.check_stmt(s, line, col);
                }
                if let Some(eb) = else_block {
                    for s in eb {
                        self.check_stmt(s, line, col);
                    }
                }
            }
            Expr::Array(items) => {
                for item in items {
                    self.check_expr(item, line, col);
                }
            }
            Expr::Block(stmts) => {
                self.push_scope();
                for s in stmts {
                    self.check_stmt(s, line, col);
                }
                self.pop_scope();
            }
            Expr::Lambda(params, body) => {
                self.push_scope();
                for p in params {
                    let typ = p.typ.as_ref()
                        .map(|t| CheckType::from_annotation(t))
                        .unwrap_or(CheckType::Unknown);
                    self.define(&p.name, typ);
                }
                self.check_expr(body, line, col);
                self.pop_scope();
            }
            _ => {} // Literals, identifiers, etc. are fine
        }
    }

    // ── Type inference ──────────────────────────────────────

    /// Infer the type of an expression without emitting errors.
    fn infer_expr(&self, expr: &Expr) -> CheckType {
        match expr {
            Expr::IntLit(_) => CheckType::Int,
            Expr::FloatLit(_) => CheckType::Float,
            Expr::BoolLit(_) => CheckType::Bool,
            Expr::StringLit(_) => CheckType::Str,
            Expr::CharLit(_) => CheckType::Char,
            Expr::Ident(name) => {
                self.lookup(name).unwrap_or(CheckType::Unknown)
            }
            Expr::Binary(left, op, right) => {
                let l = self.infer_expr(left);
                let r = self.infer_expr(right);
                match op {
                    // Comparison operators always return bool
                    BinOp::Eq | BinOp::Ne | BinOp::Lt | BinOp::Gt
                    | BinOp::Le | BinOp::Ge => CheckType::Bool,
                    // Logical operators return bool
                    BinOp::And | BinOp::Or | BinOp::Xor => CheckType::Bool,
                    // Arithmetic: promote to float if either is float
                    BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div
                    | BinOp::Rem | BinOp::Pow => {
                        // String + String = String
                        if matches!((&l, op), (CheckType::Str, BinOp::Add)) {
                            return CheckType::Str;
                        }
                        if matches!((&l, &r), (CheckType::Float, _) | (_, CheckType::Float)) {
                            CheckType::Float
                        } else if matches!((&l, &r), (CheckType::Int, CheckType::Int)) {
                            CheckType::Int
                        } else {
                            CheckType::Unknown
                        }
                    }
                    // Bitwise
                    BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor => CheckType::Int,
                    _ => CheckType::Unknown,
                }
            }
            Expr::Unary(op, operand) => {
                let inner = self.infer_expr(operand);
                match op {
                    UnaryOp::Neg => inner, // -int = int, -float = float
                    UnaryOp::Not => CheckType::Bool,
                    UnaryOp::BitNot => CheckType::Int,
                }
            }
            Expr::Array(items) => {
                if items.is_empty() {
                    CheckType::Array(Box::new(CheckType::Unknown))
                } else {
                    let elem = self.infer_expr(&items[0]);
                    CheckType::Array(Box::new(elem))
                }
            }
            Expr::Tuple(items) => {
                let types: Vec<CheckType> = items.iter().map(|e| self.infer_expr(e)).collect();
                CheckType::Tuple(types)
            }
            Expr::Call(callee, call_args) => {
                // Try to get return type from function signature
                if let Expr::Ident(fn_name) = callee.as_ref() {
                    if let Some(sig) = self.fn_sigs.get(fn_name) {
                        if let Some(ret) = &sig.ret_type {
                            return ret.clone();
                        }
                    }
                    // For generic functions, infer return type via monomorphization
                    if let Some(gsig) = self.generic_fn_sigs.get(fn_name).cloned() {
                        let type_map = self.infer_type_params(&gsig, call_args);
                        if let Some(ret) = &gsig.ret_type {
                            return self.substitute_type(ret, &type_map);
                        }
                    }
                }
                CheckType::Unknown
            }
            Expr::If(_, then_block, _) => {
                // Infer from last expression in then block
                if let Some(last) = then_block.last() {
                    if let Stmt::Expr(e) = last {
                        return self.infer_expr(e);
                    }
                }
                CheckType::Void
            }
            Expr::Range(_, _, _) => CheckType::Array(Box::new(CheckType::Int)),
            Expr::StructInit(name, _) => CheckType::Struct(name.clone()),
            Expr::MapLit(_) => CheckType::Map,
            Expr::EnumPath(name, _, _) => CheckType::Enum(name.clone()),
            Expr::Field(_, _) => CheckType::Unknown,
            Expr::Index(arr, _) => {
                let arr_type = self.infer_expr(arr);
                match arr_type {
                    CheckType::Array(inner) => *inner,
                    CheckType::Str => CheckType::Char,
                    _ => CheckType::Unknown,
                }
            }
            Expr::Block(stmts) => {
                if let Some(last) = stmts.last() {
                    if let Stmt::Expr(e) = last {
                        return self.infer_expr(e);
                    }
                }
                CheckType::Void
            }
            Expr::Lambda(_, _) => CheckType::Unknown, // could be Fn(...) but complex
            Expr::Match(_, _) => CheckType::Unknown,
            Expr::Own(inner) => self.infer_expr(inner),
            Expr::MoveExpr(inner) => self.infer_expr(inner),
            Expr::Borrow(inner) => self.infer_expr(inner),
            Expr::Await(inner) => self.infer_expr(inner),
            Expr::Wildcard => CheckType::Unknown,
            _ => CheckType::Unknown,  // MacroInvoc, etc.
        }
    }

    // ── Monomorphization ───────────────────────────────────

    /// Infer concrete types for type parameters from call arguments.
    /// Returns a map: type_param_name -> concrete CheckType.
    fn infer_type_params(&self, gsig: &GenericFnSig, args: &[Expr]) -> HashMap<String, CheckType> {
        let mut type_map: HashMap<String, CheckType> = HashMap::new();
        for (i, param_type) in gsig.param_types.iter().enumerate() {
            if let Some(CheckType::Generic(tp_name)) = param_type {
                if let Some(arg) = args.get(i) {
                    let actual = self.infer_expr(arg);
                    if !matches!(actual, CheckType::Unknown) {
                        type_map.insert(tp_name.clone(), actual);
                    }
                }
            }
        }
        type_map
    }

    /// Substitute generic type parameters with concrete types.
    fn substitute_type(&self, typ: &CheckType, type_map: &HashMap<String, CheckType>) -> CheckType {
        match typ {
            CheckType::Generic(name) => {
                type_map.get(name).cloned().unwrap_or(CheckType::Unknown)
            }
            CheckType::Array(inner) => {
                CheckType::Array(Box::new(self.substitute_type(inner, type_map)))
            }
            CheckType::Tuple(items) => {
                CheckType::Tuple(items.iter().map(|t| self.substitute_type(t, type_map)).collect())
            }
            CheckType::Fn(params, ret) => {
                CheckType::Fn(
                    params.iter().map(|t| self.substitute_type(t, type_map)).collect(),
                    Box::new(self.substitute_type(ret, type_map)),
                )
            }
            other => other.clone(),
        }
    }

    /// Generate a mangled name for a specialization: "identity_int", "pair_first_int_string".
    fn mangle_name(fn_name: &str, concrete_types: &[CheckType]) -> String {
        let suffix: Vec<String> = concrete_types.iter()
            .map(|t| t.display_name().replace('<', "_").replace('>', ""))
            .collect();
        format!("{}_{}", fn_name, suffix.join("_"))
    }

    /// Monomorphize a generic function call: infer types, check bounds, type-check body.
    fn monomorphize_call(&mut self, fn_name: &str, gsig: &GenericFnSig, args: &[Expr], line: usize, col: usize) {
        // Step 1: Infer concrete types from arguments
        let type_map = self.infer_type_params(gsig, args);

        // Step 2: Build concrete type list for cache key
        let concrete_types: Vec<String> = gsig.type_params.iter()
            .map(|tp| type_map.get(tp).map_or("unknown".to_string(), |t| t.display_name()))
            .collect();

        let key = SpecKey {
            fn_name: fn_name.to_string(),
            concrete_types: concrete_types.clone(),
        };

        // Already specialized? Skip.
        if self.spec_cache.contains_key(&key) {
            // Still check arguments against the specialized signature
            if let Some(spec) = self.spec_cache.get(&key).cloned() {
                for (i, (arg, expected)) in args.iter().zip(spec.param_types.iter()).enumerate() {
                    let actual = self.infer_expr(arg);
                    if !expected.accepts(&actual) {
                        self.emit_error(line, col, format!(
                            "type mismatch: argument {} of '{}' expected {} but got {} in {}",
                            i + 1, fn_name, expected.display_name(), actual.display_name(),
                            spec.mangled_name
                        ));
                    }
                }
            }
            return;
        }

        // Step 3: Check trait bounds
        for (tp_name, bounds) in &gsig.trait_bounds {
            if let Some(concrete) = type_map.get(tp_name) {
                let concrete_name = concrete.display_name();
                for bound in bounds {
                    if !self.type_satisfies_trait(&concrete_name, bound) {
                        self.emit_error(line, col, format!(
                            "type '{}' does not implement trait '{}' required by type parameter '{}' in '{}'",
                            concrete_name, bound, tp_name, fn_name
                        ));
                    }
                }
            }
        }

        // Step 4: Resolve specialized param/return types
        let spec_param_types: Vec<CheckType> = gsig.param_types.iter()
            .map(|pt| match pt {
                Some(t) => self.substitute_type(t, &type_map),
                None => CheckType::Unknown,
            })
            .collect();
        let spec_ret_type = match &gsig.ret_type {
            Some(t) => self.substitute_type(t, &type_map),
            None => CheckType::Unknown,
        };

        let concrete_checks: Vec<CheckType> = gsig.type_params.iter()
            .map(|tp| type_map.get(tp).cloned().unwrap_or(CheckType::Unknown))
            .collect();
        let mangled = Self::mangle_name(fn_name, &concrete_checks);

        let spec = SpecializedFn {
            mangled_name: mangled.clone(),
            param_types: spec_param_types.clone(),
            ret_type: spec_ret_type,
            type_map: type_map.clone(),
        };
        self.spec_cache.insert(key, spec);

        // Step 5: Check argument types against specialized parameter types
        for (i, (arg, expected)) in args.iter().zip(spec_param_types.iter()).enumerate() {
            let actual = self.infer_expr(arg);
            if !expected.accepts(&actual) {
                self.emit_error(line, col, format!(
                    "type mismatch: argument {} of '{}' expected {} but got {} in {}",
                    i + 1, fn_name, expected.display_name(), actual.display_name(),
                    mangled
                ));
            }
        }

        // Step 6: Type-check function body with concrete types
        self.push_scope();
        for (param, ptype) in gsig.params.iter().zip(spec_param_types.iter()) {
            self.define(&param.name, ptype.clone());
        }
        // Set active type params to none (they're resolved now)
        let prev_type_params = std::mem::take(&mut self.active_type_params);
        for s in &gsig.body {
            self.check_stmt(s, line, col);
        }
        self.active_type_params = prev_type_params;
        self.pop_scope();
    }

    /// Check if a concrete type satisfies a trait bound.
    fn type_satisfies_trait(&self, type_name: &str, trait_name: &str) -> bool {
        // Check explicit impl declarations
        if self.trait_impls.contains(&(type_name.to_string(), trait_name.to_string())) {
            return true;
        }
        // Built-in trait satisfaction for primitive types
        match trait_name {
            "Display" => matches!(type_name, "int" | "float" | "bool" | "string" | "char" | "byte"),
            "Debug" => matches!(type_name, "int" | "float" | "bool" | "string" | "char" | "byte" | "array" | "void"),
            "Clone" | "Copy" => matches!(type_name, "int" | "float" | "bool" | "char" | "byte"),
            "PartialEq" | "Eq" => matches!(type_name, "int" | "float" | "bool" | "string" | "char" | "byte"),
            "PartialOrd" | "Ord" => matches!(type_name, "int" | "float" | "char" | "byte"),
            "Add" => matches!(type_name, "int" | "float" | "string"),
            "Numeric" => matches!(type_name, "int" | "float"),
            "Hash" => matches!(type_name, "int" | "bool" | "string" | "char" | "byte"),
            _ => false,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn check_source(src: &str) -> Result<(), HexaError> {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let result = Parser::new(tokens).parse_with_spans().unwrap();
        let mut checker = TypeChecker::new();
        checker.check(&result.stmts, &result.spans)
    }

    // ── Type annotation enforcement tests ──────────────────

    #[test]
    fn test_let_type_mismatch_int_string() {
        let result = check_source("let x: int = \"hello\"");
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert_eq!(err.class, ErrorClass::Type);
        assert!(err.message.contains("type mismatch"));
        assert!(err.message.contains("int"));
        assert!(err.message.contains("string"));
    }

    #[test]
    fn test_let_type_match_ok() {
        assert!(check_source("let x: int = 42").is_ok());
        assert!(check_source("let x: float = 3.14").is_ok());
        assert!(check_source("let x: string = \"hello\"").is_ok());
        assert!(check_source("let x: bool = true").is_ok());
    }

    #[test]
    fn test_let_int_to_float_coercion() {
        // int -> float should be allowed (implicit coercion)
        assert!(check_source("let x: float = 5").is_ok());
    }

    #[test]
    fn test_fn_return_type_mismatch() {
        let result = check_source("fn foo() -> int {\n  return \"nope\"\n}");
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert_eq!(err.class, ErrorClass::Type);
        assert!(err.message.contains("type mismatch"));
        assert!(err.message.contains("foo"));
    }

    #[test]
    fn test_fn_return_type_ok() {
        assert!(check_source("fn foo() -> int {\n  return 42\n}").is_ok());
    }

    #[test]
    fn test_fn_arg_type_mismatch() {
        let src = "fn foo(x: int) -> int {\n  return x\n}\nfoo(\"hello\")";
        let result = check_source(src);
        assert!(result.is_err());
        let err = result.unwrap_err();
        assert_eq!(err.class, ErrorClass::Type);
        assert!(err.message.contains("argument 1"));
        assert!(err.message.contains("foo"));
    }

    #[test]
    fn test_fn_arg_type_ok() {
        let src = "fn double(x: int) -> int {\n  return x * 2\n}\ndouble(6)";
        assert!(check_source(src).is_ok());
    }

    #[test]
    fn test_fn_arg_int_to_float_coercion() {
        let src = "fn half(x: float) -> float {\n  return x / 2.0\n}\nhalf(10)";
        assert!(check_source(src).is_ok());
    }

    #[test]
    fn test_untyped_code_still_works() {
        // No annotations -> no type errors
        assert!(check_source("let x = 5").is_ok());
        assert!(check_source("let x = \"hello\"").is_ok());
        assert!(check_source("fn foo(x) {\n  return x\n}\nfoo(42)").is_ok());
    }

    #[test]
    fn test_let_type_mismatch_float_to_int() {
        // float -> int should NOT be allowed implicitly
        let result = check_source("let x: int = 3.14");
        assert!(result.is_err());
    }

    #[test]
    fn test_let_type_mismatch_bool_to_int() {
        let result = check_source("let x: int = true");
        assert!(result.is_err());
    }

    #[test]
    fn test_array_type_inference() {
        // let x: int = [1, 2, 3] should fail (array vs int)
        let result = check_source("let x: int = [1, 2, 3]");
        assert!(result.is_err());
    }
}

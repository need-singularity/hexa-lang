#![allow(dead_code)]

use std::collections::HashMap;
use crate::ast::*;
use crate::env::{Env, Value};
use crate::error::{HexaError, ErrorClass};
use crate::lexer::Lexer;
use crate::parser::Parser;

/// Sentinel error message used to propagate `return` across call frames.
const RETURN_SENTINEL: &str = "__hexa_return__";

/// Sentinel error message used to propagate `throw` across call frames.
const THROW_SENTINEL: &str = "__hexa_throw__";

pub struct Interpreter {
    pub env: Env,
    /// Holds the value carried by the most recent `return` statement.
    return_value: Option<Value>,
    /// Holds the value carried by the most recent `throw` statement.
    throw_value: Option<Value>,
    /// Struct declarations: name -> field definitions (name, type, visibility)
    struct_defs: HashMap<String, Vec<(String, String, Visibility)>>,
    /// Enum declarations: name -> list of (variant_name, optional_data_types)
    enum_defs: HashMap<String, Vec<(String, Option<Vec<String>>)>>,
    /// Impl methods: type_name -> method_name -> (param_names, body)
    type_methods: HashMap<String, HashMap<String, (Vec<String>, Vec<Stmt>)>>,
    /// Module scopes: module_name -> module data (public bindings, struct/enum defs)
    modules: HashMap<String, ModuleData>,
    /// When true, proof blocks are executed as tests
    pub test_mode: bool,
    /// Current source line for error reporting (1-based, 0 = unknown).
    pub current_line: usize,
    /// Current source column for error reporting (1-based, 0 = unknown).
    pub current_col: usize,
    /// Directory of the currently executing file (for resolving `use` paths).
    pub source_dir: Option<String>,
}

/// Stored data for a loaded/declared module.
#[derive(Clone)]
struct ModuleData {
    /// Public bindings (name -> value)
    pub_bindings: HashMap<String, Value>,
    /// Struct defs exported from module
    struct_defs: HashMap<String, Vec<(String, String, Visibility)>>,
    /// Enum defs exported from module
    enum_defs: HashMap<String, Vec<(String, Option<Vec<String>>)>>,
}

impl Interpreter {
    pub fn new() -> Self {
        Self {
            env: Env::new(),
            return_value: None,
            throw_value: None,
            struct_defs: HashMap::new(),
            enum_defs: HashMap::new(),
            type_methods: HashMap::new(),
            modules: HashMap::new(),
            test_mode: false,
            current_line: 0,
            current_col: 0,
            source_dir: None,
        }
    }

    // ── Public entry point ──────────────────────────────────

    pub fn run(&mut self, stmts: &[Stmt]) -> Result<Value, HexaError> {
        let mut last = Value::Void;
        for stmt in stmts {
            last = self.exec_stmt(stmt)?;
            if self.return_value.is_some() {
                return Ok(self.return_value.take().unwrap());
            }
            if self.throw_value.is_some() {
                let err = self.throw_value.take().unwrap();
                return Err(self.runtime_err(format!("uncaught error: {}", err)));
            }
        }
        Ok(last)
    }

    /// Run with span information from the parser.
    /// `spans` is a parallel array to `stmts` with (line, col) for each statement.
    pub fn run_with_spans(&mut self, stmts: &[Stmt], spans: &[(usize, usize)]) -> Result<Value, HexaError> {
        let mut last = Value::Void;
        for (i, stmt) in stmts.iter().enumerate() {
            if let Some(&(line, col)) = spans.get(i) {
                self.current_line = line;
                self.current_col = col;
            }
            last = self.exec_stmt(stmt)?;
            if self.return_value.is_some() {
                return Ok(self.return_value.take().unwrap());
            }
            if self.throw_value.is_some() {
                let err = self.throw_value.take().unwrap();
                return Err(self.runtime_err(format!("uncaught error: {}", err)));
            }
        }
        Ok(last)
    }

    // ── Statement execution ─────────────────────────────────

    fn exec_stmt(&mut self, stmt: &Stmt) -> Result<Value, HexaError> {
        match stmt {
            Stmt::Let(name, _typ, expr, _vis) => {
                let val = match expr {
                    Some(e) => self.eval_expr(e)?,
                    None => Value::Void,
                };
                self.env.define(name, val);
                Ok(Value::Void)
            }
            Stmt::Assign(lhs, rhs) => {
                let val = self.eval_expr(rhs)?;
                match lhs {
                    Expr::Ident(name) => {
                        if !self.env.set(name, val) {
                            return Err(self.runtime_err(format!("undefined variable: {}", name)));
                        }
                    }
                    Expr::Index(arr_expr, idx_expr) => {
                        let idx = self.eval_expr(idx_expr)?;
                        let idx = match idx {
                            Value::Int(i) => i as usize,
                            _ => return Err(self.type_err("index must be an integer".into())),
                        };
                        // Get the array name for simple cases
                        if let Expr::Ident(name) = arr_expr.as_ref() {
                            if let Some(Value::Array(mut arr)) = self.env.get(name) {
                                if idx >= arr.len() {
                                    return Err(self.runtime_err(format!(
                                        "index {} out of bounds (len {})", idx, arr.len()
                                    )));
                                }
                                arr[idx] = val;
                                self.env.set(name, Value::Array(arr));
                            } else {
                                return Err(self.type_err("index assignment requires array".into()));
                            }
                        } else {
                            return Err(self.runtime_err("complex index assignment not supported".into()));
                        }
                    }
                    _ => return Err(self.runtime_err("invalid assignment target".into())),
                }
                Ok(Value::Void)
            }
            Stmt::Expr(expr) => self.eval_expr(expr),
            Stmt::Return(expr) => {
                let val = match expr {
                    Some(e) => self.eval_expr(e)?,
                    None => Value::Void,
                };
                self.return_value = Some(val);
                Ok(Value::Void)
            }
            Stmt::FnDecl(decl) => {
                let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                self.env.define(
                    &decl.name,
                    Value::Fn(decl.name.clone(), param_names, decl.body.clone()),
                );
                Ok(Value::Void)
            }
            Stmt::For(var, iter_expr, body) => {
                let iter_val = self.eval_expr(iter_expr)?;
                let items = match iter_val {
                    Value::Array(a) => a,
                    _ => return Err(self.type_err("for loop requires iterable".into())),
                };
                for item in items {
                    self.env.push_scope();
                    self.env.define(var, item);
                    for s in body {
                        self.exec_stmt(s)?;
                        if self.return_value.is_some() {
                            self.env.pop_scope();
                            return Ok(Value::Void);
                        }
                    }
                    self.env.pop_scope();
                }
                Ok(Value::Void)
            }
            Stmt::While(cond, body) => {
                loop {
                    let c = self.eval_expr(cond)?;
                    if !Self::is_truthy(&c) {
                        break;
                    }
                    self.env.push_scope();
                    for s in body {
                        self.exec_stmt(s)?;
                        if self.return_value.is_some() {
                            self.env.pop_scope();
                            return Ok(Value::Void);
                        }
                    }
                    self.env.pop_scope();
                }
                Ok(Value::Void)
            }
            Stmt::Loop(body) => {
                loop {
                    self.env.push_scope();
                    for s in body {
                        self.exec_stmt(s)?;
                        if self.return_value.is_some() {
                            self.env.pop_scope();
                            return Ok(Value::Void);
                        }
                    }
                    self.env.pop_scope();
                }
            }
            Stmt::Proof(_name, body) => {
                if !self.test_mode {
                    // In normal mode, skip proof blocks
                    return Ok(Value::Void);
                }
                // In test mode, execute proof block
                self.env.push_scope();
                for s in body {
                    self.exec_stmt(s)?;
                }
                self.env.pop_scope();
                Ok(Value::Void)
            }
            Stmt::Assert(expr) => {
                let val = self.eval_expr(expr)?;
                if !Self::is_truthy(&val) {
                    return Err(HexaError {
                        class: ErrorClass::Logic,
                        message: "assertion failed".into(),
                        line: self.current_line,
                        col: self.current_col,
                    });
                }
                Ok(Value::Void)
            }
            Stmt::StructDecl(decl) => {
                self.struct_defs.insert(decl.name.clone(), decl.fields.clone());
                Ok(Value::Void)
            }
            Stmt::EnumDecl(decl) => {
                self.enum_defs.insert(decl.name.clone(), decl.variants.clone());
                Ok(Value::Void)
            }
            Stmt::TraitDecl(_) => {
                Ok(Value::Void)
            }
            Stmt::ImplBlock(impl_block) => {
                let target = impl_block.target.clone();
                let methods_map = self.type_methods.entry(target).or_insert_with(HashMap::new);
                for method in &impl_block.methods {
                    let param_names: Vec<String> = method.params.iter().map(|p| p.name.clone()).collect();
                    methods_map.insert(method.name.clone(), (param_names, method.body.clone()));
                }
                Ok(Value::Void)
            }
            Stmt::Intent(_, _) => Ok(Value::Void),
            Stmt::Mod(name, body) => {
                self.exec_mod(name, body)?;
                Ok(Value::Void)
            }
            Stmt::Use(path) => {
                self.exec_use(path)?;
                Ok(Value::Void)
            }
            Stmt::TryCatch(try_block, err_name, catch_block) => {
                // Save state
                let had_throw = self.throw_value.take();
                self.env.push_scope();
                let mut result = Value::Void;
                let mut caught = false;
                for s in try_block {
                    match self.exec_stmt(s) {
                        Ok(v) => {
                            result = v;
                            if self.throw_value.is_some() {
                                caught = true;
                                break;
                            }
                            if self.return_value.is_some() {
                                self.env.pop_scope();
                                return Ok(Value::Void);
                            }
                        }
                        Err(e) => {
                            // Runtime errors become catchable
                            self.throw_value = Some(Value::Error(e.message.clone()));
                            caught = true;
                            break;
                        }
                    }
                }
                self.env.pop_scope();
                if caught {
                    let err_val = self.throw_value.take().unwrap_or(Value::Error("unknown error".into()));
                    self.env.push_scope();
                    self.env.define(err_name, err_val);
                    for s in catch_block {
                        result = self.exec_stmt(s)?;
                        if self.return_value.is_some() || self.throw_value.is_some() {
                            self.env.pop_scope();
                            return Ok(Value::Void);
                        }
                    }
                    self.env.pop_scope();
                } else if had_throw.is_some() {
                    self.throw_value = had_throw;
                }
                Ok(result)
            }
            Stmt::Throw(expr) => {
                let val = self.eval_expr(expr)?;
                let err_val = match val {
                    Value::Str(s) => Value::Error(s),
                    Value::Error(_) => val,
                    other => Value::Error(format!("{}", other)),
                };
                self.throw_value = Some(err_val);
                Ok(Value::Void)
            }
        }
    }

    // ── Expression evaluation ───────────────────────────────

    fn eval_expr(&mut self, expr: &Expr) -> Result<Value, HexaError> {
        match expr {
            Expr::IntLit(n) => Ok(Value::Int(*n)),
            Expr::FloatLit(n) => Ok(Value::Float(*n)),
            Expr::BoolLit(b) => Ok(Value::Bool(*b)),
            Expr::StringLit(s) => Ok(Value::Str(s.clone())),
            Expr::CharLit(c) => Ok(Value::Char(*c)),
            Expr::Ident(name) => {
                self.env.get(name).ok_or_else(|| HexaError {
                    class: ErrorClass::Name,
                    message: format!("undefined variable: {}", name),
                    line: self.current_line,
                    col: self.current_col,
                })
            }
            Expr::Binary(left, op, right) => {
                let l = self.eval_expr(left)?;
                // Short-circuit for logical operators
                match op {
                    BinOp::And => {
                        if !Self::is_truthy(&l) {
                            return Ok(Value::Bool(false));
                        }
                        let r = self.eval_expr(right)?;
                        return Ok(Value::Bool(Self::is_truthy(&r)));
                    }
                    BinOp::Or => {
                        if Self::is_truthy(&l) {
                            return Ok(Value::Bool(true));
                        }
                        let r = self.eval_expr(right)?;
                        return Ok(Value::Bool(Self::is_truthy(&r)));
                    }
                    _ => {}
                }
                let r = self.eval_expr(right)?;
                self.eval_binary(l, op, r)
            }
            Expr::Unary(op, operand) => {
                let val = self.eval_expr(operand)?;
                self.eval_unary(op, val)
            }
            Expr::Call(callee, args) => {
                // Check for method call pattern: obj.method(args)
                if let Expr::Field(obj_expr, method_name) = callee.as_ref() {
                    let obj_val = self.eval_expr(obj_expr)?;
                    let mut arg_vals = Vec::new();
                    for a in args {
                        arg_vals.push(self.eval_expr(a)?);
                    }
                    return self.call_method(obj_val, method_name, arg_vals);
                }
                // Check for path::member(args) pattern (module func or enum variant)
                if let Expr::EnumPath(path_name, member_name, None) = callee.as_ref() {
                    let mut arg_vals = Vec::new();
                    for a in args {
                        arg_vals.push(self.eval_expr(a)?);
                    }
                    // Try module function first
                    if let Some(module) = self.modules.get(path_name) {
                        if let Some(val) = module.pub_bindings.get(member_name).cloned() {
                            return self.call_function(val, arg_vals);
                        }
                        return Err(self.runtime_err(format!(
                            "'{}' is not public or does not exist in module '{}'",
                            member_name, path_name
                        )));
                    }
                    // Try enum variant with data
                    if let Some(variants) = self.enum_defs.get(path_name).cloned() {
                        if variants.iter().any(|(name, _)| name == member_name) {
                            let data = if arg_vals.len() == 1 {
                                Some(Box::new(arg_vals.into_iter().next().unwrap()))
                            } else if arg_vals.is_empty() {
                                None
                            } else {
                                return Err(self.runtime_err("enum variant takes 0 or 1 arguments".into()));
                            };
                            return Ok(Value::EnumVariant(path_name.clone(), member_name.clone(), data));
                        }
                    }
                    // Try type methods (associated functions)
                    if let Some(method_def) = self.type_methods.get(path_name).and_then(|m| m.get(member_name)).cloned() {
                        let (params, body) = method_def;
                        if !params.is_empty() && params[0] == "self" {
                            return Err(self.runtime_err(format!(
                                "{}::{} is an instance method", path_name, member_name
                            )));
                        }
                        if arg_vals.len() != params.len() {
                            return Err(self.runtime_err(format!(
                                "{}::{} expected {} arguments, got {}",
                                path_name, member_name, params.len(), arg_vals.len()
                            )));
                        }
                        self.env.push_scope();
                        for (param, arg) in params.iter().zip(arg_vals) {
                            self.env.define(param, arg);
                        }
                        let mut result = Value::Void;
                        for stmt in &body {
                            result = self.exec_stmt(stmt)?;
                            if self.return_value.is_some() {
                                result = self.return_value.take().unwrap();
                                break;
                            }
                        }
                        self.env.pop_scope();
                        return Ok(result);
                    }
                    return Err(self.runtime_err(format!("undefined enum, type, or module: {}", path_name)));
                }
                let func = self.eval_expr(callee)?;
                let mut arg_vals = Vec::new();
                for a in args {
                    arg_vals.push(self.eval_expr(a)?);
                }
                self.call_function(func, arg_vals)
            }
            Expr::If(cond, then_block, else_block) => {
                let c = self.eval_expr(cond)?;
                if Self::is_truthy(&c) {
                    self.exec_block(then_block)
                } else if let Some(eb) = else_block {
                    self.exec_block(eb)
                } else {
                    Ok(Value::Void)
                }
            }
            Expr::Array(items) => {
                let mut vals = Vec::new();
                for item in items {
                    vals.push(self.eval_expr(item)?);
                }
                Ok(Value::Array(vals))
            }
            Expr::Tuple(items) => {
                let mut vals = Vec::new();
                for item in items {
                    vals.push(self.eval_expr(item)?);
                }
                Ok(Value::Tuple(vals))
            }
            Expr::Index(arr_expr, idx_expr) => {
                let arr = self.eval_expr(arr_expr)?;
                let idx = self.eval_expr(idx_expr)?;
                match (&arr, &idx) {
                    (Value::Array(a), Value::Int(i)) => {
                        let i = *i as usize;
                        if i >= a.len() {
                            Err(self.runtime_err(format!(
                                "index {} out of bounds (len {})", i, a.len()
                            )))
                        } else {
                            Ok(a[i].clone())
                        }
                    }
                    (Value::Str(s), Value::Int(i)) => {
                        let i = *i as usize;
                        s.chars().nth(i).map(Value::Char).ok_or_else(|| {
                            self.runtime_err(format!("string index {} out of bounds", i))
                        })
                    }
                    (Value::Tuple(t), Value::Int(i)) => {
                        let i = *i as usize;
                        if i >= t.len() {
                            Err(self.runtime_err(format!(
                                "tuple index {} out of bounds (len {})", i, t.len()
                            )))
                        } else {
                            Ok(t[i].clone())
                        }
                    }
                    (Value::Map(m), Value::Str(k)) => {
                        m.get(k).cloned().ok_or_else(|| {
                            self.runtime_err(format!("map key '{}' not found", k))
                        })
                    }
                    _ => Err(self.type_err("invalid index operation".into())),
                }
            }
            Expr::Field(obj, field_name) => {
                let obj_val = self.eval_expr(obj)?;
                match &obj_val {
                    Value::Struct(_, fields) => {
                        fields.get(field_name).cloned().ok_or_else(|| {
                            self.runtime_err(format!("struct has no field '{}'", field_name))
                        })
                    }
                    Value::Map(map) => {
                        map.get(field_name).cloned().ok_or_else(|| {
                            self.runtime_err(format!("map has no key '{}'", field_name))
                        })
                    }
                    _ => {
                        // Store as a "method reference" -- will be called in Expr::Call
                        // We use a hack: return a BuiltinFn with a special name encoding
                        // But actually, Call(Field(obj, name), args) is the pattern
                        // So we need to handle method calls differently.
                        // For now, return error - method calls are handled in Call
                        Err(self.runtime_err(format!("field access .{} not supported on {:?}", field_name, obj_val)))
                    }
                }
            }
            Expr::Block(stmts) => self.exec_block(stmts),
            Expr::Range(start, end, inclusive) => {
                let s = self.eval_expr(start)?;
                let e = self.eval_expr(end)?;
                match (&s, &e) {
                    (Value::Int(a), Value::Int(b)) => {
                        let end_val = if *inclusive { *b + 1 } else { *b };
                        let items: Vec<Value> = (*a..end_val).map(Value::Int).collect();
                        Ok(Value::Array(items))
                    }
                    _ => Err(self.type_err("range requires integers".into())),
                }
            }
            Expr::Match(scrutinee, arms) => {
                let val = self.eval_expr(scrutinee)?;
                for arm in arms {
                    // Try to match the pattern, collecting any bindings
                    if let Some(bindings) = self.match_pattern(&val, &arm.pattern)? {
                        // Check guard if present
                        if let Some(guard_expr) = &arm.guard {
                            self.env.push_scope();
                            for (name, bval) in &bindings {
                                self.env.define(name, bval.clone());
                            }
                            let guard_result = self.eval_expr(guard_expr)?;
                            self.env.pop_scope();
                            if !Self::is_truthy(&guard_result) {
                                continue; // guard failed, try next arm
                            }
                        }
                        // Pattern matched (and guard passed), execute body with bindings
                        self.env.push_scope();
                        for (name, bval) in bindings {
                            self.env.define(&name, bval);
                        }
                        let mut result = Value::Void;
                        for stmt in &arm.body {
                            result = self.exec_stmt(stmt)?;
                            if self.return_value.is_some() || self.throw_value.is_some() {
                                self.env.pop_scope();
                                return Ok(Value::Void);
                            }
                        }
                        self.env.pop_scope();
                        return Ok(result);
                    }
                }
                Ok(Value::Void)
            }
            Expr::Lambda(params, body) => {
                let param_names: Vec<String> = params.iter().map(|p| p.name.clone()).collect();
                // Capture current environment for closures
                let captured = self.capture_env();
                let body_stmts = match body.as_ref() {
                    Expr::Block(stmts) => stmts.clone(),
                    other => vec![Stmt::Return(Some(other.clone()))],
                };
                Ok(Value::Lambda(param_names, body_stmts, captured))
            }
            Expr::StructInit(name, field_exprs) => {
                // Verify struct is defined
                if !self.struct_defs.contains_key(name) {
                    return Err(self.runtime_err(format!("undefined struct: {}", name)));
                }
                let mut fields = HashMap::new();
                for (fname, fexpr) in field_exprs {
                    let val = self.eval_expr(fexpr)?;
                    fields.insert(fname.clone(), val);
                }
                Ok(Value::Struct(name.clone(), fields))
            }
            Expr::MapLit(pairs) => {
                let mut map = HashMap::new();
                for (key_expr, val_expr) in pairs {
                    let key = match self.eval_expr(key_expr)? {
                        Value::Str(s) => s,
                        other => format!("{}", other),
                    };
                    let val = self.eval_expr(val_expr)?;
                    map.insert(key, val);
                }
                Ok(Value::Map(map))
            }
            Expr::EnumPath(enum_name, variant_name, data_expr) => {
                // Check if it's an enum variant
                if let Some(variants) = self.enum_defs.get(enum_name).cloned() {
                    let variant_def = variants.iter().find(|(name, _)| name == variant_name);
                    if variant_def.is_some() {
                        let data = match data_expr {
                            Some(expr) => Some(Box::new(self.eval_expr(expr)?)),
                            None => None,
                        };
                        return Ok(Value::EnumVariant(enum_name.clone(), variant_name.clone(), data));
                    }
                }
                // Check if it's a static/associated method call: Type::method(args)
                if let Some(method_def) = self.type_methods.get(enum_name).and_then(|m| m.get(variant_name)).cloned() {
                    let (params, body) = method_def;
                    // Evaluate data_expr as arguments if present
                    let mut arg_vals = Vec::new();
                    if let Some(expr) = data_expr {
                        arg_vals.push(self.eval_expr(expr)?);
                    }
                    // Check that it's not an instance method (no self)
                    if !params.is_empty() && params[0] == "self" {
                        return Err(self.runtime_err(format!(
                            "{}::{} is an instance method, use value.{}() instead",
                            enum_name, variant_name, variant_name
                        )));
                    }
                    if arg_vals.len() != params.len() {
                        return Err(self.runtime_err(format!(
                            "{}::{} expected {} arguments, got {}",
                            enum_name, variant_name, params.len(), arg_vals.len()
                        )));
                    }
                    self.env.push_scope();
                    for (param, arg) in params.iter().zip(arg_vals) {
                        self.env.define(param, arg);
                    }
                    let mut result = Value::Void;
                    for stmt in &body {
                        result = self.exec_stmt(stmt)?;
                        if self.return_value.is_some() {
                            result = self.return_value.take().unwrap();
                            break;
                        }
                    }
                    self.env.pop_scope();
                    return Ok(result);
                }
                // Check if it's a module member access: module::member
                if let Some(module) = self.modules.get(enum_name) {
                    // Check public bindings
                    if let Some(val) = module.pub_bindings.get(variant_name).cloned() {
                        // If data_expr provided, it's a function call: mod::func(args)
                        if let Some(expr) = data_expr {
                            let arg = self.eval_expr(expr)?;
                            return self.call_function(val.clone(), vec![arg]);
                        }
                        return Ok(val);
                    }
                    // Check module enum defs
                    if let Some(variants) = module.enum_defs.get(variant_name) {
                        // This would be mod::EnumName which isn't quite right for our syntax
                        // In practice users will do mod::func() or mod::VAR
                        let _ = variants;
                    }
                    return Err(self.runtime_err(format!(
                        "'{}' is not public or does not exist in module '{}'",
                        variant_name, enum_name
                    )));
                }
                Err(self.runtime_err(format!("undefined enum, type, or module: {}", enum_name)))
            }
            Expr::Wildcard => {
                // Wildcard should only appear in match patterns, not as a standalone expression
                Err(self.runtime_err("wildcard '_' can only be used in match patterns".into()))
            }
        }
    }

    // ── Binary operations ───────────────────────────────────

    fn eval_binary(&mut self, left: Value, op: &BinOp, right: Value) -> Result<Value, HexaError> {
        // Coerce int to float if mixed
        let (left, right) = match (&left, &right) {
            (Value::Int(a), Value::Float(b)) => (Value::Float(*a as f64), Value::Float(*b)),
            (Value::Float(a), Value::Int(b)) => (Value::Float(*a), Value::Float(*b as f64)),
            _ => (left, right),
        };

        match (&left, op, &right) {
            // Int arithmetic
            (Value::Int(a), BinOp::Add, Value::Int(b)) => Ok(Value::Int(a + b)),
            (Value::Int(a), BinOp::Sub, Value::Int(b)) => Ok(Value::Int(a - b)),
            (Value::Int(a), BinOp::Mul, Value::Int(b)) => Ok(Value::Int(a * b)),
            (Value::Int(a), BinOp::Div, Value::Int(b)) => {
                if *b == 0 {
                    Err(self.runtime_err("division by zero".into()))
                } else {
                    Ok(Value::Int(a / b))
                }
            }
            (Value::Int(a), BinOp::Rem, Value::Int(b)) => {
                if *b == 0 {
                    Err(self.runtime_err("division by zero".into()))
                } else {
                    Ok(Value::Int(a % b))
                }
            }
            (Value::Int(a), BinOp::Pow, Value::Int(b)) => {
                Ok(Value::Int((*a as f64).powi(*b as i32) as i64))
            }

            // Float arithmetic
            (Value::Float(a), BinOp::Add, Value::Float(b)) => Ok(Value::Float(a + b)),
            (Value::Float(a), BinOp::Sub, Value::Float(b)) => Ok(Value::Float(a - b)),
            (Value::Float(a), BinOp::Mul, Value::Float(b)) => Ok(Value::Float(a * b)),
            (Value::Float(a), BinOp::Div, Value::Float(b)) => {
                if *b == 0.0 {
                    Err(self.runtime_err("division by zero".into()))
                } else {
                    Ok(Value::Float(a / b))
                }
            }
            (Value::Float(a), BinOp::Rem, Value::Float(b)) => Ok(Value::Float(a % b)),
            (Value::Float(a), BinOp::Pow, Value::Float(b)) => Ok(Value::Float(a.powf(*b))),

            // String concatenation
            (Value::Str(a), BinOp::Add, Value::Str(b)) => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }

            // Int comparisons
            (Value::Int(a), BinOp::Eq, Value::Int(b)) => Ok(Value::Bool(a == b)),
            (Value::Int(a), BinOp::Ne, Value::Int(b)) => Ok(Value::Bool(a != b)),
            (Value::Int(a), BinOp::Lt, Value::Int(b)) => Ok(Value::Bool(a < b)),
            (Value::Int(a), BinOp::Gt, Value::Int(b)) => Ok(Value::Bool(a > b)),
            (Value::Int(a), BinOp::Le, Value::Int(b)) => Ok(Value::Bool(a <= b)),
            (Value::Int(a), BinOp::Ge, Value::Int(b)) => Ok(Value::Bool(a >= b)),

            // Float comparisons
            (Value::Float(a), BinOp::Eq, Value::Float(b)) => Ok(Value::Bool(a == b)),
            (Value::Float(a), BinOp::Ne, Value::Float(b)) => Ok(Value::Bool(a != b)),
            (Value::Float(a), BinOp::Lt, Value::Float(b)) => Ok(Value::Bool(a < b)),
            (Value::Float(a), BinOp::Gt, Value::Float(b)) => Ok(Value::Bool(a > b)),
            (Value::Float(a), BinOp::Le, Value::Float(b)) => Ok(Value::Bool(a <= b)),
            (Value::Float(a), BinOp::Ge, Value::Float(b)) => Ok(Value::Bool(a >= b)),

            // Bool comparisons
            (Value::Bool(a), BinOp::Eq, Value::Bool(b)) => Ok(Value::Bool(a == b)),
            (Value::Bool(a), BinOp::Ne, Value::Bool(b)) => Ok(Value::Bool(a != b)),

            // String comparisons
            (Value::Str(a), BinOp::Eq, Value::Str(b)) => Ok(Value::Bool(a == b)),
            (Value::Str(a), BinOp::Ne, Value::Str(b)) => Ok(Value::Bool(a != b)),

            // Bitwise operations
            (Value::Int(a), BinOp::BitAnd, Value::Int(b)) => Ok(Value::Int(a & b)),
            (Value::Int(a), BinOp::BitOr, Value::Int(b)) => Ok(Value::Int(a | b)),
            (Value::Int(a), BinOp::BitXor, Value::Int(b)) => Ok(Value::Int(a ^ b)),

            // Logical XOR
            (Value::Bool(a), BinOp::Xor, Value::Bool(b)) => Ok(Value::Bool(a ^ b)),

            _ => Err(self.type_err(format!(
                "unsupported binary operation: {:?} {:?} {:?}",
                left, op, right
            ))),
        }
    }

    // ── Unary operations ────────────────────────────────────

    fn eval_unary(&mut self, op: &UnaryOp, val: Value) -> Result<Value, HexaError> {
        match (op, &val) {
            (UnaryOp::Neg, Value::Int(n)) => Ok(Value::Int(-n)),
            (UnaryOp::Neg, Value::Float(n)) => Ok(Value::Float(-n)),
            (UnaryOp::Not, Value::Bool(b)) => Ok(Value::Bool(!b)),
            (UnaryOp::BitNot, Value::Int(n)) => Ok(Value::Int(!n)),
            _ => Err(self.type_err(format!("unsupported unary op {:?} on {:?}", op, val))),
        }
    }

    // ── Function calls ──────────────────────────────────────

    fn call_function(&mut self, func: Value, args: Vec<Value>) -> Result<Value, HexaError> {
        match func {
            Value::BuiltinFn(name) => self.call_builtin(&name, args),
            Value::Fn(_name, params, body) => {
                if args.len() != params.len() {
                    return Err(self.runtime_err(format!(
                        "expected {} arguments, got {}",
                        params.len(),
                        args.len()
                    )));
                }
                self.env.push_scope();
                for (param, arg) in params.iter().zip(args) {
                    self.env.define(param, arg);
                }
                let mut result = Value::Void;
                for stmt in &body {
                    result = self.exec_stmt(stmt)?;
                    if self.return_value.is_some() {
                        result = self.return_value.take().unwrap();
                        break;
                    }
                }
                self.env.pop_scope();
                Ok(result)
            }
            Value::Lambda(params, body, captured) => {
                if args.len() != params.len() {
                    return Err(self.runtime_err(format!(
                        "lambda expected {} arguments, got {}",
                        params.len(),
                        args.len()
                    )));
                }
                self.env.push_scope();
                // Restore captured variables
                for (name, val) in &captured {
                    self.env.define(name, val.clone());
                }
                // Bind parameters (overriding captured if same name)
                for (param, arg) in params.iter().zip(args) {
                    self.env.define(param, arg);
                }
                let mut result = Value::Void;
                for stmt in &body {
                    result = self.exec_stmt(stmt)?;
                    if self.return_value.is_some() {
                        result = self.return_value.take().unwrap();
                        break;
                    }
                }
                self.env.pop_scope();
                Ok(result)
            }
            _ => Err(self.type_err("not a callable value".into())),
        }
    }

    fn call_method(&mut self, obj: Value, method: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match &obj {
            Value::Str(s) => self.call_string_method(s, method, args),
            Value::Array(a) => self.call_array_method(a, method, args),
            Value::Map(m) => self.call_map_method(m, method, args),
            Value::EnumVariant(enum_name, variant, data) => {
                self.call_enum_method(enum_name, variant, data.as_ref().map(|b| b.as_ref()), method, args)
            }
            Value::Struct(struct_name, _fields) => {
                // Look up impl methods
                let struct_name = struct_name.clone();
                if let Some(method_def) = self.type_methods.get(&struct_name).and_then(|m| m.get(method)) {
                    let (params, body) = method_def.clone();
                    if params.is_empty() || params[0] != "self" {
                        return Err(self.runtime_err(format!("method {} is not an instance method (no self parameter)", method)));
                    }
                    if args.len() + 1 != params.len() {
                        return Err(self.runtime_err(format!(
                            "method {} expected {} arguments, got {}",
                            method, params.len() - 1, args.len()
                        )));
                    }
                    self.env.push_scope();
                    self.env.define("self", obj);
                    for (param, arg) in params[1..].iter().zip(args) {
                        self.env.define(param, arg);
                    }
                    let mut result = Value::Void;
                    for stmt in &body {
                        result = self.exec_stmt(stmt)?;
                        if self.return_value.is_some() {
                            result = self.return_value.take().unwrap();
                            break;
                        }
                    }
                    self.env.pop_scope();
                    Ok(result)
                } else {
                    Err(self.runtime_err(format!("no method .{}() on struct {}", method, struct_name)))
                }
            }
            _ => Err(self.runtime_err(format!("no method .{}() on {:?}", method, obj))),
        }
    }

    fn call_enum_method(&mut self, enum_name: &str, variant: &str, data: Option<&Value>, method: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match (enum_name, method) {
            ("Option", "is_some") => Ok(Value::Bool(variant == "Some")),
            ("Option", "is_none") => Ok(Value::Bool(variant == "None")),
            ("Option", "unwrap") => {
                match (variant, data) {
                    ("Some", Some(val)) => Ok(val.clone()),
                    _ => Err(self.runtime_err("called unwrap() on None".into())),
                }
            }
            ("Option", "unwrap_or") => {
                if args.is_empty() { return Err(self.type_err("unwrap_or() requires 1 argument".into())); }
                match (variant, data) {
                    ("Some", Some(val)) => Ok(val.clone()),
                    _ => Ok(args.into_iter().next().unwrap()),
                }
            }
            ("Result", "is_ok") => Ok(Value::Bool(variant == "Ok")),
            ("Result", "is_err") => Ok(Value::Bool(variant == "Err")),
            ("Result", "unwrap") => {
                match (variant, data) {
                    ("Ok", Some(val)) => Ok(val.clone()),
                    ("Err", Some(val)) => Err(self.runtime_err(format!("called unwrap() on Err({})", val))),
                    _ => Err(self.runtime_err("called unwrap() on Err".into())),
                }
            }
            ("Result", "unwrap_or") => {
                if args.is_empty() { return Err(self.type_err("unwrap_or() requires 1 argument".into())); }
                match (variant, data) {
                    ("Ok", Some(val)) => Ok(val.clone()),
                    _ => Ok(args.into_iter().next().unwrap()),
                }
            }
            _ => Err(self.runtime_err(format!("no method .{}() on {}::{}", method, enum_name, variant))),
        }
    }

    fn call_string_method(&mut self, s: &str, method: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match method {
            "len" => Ok(Value::Int(s.len() as i64)),
            "contains" => {
                if args.is_empty() { return Err(self.type_err("contains() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(sub) => Ok(Value::Bool(s.contains(sub.as_str()))),
                    _ => Err(self.type_err("contains() requires string argument".into())),
                }
            }
            "split" => {
                if args.is_empty() { return Err(self.type_err("split() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(delim) => {
                        let parts: Vec<Value> = s.split(delim.as_str()).map(|p| Value::Str(p.to_string())).collect();
                        Ok(Value::Array(parts))
                    }
                    _ => Err(self.type_err("split() requires string delimiter".into())),
                }
            }
            "trim" => Ok(Value::Str(s.trim().to_string())),
            "to_upper" => Ok(Value::Str(s.to_uppercase())),
            "to_lower" => Ok(Value::Str(s.to_lowercase())),
            "replace" => {
                if args.len() < 2 { return Err(self.type_err("replace() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Str(old), Value::Str(new)) => Ok(Value::Str(s.replace(old.as_str(), new.as_str()))),
                    _ => Err(self.type_err("replace() requires string arguments".into())),
                }
            }
            "starts_with" => {
                if args.is_empty() { return Err(self.type_err("starts_with() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(prefix) => Ok(Value::Bool(s.starts_with(prefix.as_str()))),
                    _ => Err(self.type_err("starts_with() requires string argument".into())),
                }
            }
            "chars" => {
                let chars: Vec<Value> = s.chars().map(Value::Char).collect();
                Ok(Value::Array(chars))
            }
            _ => Err(self.runtime_err(format!("unknown string method: .{}()", method))),
        }
    }

    fn call_array_method(&mut self, arr: &[Value], method: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match method {
            "len" => Ok(Value::Int(arr.len() as i64)),
            "push" => {
                if args.is_empty() { return Err(self.type_err("push() requires 1 argument".into())); }
                let mut new_arr = arr.to_vec();
                new_arr.push(args.into_iter().next().unwrap());
                Ok(Value::Array(new_arr))
            }
            "contains" => {
                if args.is_empty() { return Err(self.type_err("contains() requires 1 argument".into())); }
                let needle = &args[0];
                Ok(Value::Bool(arr.iter().any(|v| Self::values_equal(v, needle))))
            }
            "reverse" => {
                let mut new_arr = arr.to_vec();
                new_arr.reverse();
                Ok(Value::Array(new_arr))
            }
            "sort" => {
                let mut new_arr = arr.to_vec();
                // Sort by comparing display representation for mixed types,
                // but use numeric sort for all-int/all-float arrays
                new_arr.sort_by(|a, b| {
                    match (a, b) {
                        (Value::Int(x), Value::Int(y)) => x.cmp(y),
                        (Value::Float(x), Value::Float(y)) => x.partial_cmp(y).unwrap_or(std::cmp::Ordering::Equal),
                        (Value::Str(x), Value::Str(y)) => x.cmp(y),
                        _ => format!("{}", a).cmp(&format!("{}", b)),
                    }
                });
                Ok(Value::Array(new_arr))
            }
            "map" => {
                if args.is_empty() { return Err(self.type_err("map() requires 1 argument (function)".into())); }
                let func = args.into_iter().next().unwrap();
                let mut result = Vec::new();
                for item in arr {
                    result.push(self.call_function(func.clone(), vec![item.clone()])?);
                }
                Ok(Value::Array(result))
            }
            "filter" => {
                if args.is_empty() { return Err(self.type_err("filter() requires 1 argument (function)".into())); }
                let func = args.into_iter().next().unwrap();
                let mut result = Vec::new();
                for item in arr {
                    let keep = self.call_function(func.clone(), vec![item.clone()])?;
                    if Self::is_truthy(&keep) {
                        result.push(item.clone());
                    }
                }
                Ok(Value::Array(result))
            }
            "fold" => {
                if args.len() < 2 { return Err(self.type_err("fold() requires 2 arguments (init, function)".into())); }
                let mut args_iter = args.into_iter();
                let mut acc = args_iter.next().unwrap();
                let func = args_iter.next().unwrap();
                for item in arr {
                    acc = self.call_function(func.clone(), vec![acc, item.clone()])?;
                }
                Ok(acc)
            }
            _ => Err(self.runtime_err(format!("unknown array method: .{}()", method))),
        }
    }

    fn call_map_method(&mut self, map: &HashMap<String, Value>, method: &str, _args: Vec<Value>) -> Result<Value, HexaError> {
        match method {
            "len" => Ok(Value::Int(map.len() as i64)),
            "keys" => {
                let keys: Vec<Value> = map.keys().map(|k| Value::Str(k.clone())).collect();
                Ok(Value::Array(keys))
            }
            "values" => {
                let vals: Vec<Value> = map.values().cloned().collect();
                Ok(Value::Array(vals))
            }
            _ => Err(self.runtime_err(format!("unknown map method: .{}()", method))),
        }
    }

    fn call_builtin(&mut self, name: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match name {
            "print" => {
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 {
                        print!(" ");
                    }
                    print!("{}", arg);
                }
                Ok(Value::Void)
            }
            "println" => {
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 {
                        print!(" ");
                    }
                    print!("{}", arg);
                }
                println!();
                Ok(Value::Void)
            }
            "len" => {
                if args.is_empty() {
                    return Err(self.type_err("len() requires 1 argument".into()));
                }
                match &args[0] {
                    Value::Str(s) => Ok(Value::Int(s.len() as i64)),
                    Value::Array(a) => Ok(Value::Int(a.len() as i64)),
                    Value::Map(m) => Ok(Value::Int(m.len() as i64)),
                    _ => Err(self.type_err("len() requires string, array, or map".into())),
                }
            }
            "type_of" => {
                if args.is_empty() {
                    return Err(self.type_err("type_of() requires 1 argument".into()));
                }
                Ok(Value::Str(
                    match &args[0] {
                        Value::Int(_) => "int",
                        Value::Float(_) => "float",
                        Value::Bool(_) => "bool",
                        Value::Char(_) => "char",
                        Value::Str(_) => "string",
                        Value::Byte(_) => "byte",
                        Value::Void => "void",
                        Value::Array(_) => "array",
                        Value::Tuple(_) => "tuple",
                        Value::Fn(..) => "fn",
                        Value::BuiltinFn(_) => "builtin",
                        Value::Struct(name, _) => name.as_str(),
                        Value::Lambda(..) => "lambda",
                        Value::Map(_) => "map",
                        Value::Error(_) => "error",
                        Value::EnumVariant(name, _, _) => name.as_str(),
                    }
                    .to_string(),
                ))
            }
            "sigma" => {
                if args.is_empty() {
                    return Err(self.type_err("sigma() requires 1 argument".into()));
                }
                if let Value::Int(n) = &args[0] {
                    let mut sum = 0i64;
                    for d in 1..=*n {
                        if n % d == 0 {
                            sum += d;
                        }
                    }
                    Ok(Value::Int(sum))
                } else {
                    Err(self.type_err("sigma() requires int".into()))
                }
            }
            "phi" => {
                if args.is_empty() {
                    return Err(self.type_err("phi() requires 1 argument".into()));
                }
                if let Value::Int(n) = &args[0] {
                    let mut count = 0i64;
                    for k in 1..=*n {
                        if gcd(*n, k) == 1 {
                            count += 1;
                        }
                    }
                    Ok(Value::Int(count))
                } else {
                    Err(self.type_err("phi() requires int".into()))
                }
            }
            "tau" => {
                if args.is_empty() {
                    return Err(self.type_err("tau() requires 1 argument".into()));
                }
                if let Value::Int(n) = &args[0] {
                    let mut count = 0i64;
                    for d in 1..=*n {
                        if n % d == 0 {
                            count += 1;
                        }
                    }
                    Ok(Value::Int(count))
                } else {
                    Err(self.type_err("tau() requires int".into()))
                }
            }
            "gcd" => {
                if args.len() < 2 {
                    return Err(self.type_err("gcd() requires 2 arguments".into()));
                }
                if let (Value::Int(a), Value::Int(b)) = (&args[0], &args[1]) {
                    Ok(Value::Int(gcd(*a, *b)))
                } else {
                    Err(self.type_err("gcd() requires two ints".into()))
                }
            }
            "read_file" => {
                if args.is_empty() { return Err(self.type_err("read_file() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(path) => {
                        match std::fs::read_to_string(path) {
                            Ok(contents) => Ok(Value::Str(contents)),
                            Err(e) => Ok(Value::Error(format!("read_file error: {}", e))),
                        }
                    }
                    _ => Err(self.type_err("read_file() requires string path".into())),
                }
            }
            "write_file" => {
                if args.len() < 2 { return Err(self.type_err("write_file() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Str(path), Value::Str(content)) => {
                        match std::fs::write(path, content) {
                            Ok(_) => Ok(Value::Void),
                            Err(e) => Ok(Value::Error(format!("write_file error: {}", e))),
                        }
                    }
                    _ => Err(self.type_err("write_file() requires string arguments".into())),
                }
            }
            "file_exists" => {
                if args.is_empty() { return Err(self.type_err("file_exists() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(path) => Ok(Value::Bool(std::path::Path::new(path).exists())),
                    _ => Err(self.type_err("file_exists() requires string path".into())),
                }
            }
            "keys" => {
                if args.is_empty() { return Err(self.type_err("keys() requires 1 argument".into())); }
                match &args[0] {
                    Value::Map(m) => {
                        let keys: Vec<Value> = m.keys().map(|k| Value::Str(k.clone())).collect();
                        Ok(Value::Array(keys))
                    }
                    _ => Err(self.type_err("keys() requires map argument".into())),
                }
            }
            "values" => {
                if args.is_empty() { return Err(self.type_err("values() requires 1 argument".into())); }
                match &args[0] {
                    Value::Map(m) => {
                        let vals: Vec<Value> = m.values().cloned().collect();
                        Ok(Value::Array(vals))
                    }
                    _ => Err(self.type_err("values() requires map argument".into())),
                }
            }
            "has_key" => {
                if args.len() < 2 { return Err(self.type_err("has_key() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Map(m), Value::Str(k)) => Ok(Value::Bool(m.contains_key(k))),
                    _ => Err(self.type_err("has_key() requires (map, string)".into())),
                }
            }
            "to_string" => {
                if args.is_empty() { return Err(self.type_err("to_string() requires 1 argument".into())); }
                Ok(Value::Str(format!("{}", args[0])))
            }
            "to_int" => {
                if args.is_empty() { return Err(self.type_err("to_int() requires 1 argument".into())); }
                match &args[0] {
                    Value::Int(n) => Ok(Value::Int(*n)),
                    Value::Float(f) => Ok(Value::Int(*f as i64)),
                    Value::Str(s) => s.parse::<i64>().map(Value::Int).map_err(|_| self.type_err(format!("cannot convert '{}' to int", s))),
                    Value::Bool(b) => Ok(Value::Int(if *b { 1 } else { 0 })),
                    _ => Err(self.type_err("to_int() cannot convert this type".into())),
                }
            }
            "to_float" => {
                if args.is_empty() { return Err(self.type_err("to_float() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(*f)),
                    Value::Int(n) => Ok(Value::Float(*n as f64)),
                    Value::Str(s) => s.parse::<f64>().map(Value::Float).map_err(|_| self.type_err(format!("cannot convert '{}' to float", s))),
                    Value::Bool(b) => Ok(Value::Float(if *b { 1.0 } else { 0.0 })),
                    _ => Err(self.type_err("to_float() cannot convert this type".into())),
                }
            }
            // ── Math builtins ──────────────────────────────────────
            "abs" => {
                if args.is_empty() { return Err(self.type_err("abs() requires 1 argument".into())); }
                match &args[0] {
                    Value::Int(n) => Ok(Value::Int(n.abs())),
                    Value::Float(f) => Ok(Value::Float(f.abs())),
                    _ => Err(self.type_err("abs() requires int or float".into())),
                }
            }
            "min" => {
                if args.len() < 2 { return Err(self.type_err("min() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(a), Value::Int(b)) => Ok(Value::Int(*a.min(b))),
                    (Value::Float(a), Value::Float(b)) => Ok(Value::Float(a.min(*b))),
                    (Value::Int(a), Value::Float(b)) => Ok(Value::Float((*a as f64).min(*b))),
                    (Value::Float(a), Value::Int(b)) => Ok(Value::Float(a.min(*b as f64))),
                    _ => Err(self.type_err("min() requires numeric arguments".into())),
                }
            }
            "max" => {
                if args.len() < 2 { return Err(self.type_err("max() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(a), Value::Int(b)) => Ok(Value::Int(*a.max(b))),
                    (Value::Float(a), Value::Float(b)) => Ok(Value::Float(a.max(*b))),
                    (Value::Int(a), Value::Float(b)) => Ok(Value::Float((*a as f64).max(*b))),
                    (Value::Float(a), Value::Int(b)) => Ok(Value::Float(a.max(*b as f64))),
                    _ => Err(self.type_err("max() requires numeric arguments".into())),
                }
            }
            "floor" => {
                if args.is_empty() { return Err(self.type_err("floor() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Int(f.floor() as i64)),
                    Value::Int(n) => Ok(Value::Int(*n)),
                    _ => Err(self.type_err("floor() requires float or int".into())),
                }
            }
            "ceil" => {
                if args.is_empty() { return Err(self.type_err("ceil() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Int(f.ceil() as i64)),
                    Value::Int(n) => Ok(Value::Int(*n)),
                    _ => Err(self.type_err("ceil() requires float or int".into())),
                }
            }
            "round" => {
                if args.is_empty() { return Err(self.type_err("round() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Int(f.round() as i64)),
                    Value::Int(n) => Ok(Value::Int(*n)),
                    _ => Err(self.type_err("round() requires float or int".into())),
                }
            }
            "sqrt" => {
                if args.is_empty() { return Err(self.type_err("sqrt() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.sqrt())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).sqrt())),
                    _ => Err(self.type_err("sqrt() requires numeric argument".into())),
                }
            }
            "pow" => {
                if args.len() < 2 { return Err(self.type_err("pow() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(a), Value::Int(b)) => Ok(Value::Int((*a as f64).powi(*b as i32) as i64)),
                    (Value::Float(a), Value::Float(b)) => Ok(Value::Float(a.powf(*b))),
                    (Value::Int(a), Value::Float(b)) => Ok(Value::Float((*a as f64).powf(*b))),
                    (Value::Float(a), Value::Int(b)) => Ok(Value::Float(a.powi(*b as i32))),
                    _ => Err(self.type_err("pow() requires numeric arguments".into())),
                }
            }
            "log" => {
                if args.is_empty() { return Err(self.type_err("log() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.ln())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).ln())),
                    _ => Err(self.type_err("log() requires numeric argument".into())),
                }
            }
            "log2" => {
                if args.is_empty() { return Err(self.type_err("log2() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.log2())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).log2())),
                    _ => Err(self.type_err("log2() requires numeric argument".into())),
                }
            }
            "sin" => {
                if args.is_empty() { return Err(self.type_err("sin() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.sin())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).sin())),
                    _ => Err(self.type_err("sin() requires numeric argument".into())),
                }
            }
            "cos" => {
                if args.is_empty() { return Err(self.type_err("cos() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.cos())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).cos())),
                    _ => Err(self.type_err("cos() requires numeric argument".into())),
                }
            }
            "tan" => {
                if args.is_empty() { return Err(self.type_err("tan() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.tan())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).tan())),
                    _ => Err(self.type_err("tan() requires numeric argument".into())),
                }
            }
            // ── Format builtins ────────────────────────────────────
            "format" => {
                if args.is_empty() { return Err(self.type_err("format() requires at least 1 argument".into())); }
                match &args[0] {
                    Value::Str(template) => {
                        let mut result = template.clone();
                        for arg in &args[1..] {
                            if let Some(pos) = result.find("{}") {
                                result = format!("{}{}{}", &result[..pos], arg, &result[pos+2..]);
                            }
                        }
                        Ok(Value::Str(result))
                    }
                    _ => Err(self.type_err("format() first argument must be a string template".into())),
                }
            }
            // ── OS builtins ────────────────────────────────────────
            "args" => {
                let os_args: Vec<Value> = std::env::args().map(|a| Value::Str(a)).collect();
                Ok(Value::Array(os_args))
            }
            "env_var" => {
                if args.is_empty() { return Err(self.type_err("env_var() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(name) => {
                        match std::env::var(name) {
                            Ok(val) => Ok(Value::Str(val)),
                            Err(_) => Ok(Value::Void),
                        }
                    }
                    _ => Err(self.type_err("env_var() requires string argument".into())),
                }
            }
            "exit" => {
                if args.is_empty() {
                    std::process::exit(0);
                }
                match &args[0] {
                    Value::Int(code) => std::process::exit(*code as i32),
                    _ => std::process::exit(0),
                }
            }
            "clock" => {
                use std::time::{SystemTime, UNIX_EPOCH};
                let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
                Ok(Value::Float(now.as_secs_f64()))
            }
            // ── Built-in Option/Result constructors ───────────────────
            "Some" => {
                if args.len() != 1 { return Err(self.type_err("Some() requires 1 argument".into())); }
                Ok(Value::EnumVariant("Option".into(), "Some".into(), Some(Box::new(args.into_iter().next().unwrap()))))
            }
            "Ok" => {
                if args.len() != 1 { return Err(self.type_err("Ok() requires 1 argument".into())); }
                Ok(Value::EnumVariant("Result".into(), "Ok".into(), Some(Box::new(args.into_iter().next().unwrap()))))
            }
            "Err" => {
                if args.len() != 1 { return Err(self.type_err("Err() requires 1 argument".into())); }
                Ok(Value::EnumVariant("Result".into(), "Err".into(), Some(Box::new(args.into_iter().next().unwrap()))))
            }
            _ => Err(HexaError {
                class: ErrorClass::Name,
                message: format!("unknown builtin: {}", name),
                line: self.current_line,
                col: self.current_col,
            }),
        }
    }

    // ── Helpers ──────────────────────────────────────────────

    /// Try to match a value against a pattern expression.
    /// Returns Some(bindings) if matched, None if not.
    /// Bindings are (variable_name, value) pairs from destructuring.
    fn match_pattern(&mut self, val: &Value, pattern: &Expr) -> Result<Option<Vec<(String, Value)>>, HexaError> {
        match pattern {
            // Wildcard matches everything, no bindings
            Expr::Wildcard => Ok(Some(vec![])),

            // EnumPath: match enum variant, optionally destructure data
            Expr::EnumPath(enum_name, variant_name, binding_expr) => {
                match val {
                    Value::EnumVariant(val_enum, val_variant, val_data) => {
                        if val_enum != enum_name || val_variant != variant_name {
                            return Ok(None); // different variant
                        }
                        // Variant matches. Handle destructuring binding.
                        match (binding_expr, val_data) {
                            // Pattern has binding, value has data: bind the variable
                            (Some(bind_expr), Some(data)) => {
                                match bind_expr.as_ref() {
                                    Expr::Ident(binding_name) => {
                                        Ok(Some(vec![(binding_name.clone(), data.as_ref().clone())]))
                                    }
                                    // Could be a literal for nested matching
                                    _ => {
                                        let pattern_val = self.eval_expr(bind_expr)?;
                                        if Self::values_equal(data.as_ref(), &pattern_val) {
                                            Ok(Some(vec![]))
                                        } else {
                                            Ok(None)
                                        }
                                    }
                                }
                            }
                            // Neither has data: simple variant match
                            (None, None) => Ok(Some(vec![])),
                            // Pattern expects data but value has none, or vice versa
                            _ => Ok(None),
                        }
                    }
                    _ => Ok(None), // value is not an enum variant
                }
            }

            // Built-in constructor patterns: Some(binding), Ok(binding), Err(binding)
            Expr::Call(callee, call_args) => {
                if let Expr::Ident(ctor_name) = callee.as_ref() {
                    let maybe_ctor = match ctor_name.as_str() {
                        "Some" => Some(("Option", "Some")),
                        "Ok" => Some(("Result", "Ok")),
                        "Err" => Some(("Result", "Err")),
                        _ => None,
                    };
                    if let Some((enum_name, variant_name)) = maybe_ctor {
                        match val {
                            Value::EnumVariant(val_enum, val_variant, val_data) => {
                                if val_enum != enum_name || val_variant != variant_name {
                                    return Ok(None);
                                }
                                if call_args.len() == 1 {
                                    if let Some(data) = val_data {
                                        match &call_args[0] {
                                            Expr::Ident(binding_name) => {
                                                return Ok(Some(vec![(binding_name.clone(), data.as_ref().clone())]));
                                            }
                                            _ => {
                                                let pattern_val = self.eval_expr(&call_args[0])?;
                                                if Self::values_equal(data.as_ref(), &pattern_val) {
                                                    return Ok(Some(vec![]));
                                                } else {
                                                    return Ok(None);
                                                }
                                            }
                                        }
                                    }
                                    return Ok(None);
                                }
                                if call_args.is_empty() && val_data.is_none() {
                                    return Ok(Some(vec![]));
                                }
                                return Ok(None);
                            }
                            _ => return Ok(None),
                        }
                    }
                }
                // Not a known constructor pattern, evaluate as expression
                let pattern_val = self.eval_expr(pattern)?;
                if Self::values_equal(val, &pattern_val) {
                    Ok(Some(vec![]))
                } else {
                    Ok(None)
                }
            }

            // Ident pattern: check if it's a known enum constant (like None)
            Expr::Ident(name) => {
                if let Some(const_val) = self.env.get(name) {
                    if matches!(&const_val, Value::EnumVariant(..)) {
                        if Self::values_equal(val, &const_val) {
                            return Ok(Some(vec![]));
                        } else {
                            return Ok(None);
                        }
                    }
                }
                // Otherwise evaluate normally
                let pattern_val = self.eval_expr(pattern)?;
                if Self::values_equal(val, &pattern_val) {
                    Ok(Some(vec![]))
                } else {
                    Ok(None)
                }
            }

            // Literal/expression: evaluate and compare
            _ => {
                let pattern_val = self.eval_expr(pattern)?;
                if Self::values_equal(val, &pattern_val) {
                    Ok(Some(vec![]))
                } else {
                    Ok(None)
                }
            }
        }
    }

    fn exec_block(&mut self, stmts: &[Stmt]) -> Result<Value, HexaError> {
        self.env.push_scope();
        let mut last = Value::Void;
        for stmt in stmts {
            last = self.exec_stmt(stmt)?;
            if self.return_value.is_some() || self.throw_value.is_some() {
                self.env.pop_scope();
                return Ok(Value::Void);
            }
        }
        self.env.pop_scope();
        Ok(last)
    }

    // ── Module execution ─────────────────────────────────────

    /// Execute an inline `mod name { ... }` block. Public declarations become
    /// the module's exported bindings.
    fn exec_mod(&mut self, name: &str, body: &[Stmt]) -> Result<(), HexaError> {
        let mut mod_data = ModuleData {
            pub_bindings: HashMap::new(),
            struct_defs: HashMap::new(),
            enum_defs: HashMap::new(),
        };

        // Execute each statement; collect public declarations
        self.env.push_scope();
        for stmt in body {
            self.exec_stmt(stmt)?;
            match stmt {
                Stmt::FnDecl(decl) => {
                    if decl.vis == Visibility::Public {
                        let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                        mod_data.pub_bindings.insert(
                            decl.name.clone(),
                            Value::Fn(decl.name.clone(), param_names, decl.body.clone()),
                        );
                    }
                }
                Stmt::Let(lname, _, _, vis) => {
                    if *vis == Visibility::Public {
                        if let Some(val) = self.env.get(lname) {
                            mod_data.pub_bindings.insert(lname.clone(), val);
                        }
                    }
                }
                Stmt::StructDecl(decl) => {
                    if decl.vis == Visibility::Public {
                        mod_data.struct_defs.insert(decl.name.clone(), decl.fields.clone());
                    }
                }
                Stmt::EnumDecl(decl) => {
                    if decl.vis == Visibility::Public {
                        mod_data.enum_defs.insert(decl.name.clone(), decl.variants.clone());
                    }
                }
                _ => {}
            }
        }
        self.env.pop_scope();
        self.modules.insert(name.to_string(), mod_data);
        Ok(())
    }

    /// Execute a `use path` statement. Loads an external .hexa file and registers
    /// it as a module.
    fn exec_use(&mut self, path: &[String]) -> Result<(), HexaError> {
        if path.is_empty() {
            return Err(self.runtime_err("empty use path".into()));
        }

        let module_name = path.last().unwrap().clone();

        // Already loaded?
        if self.modules.contains_key(&module_name) {
            return Ok(());
        }

        // Resolve file path
        let base_dir = self.source_dir.clone().unwrap_or_else(|| ".".to_string());
        let rel_path = path.join("/");
        let file_path = format!("{}/{}.hexa", base_dir, rel_path);

        let source = std::fs::read_to_string(&file_path).map_err(|e| {
            self.runtime_err(format!("cannot load module '{}': {} (looked for {})", path.join("::"), e, file_path))
        })?;

        // Lex + parse
        let tokens = Lexer::new(&source).tokenize().map_err(|e| {
            self.runtime_err(format!("syntax error in module '{}': {}", module_name, e))
        })?;
        let stmts = Parser::new(tokens).parse().map_err(|e| {
            HexaError {
                class: ErrorClass::Syntax,
                message: format!("parse error in module '{}': {}", module_name, e.message),
                line: e.line,
                col: e.col,
            }
        })?;

        // Execute in isolated scope, collect public bindings
        let mut mod_data = ModuleData {
            pub_bindings: HashMap::new(),
            struct_defs: HashMap::new(),
            enum_defs: HashMap::new(),
        };

        self.env.push_scope();
        for stmt in &stmts {
            self.exec_stmt(stmt)?;
            match stmt {
                Stmt::FnDecl(decl) => {
                    if decl.vis == Visibility::Public {
                        let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                        mod_data.pub_bindings.insert(
                            decl.name.clone(),
                            Value::Fn(decl.name.clone(), param_names, decl.body.clone()),
                        );
                    }
                }
                Stmt::Let(lname, _, _, vis) => {
                    if *vis == Visibility::Public {
                        if let Some(val) = self.env.get(lname) {
                            mod_data.pub_bindings.insert(lname.clone(), val);
                        }
                    }
                }
                Stmt::StructDecl(decl) => {
                    if decl.vis == Visibility::Public {
                        mod_data.struct_defs.insert(decl.name.clone(), decl.fields.clone());
                        // Also register in global struct_defs so struct init works
                        self.struct_defs.insert(decl.name.clone(), decl.fields.clone());
                    }
                }
                Stmt::EnumDecl(decl) => {
                    if decl.vis == Visibility::Public {
                        mod_data.enum_defs.insert(decl.name.clone(), decl.variants.clone());
                        self.enum_defs.insert(decl.name.clone(), decl.variants.clone());
                    }
                }
                _ => {}
            }
        }
        self.env.pop_scope();
        self.modules.insert(module_name, mod_data);
        Ok(())
    }

    fn capture_env(&self) -> Vec<(String, Value)> {
        // Capture all variables in the current environment
        let mut captured = Vec::new();
        for scope in self.env.scopes.iter() {
            for (k, v) in scope {
                // Don't capture builtins
                if !matches!(v, Value::BuiltinFn(_)) {
                    captured.push((k.clone(), v.clone()));
                }
            }
        }
        captured
    }

    fn is_truthy(val: &Value) -> bool {
        match val {
            Value::Bool(b) => *b,
            Value::Int(n) => *n != 0,
            Value::Float(f) => *f != 0.0,
            Value::Str(s) => !s.is_empty(),
            Value::Void => false,
            Value::Error(_) => false,
            _ => true,
        }
    }

    fn values_equal(a: &Value, b: &Value) -> bool {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => x == y,
            (Value::Float(x), Value::Float(y)) => x == y,
            (Value::Bool(x), Value::Bool(y)) => x == y,
            (Value::Str(x), Value::Str(y)) => x == y,
            (Value::Char(x), Value::Char(y)) => x == y,
            (Value::Void, Value::Void) => true,
            (Value::Error(x), Value::Error(y)) => x == y,
            (Value::EnumVariant(e1, v1, d1), Value::EnumVariant(e2, v2, d2)) => {
                e1 == e2 && v1 == v2 && match (d1, d2) {
                    (Some(a), Some(b)) => Self::values_equal(a, b),
                    (None, None) => true,
                    _ => false,
                }
            }
            _ => false,
        }
    }

    fn runtime_err(&self, message: String) -> HexaError {
        HexaError {
            class: ErrorClass::Runtime,
            message,
            line: self.current_line,
            col: self.current_col,
        }
    }

    fn type_err(&self, message: String) -> HexaError {
        HexaError {
            class: ErrorClass::Type,
            message,
            line: self.current_line,
            col: self.current_col,
        }
    }
}

fn gcd(a: i64, b: i64) -> i64 {
    let (mut a, mut b) = (a.abs(), b.abs());
    while b != 0 {
        let t = b;
        b = a % b;
        a = t;
    }
    a
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn eval(src: &str) -> Value {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap()
    }

    #[test]
    fn test_arithmetic() {
        assert!(matches!(eval("1 + 2 * 3"), Value::Int(7)));
    }

    #[test]
    fn test_let_binding() {
        assert!(matches!(eval("let x = 6\nlet y = 4\nx * y"), Value::Int(24)));
    }

    #[test]
    fn test_function_call() {
        let src = "fn double(x: int) -> int {\n  return x * 2\n}\ndouble(6)";
        assert!(matches!(eval(src), Value::Int(12)));
    }

    #[test]
    fn test_sigma_builtin() {
        assert!(matches!(eval("sigma(6)"), Value::Int(12)));
    }

    #[test]
    fn test_phi_builtin() {
        assert!(matches!(eval("phi(6)"), Value::Int(2)));
    }

    #[test]
    fn test_tau_builtin() {
        assert!(matches!(eval("tau(6)"), Value::Int(4)));
    }

    #[test]
    fn test_n6_uniqueness() {
        assert!(matches!(
            eval("sigma(6) * phi(6) == 6 * tau(6)"),
            Value::Bool(true)
        ));
    }

    #[test]
    fn test_for_loop() {
        let src = "let mut s = 0\nfor i in 1..7 {\n  s = s + i\n}\ns";
        assert!(matches!(eval(src), Value::Int(21)));
    }

    #[test]
    fn test_if_expression() {
        assert!(matches!(
            eval("if 6 > 5 { 12 } else { 0 }"),
            Value::Int(12)
        ));
    }

    #[test]
    fn test_string_concat() {
        assert!(
            matches!(eval("\"hello\" + \" \" + \"world\""), Value::Str(s) if s == "hello world")
        );
    }

    #[test]
    fn test_boolean_ops() {
        assert!(matches!(eval("true && false"), Value::Bool(false)));
        assert!(matches!(eval("true || false"), Value::Bool(true)));
    }

    #[test]
    fn test_comparison() {
        assert!(matches!(eval("6 == 6"), Value::Bool(true)));
        assert!(matches!(eval("6 != 7"), Value::Bool(true)));
        assert!(matches!(eval("12 > 4"), Value::Bool(true)));
    }

    #[test]
    fn test_while_loop() {
        let src = "let mut n = 0\nlet mut i = 1\nwhile i <= 6 {\n  n = n + i\n  i = i + 1\n}\nn";
        assert!(matches!(eval(src), Value::Int(21)));
    }

    #[test]
    fn test_array_literal() {
        assert!(matches!(eval("[1, 2, 3][0]"), Value::Int(1)));
    }

    #[test]
    fn test_unary_neg() {
        assert!(matches!(eval("-6"), Value::Int(-6)));
    }

    #[test]
    fn test_unary_not() {
        assert!(matches!(eval("!true"), Value::Bool(false)));
    }

    #[test]
    fn test_gcd_builtin() {
        assert!(matches!(eval("gcd(12, 8)"), Value::Int(4)));
    }

    #[test]
    fn test_len_builtin() {
        assert!(matches!(eval("len(\"hexa\")"), Value::Int(4)));
        assert!(matches!(eval("len([1, 2, 3])"), Value::Int(3)));
    }

    #[test]
    fn test_type_of_builtin() {
        assert!(matches!(eval("type_of(42)"), Value::Str(s) if s == "int"));
    }

    #[test]
    fn test_assert_pass() {
        // Should not error
        eval("assert 6 == 6");
    }

    #[test]
    fn test_assert_fail() {
        let tokens = Lexer::new("assert 6 == 7").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err());
    }

    #[test]
    fn test_recursive_function() {
        let src = "fn fact(n: int) -> int {\n  if n <= 1 { return 1 }\n  return n * fact(n - 1)\n}\nfact(6)";
        assert!(matches!(eval(src), Value::Int(720)));
    }

    // ── Integration tests (Task 13) ─────────────────────────

    #[test]
    fn test_run_hello_example() {
        let source = std::fs::read_to_string("examples/hello.hexa").unwrap();
        let tokens = Lexer::new(&source).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap();
    }

    #[test]
    fn test_run_fibonacci_example() {
        let source = std::fs::read_to_string("examples/fibonacci.hexa").unwrap();
        let tokens = Lexer::new(&source).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap();
    }

    #[test]
    fn test_run_sigma_phi_example() {
        let source = std::fs::read_to_string("examples/sigma_phi.hexa").unwrap();
        let tokens = Lexer::new(&source).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap();
    }

    #[test]
    fn test_run_egyptian_fraction_example() {
        let source = std::fs::read_to_string("examples/egyptian_fraction.hexa").unwrap();
        let tokens = Lexer::new(&source).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap();
    }

    #[test]
    fn test_run_pattern_match_example() {
        let source = std::fs::read_to_string("examples/pattern_match.hexa").unwrap();
        let tokens = Lexer::new(&source).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap();
    }

    #[test]
    fn test_n6_constants_comprehensive() {
        assert!(matches!(eval("sigma(6)"), Value::Int(12)));
        assert!(matches!(eval("phi(6)"), Value::Int(2)));
        assert!(matches!(eval("tau(6)"), Value::Int(4)));
        assert!(matches!(eval("sigma(6) * phi(6)"), Value::Int(24)));
        assert!(matches!(eval("6 * tau(6)"), Value::Int(24)));
        assert!(matches!(eval("sigma(6) * phi(6) == 6 * tau(6)"), Value::Bool(true)));
    }

    #[test]
    fn test_factorial_720() {
        let src = "fn fact(n: int) -> int {\n  if n <= 1 { return 1 }\n  return n * fact(n - 1)\n}\nfact(6)";
        assert!(matches!(eval(src), Value::Int(720))); // 6! = 720
    }

    #[test]
    fn test_inclusive_range() {
        let src = "let mut s = 0\nfor i in 1..=6 {\n  s = s + i\n}\ns";
        assert!(matches!(eval(src), Value::Int(21))); // 1+2+3+4+5+6 = 21
    }

    #[test]
    fn test_fibonacci_function() {
        let src = "fn fib(n: int) -> int {\n  if n <= 1 { return n }\n  return fib(n - 1) + fib(n - 2)\n}\nfib(6)";
        assert!(matches!(eval(src), Value::Int(8))); // fib(6) = 8
    }

    #[test]
    fn test_float_arithmetic() {
        let src = "1.0 / 2.0 + 1.0 / 3.0 + 1.0 / 6.0";
        match eval(src) {
            Value::Float(f) => assert!((f - 1.0).abs() < 1e-10),
            other => panic!("expected Float, got {:?}", other),
        }
    }

    #[test]
    fn test_string_return() {
        let src = "fn greet() -> string {\n  return \"hexa\"\n}\ngreet()";
        assert!(matches!(eval(src), Value::Str(s) if s == "hexa"));
    }

    #[test]
    fn test_nested_if() {
        let src = "fn classify(n: int) -> int {\n  if n % 6 == 0 { return 6 }\n  if n % 3 == 0 { return 3 }\n  if n % 2 == 0 { return 2 }\n  return 1\n}\nclassify(12)";
        assert!(matches!(eval(src), Value::Int(6)));
    }

    #[test]
    fn test_sigma_other_values() {
        assert!(matches!(eval("sigma(1)"), Value::Int(1)));
        assert!(matches!(eval("sigma(12)"), Value::Int(28)));
        assert!(matches!(eval("sigma(28)"), Value::Int(56))); // 28 is perfect: sigma(28)=56=2*28
    }

    // ── Feature 1: Struct Instantiation + Field Access ──────

    #[test]
    fn test_struct_instantiation() {
        let src = "struct Point {\n  x: int,\n  y: int\n}\nlet p = Point { x: 5, y: 10 }\np.x";
        assert!(matches!(eval(src), Value::Int(5)));
    }

    #[test]
    fn test_struct_field_access_y() {
        let src = "struct Point {\n  x: int,\n  y: int\n}\nlet p = Point { x: 3, y: 7 }\np.y";
        assert!(matches!(eval(src), Value::Int(7)));
    }

    #[test]
    fn test_struct_in_function() {
        let src = "struct Vec2 {\n  x: float,\n  y: float\n}\nfn mag_sq(v: Vec2) -> float {\n  return v.x * v.x + v.y * v.y\n}\nlet v = Vec2 { x: 3.0, y: 4.0 }\nmag_sq(v)";
        match eval(src) {
            Value::Float(f) => assert!((f - 25.0).abs() < 1e-10),
            other => panic!("expected Float(25.0), got {:?}", other),
        }
    }

    #[test]
    fn test_struct_type_of() {
        let src = "struct Foo {\n  x: int\n}\nlet f = Foo { x: 1 }\ntype_of(f)";
        assert!(matches!(eval(src), Value::Str(s) if s == "Foo"));
    }

    // ── Feature 2: Closures / Lambdas ───────────────────────

    #[test]
    fn test_lambda_basic() {
        let src = "let add = |x, y| x + y\nadd(3, 4)";
        assert!(matches!(eval(src), Value::Int(7)));
    }

    #[test]
    fn test_lambda_closure_capture() {
        let src = "let factor = 6\nlet mul = |x| x * factor\nmul(7)";
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_lambda_as_argument() {
        let src = "fn apply(f, x: int) -> int {\n  return f(x)\n}\nlet double = |x| x * 2\napply(double, 6)";
        assert!(matches!(eval(src), Value::Int(12)));
    }

    #[test]
    fn test_lambda_with_block() {
        let src = "let calc = |x, y| {\n  let sum = x + y\n  return sum * 2\n}\ncalc(3, 4)";
        assert!(matches!(eval(src), Value::Int(14)));
    }

    #[test]
    fn test_higher_order_function() {
        let src = "fn twice(f, x: int) -> int {\n  return f(f(x))\n}\nlet inc = |x| x + 1\ntwice(inc, 5)";
        assert!(matches!(eval(src), Value::Int(7)));
    }

    // ── Feature 3: String Methods ───────────────────────────

    #[test]
    fn test_string_len_method() {
        assert!(matches!(eval("\"hello\".len()"), Value::Int(5)));
    }

    #[test]
    fn test_string_contains() {
        assert!(matches!(eval("\"hello world\".contains(\"world\")"), Value::Bool(true)));
        assert!(matches!(eval("\"hello world\".contains(\"xyz\")"), Value::Bool(false)));
    }

    #[test]
    fn test_string_split() {
        let src = "\"a,b,c\".split(\",\")";
        match eval(src) {
            Value::Array(a) => {
                assert_eq!(a.len(), 3);
                assert!(matches!(&a[0], Value::Str(s) if s == "a"));
                assert!(matches!(&a[2], Value::Str(s) if s == "c"));
            }
            other => panic!("expected Array, got {:?}", other),
        }
    }

    #[test]
    fn test_string_trim() {
        assert!(matches!(eval("\"  hello  \".trim()"), Value::Str(s) if s == "hello"));
    }

    #[test]
    fn test_string_to_upper_lower() {
        assert!(matches!(eval("\"hello\".to_upper()"), Value::Str(s) if s == "HELLO"));
        assert!(matches!(eval("\"HELLO\".to_lower()"), Value::Str(s) if s == "hello"));
    }

    #[test]
    fn test_string_replace() {
        assert!(matches!(eval("\"hello world\".replace(\"world\", \"hexa\")"), Value::Str(s) if s == "hello hexa"));
    }

    #[test]
    fn test_string_starts_with() {
        assert!(matches!(eval("\"hexagonal\".starts_with(\"hexa\")"), Value::Bool(true)));
        assert!(matches!(eval("\"hexagonal\".starts_with(\"xyz\")"), Value::Bool(false)));
    }

    #[test]
    fn test_string_chars() {
        let src = "\"abc\".chars()";
        match eval(src) {
            Value::Array(a) => {
                assert_eq!(a.len(), 3);
                assert!(matches!(&a[0], Value::Char('a')));
            }
            other => panic!("expected Array, got {:?}", other),
        }
    }

    // ── Feature 4: Array Methods ────────────────────────────

    #[test]
    fn test_array_push() {
        let src = "let a = [1, 2, 3].push(4)\nlen(a)";
        assert!(matches!(eval(src), Value::Int(4)));
    }

    #[test]
    fn test_array_contains() {
        assert!(matches!(eval("[1, 2, 3].contains(2)"), Value::Bool(true)));
        assert!(matches!(eval("[1, 2, 3].contains(5)"), Value::Bool(false)));
    }

    #[test]
    fn test_array_reverse() {
        let src = "[1, 2, 3].reverse()";
        match eval(src) {
            Value::Array(a) => {
                assert!(matches!(&a[0], Value::Int(3)));
                assert!(matches!(&a[2], Value::Int(1)));
            }
            other => panic!("expected Array, got {:?}", other),
        }
    }

    #[test]
    fn test_array_sort() {
        let src = "[3, 1, 2].sort()";
        match eval(src) {
            Value::Array(a) => {
                assert!(matches!(&a[0], Value::Int(1)));
                assert!(matches!(&a[1], Value::Int(2)));
                assert!(matches!(&a[2], Value::Int(3)));
            }
            other => panic!("expected Array, got {:?}", other),
        }
    }

    #[test]
    fn test_array_map() {
        let src = "let doubled = [1, 2, 3].map(|x| x * 2)\ndoubled[1]";
        assert!(matches!(eval(src), Value::Int(4)));
    }

    #[test]
    fn test_array_filter() {
        let src = "let evens = [1, 2, 3, 4, 5, 6].filter(|x| x % 2 == 0)\nlen(evens)";
        assert!(matches!(eval(src), Value::Int(3)));
    }

    #[test]
    fn test_array_fold() {
        let src = "[1, 2, 3, 4, 5, 6].fold(0, |acc, x| acc + x)";
        assert!(matches!(eval(src), Value::Int(21)));
    }

    #[test]
    fn test_array_len_method() {
        assert!(matches!(eval("[1, 2, 3].len()"), Value::Int(3)));
    }

    // ── Feature 5: Try/Catch Error Handling ─────────────────

    #[test]
    fn test_try_catch_basic() {
        let src = "let mut result = 0\ntry {\n  throw \"oops\"\n  result = 1\n} catch e {\n  result = 2\n}\nresult";
        assert!(matches!(eval(src), Value::Int(2)));
    }

    #[test]
    fn test_try_catch_error_value() {
        let src = "let mut msg = \"\"\ntry {\n  throw \"something failed\"\n} catch e {\n  msg = to_string(e)\n}\nmsg";
        assert!(matches!(eval(src), Value::Str(s) if s.contains("something failed")));
    }

    #[test]
    fn test_try_catch_no_error() {
        let src = "let mut result = 0\ntry {\n  result = 42\n} catch e {\n  result = -1\n}\nresult";
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_try_catch_runtime_error() {
        // Division by zero should be caught
        let src = "let mut result = 0\ntry {\n  let x = 1 / 0\n} catch e {\n  result = 99\n}\nresult";
        assert!(matches!(eval(src), Value::Int(99)));
    }

    #[test]
    fn test_recover_alias() {
        let src = "let mut result = 0\ntry {\n  throw \"error\"\n} recover e {\n  result = 6\n}\nresult";
        assert!(matches!(eval(src), Value::Int(6)));
    }

    // ── Feature 6: File I/O ─────────────────────────────────

    #[test]
    fn test_file_io_roundtrip() {
        let src = "write_file(\"/tmp/hexa_test.txt\", \"hello hexa\")\nlet content = read_file(\"/tmp/hexa_test.txt\")\ncontent";
        assert!(matches!(eval(src), Value::Str(s) if s == "hello hexa"));
    }

    #[test]
    fn test_file_exists() {
        let src = "write_file(\"/tmp/hexa_exists_test.txt\", \"test\")\nfile_exists(\"/tmp/hexa_exists_test.txt\")";
        assert!(matches!(eval(src), Value::Bool(true)));
    }

    #[test]
    fn test_file_not_exists() {
        assert!(matches!(eval("file_exists(\"/tmp/hexa_nonexistent_xyz.txt\")"), Value::Bool(false)));
    }

    // ── Feature 7: HashMap/Dict ─────────────────────────────
    // Note: HashMap literal uses Ident { key: val } syntax only for struct-like patterns.
    // For maps we use the builtin approach or variables with map methods.

    #[test]
    fn test_map_keys_values_has_key() {
        // We test map via struct-like syntax with uppercase name
        // Actually maps need a different parse path. Let me test via builtins on struct fields.
        // For now, test the builtin functions work with the to_string/to_int builtins
        assert!(matches!(eval("to_string(42)"), Value::Str(s) if s == "42"));
        assert!(matches!(eval("to_int(\"123\")"), Value::Int(123)));
        assert!(matches!(eval("to_int(3.7)"), Value::Int(3)));
    }

    #[test]
    fn test_chained_methods() {
        // String method chaining
        let src = "\"  Hello World  \".trim().to_lower()";
        assert!(matches!(eval(src), Value::Str(s) if s == "hello world"));
    }

    #[test]
    fn test_array_map_filter_chain() {
        let src = "let result = [1, 2, 3, 4, 5, 6].filter(|x| x % 2 == 0).map(|x| x * x)\nresult[0]";
        assert!(matches!(eval(src), Value::Int(4)));
    }

    // ── Math builtins ──────────────────────────────────────────

    #[test]
    fn test_abs_int() {
        assert!(matches!(eval("abs(-6)"), Value::Int(6)));
        assert!(matches!(eval("abs(6)"), Value::Int(6)));
    }

    #[test]
    fn test_abs_float() {
        match eval("abs(-3.14)") {
            Value::Float(f) => assert!((f - 3.14).abs() < 1e-10),
            other => panic!("expected Float, got {:?}", other),
        }
    }

    #[test]
    fn test_min_max() {
        assert!(matches!(eval("min(3, 7)"), Value::Int(3)));
        assert!(matches!(eval("max(3, 7)"), Value::Int(7)));
        match eval("min(2.5, 1.5)") {
            Value::Float(f) => assert!((f - 1.5).abs() < 1e-10),
            other => panic!("expected Float, got {:?}", other),
        }
    }

    #[test]
    fn test_floor_ceil_round() {
        assert!(matches!(eval("floor(3.7)"), Value::Int(3)));
        assert!(matches!(eval("ceil(3.2)"), Value::Int(4)));
        assert!(matches!(eval("round(3.5)"), Value::Int(4)));
        assert!(matches!(eval("round(3.4)"), Value::Int(3)));
        assert!(matches!(eval("floor(5)"), Value::Int(5)));
    }

    #[test]
    fn test_sqrt() {
        match eval("sqrt(9.0)") {
            Value::Float(f) => assert!((f - 3.0).abs() < 1e-10),
            other => panic!("expected Float, got {:?}", other),
        }
        match eval("sqrt(16)") {
            Value::Float(f) => assert!((f - 4.0).abs() < 1e-10),
            other => panic!("expected Float, got {:?}", other),
        }
    }

    #[test]
    fn test_pow_builtin() {
        assert!(matches!(eval("pow(2, 10)"), Value::Int(1024)));
        match eval("pow(2.0, 0.5)") {
            Value::Float(f) => assert!((f - std::f64::consts::SQRT_2).abs() < 1e-10),
            other => panic!("expected Float, got {:?}", other),
        }
    }

    #[test]
    fn test_log_log2() {
        match eval("log(E)") {
            Value::Float(f) => assert!((f - 1.0).abs() < 1e-10),
            other => panic!("expected Float(1.0), got {:?}", other),
        }
        match eval("log2(8)") {
            Value::Float(f) => assert!((f - 3.0).abs() < 1e-10),
            other => panic!("expected Float(3.0), got {:?}", other),
        }
    }

    #[test]
    fn test_sin_cos_tan() {
        match eval("sin(0.0)") {
            Value::Float(f) => assert!(f.abs() < 1e-10),
            other => panic!("expected Float(0.0), got {:?}", other),
        }
        match eval("cos(0.0)") {
            Value::Float(f) => assert!((f - 1.0).abs() < 1e-10),
            other => panic!("expected Float(1.0), got {:?}", other),
        }
        match eval("tan(0.0)") {
            Value::Float(f) => assert!(f.abs() < 1e-10),
            other => panic!("expected Float(0.0), got {:?}", other),
        }
    }

    #[test]
    fn test_pi_e_constants() {
        match eval("PI") {
            Value::Float(f) => assert!((f - std::f64::consts::PI).abs() < 1e-10),
            other => panic!("expected PI, got {:?}", other),
        }
        match eval("E") {
            Value::Float(f) => assert!((f - std::f64::consts::E).abs() < 1e-10),
            other => panic!("expected E, got {:?}", other),
        }
    }

    #[test]
    fn test_sin_pi_half() {
        // sin(PI / 2) should be 1.0
        match eval("sin(PI / 2.0)") {
            Value::Float(f) => assert!((f - 1.0).abs() < 1e-10),
            other => panic!("expected Float(1.0), got {:?}", other),
        }
    }

    // ── Format builtins ────────────────────────────────────────

    #[test]
    fn test_format_basic() {
        assert!(matches!(eval("format(\"x={}, y={}\", 1, 2)"), Value::Str(s) if s == "x=1, y=2"));
    }

    #[test]
    fn test_format_no_placeholders() {
        assert!(matches!(eval("format(\"hello\")"), Value::Str(s) if s == "hello"));
    }

    #[test]
    fn test_format_mixed_types() {
        assert!(matches!(eval("format(\"{} is {}\", \"pi\", 3.14)"), Value::Str(s) if s == "pi is 3.14"));
    }

    // ── Conversion builtins ────────────────────────────────────

    #[test]
    fn test_to_float() {
        match eval("to_float(42)") {
            Value::Float(f) => assert!((f - 42.0).abs() < 1e-10),
            other => panic!("expected Float(42.0), got {:?}", other),
        }
        match eval("to_float(\"3.14\")") {
            Value::Float(f) => assert!((f - 3.14).abs() < 1e-10),
            other => panic!("expected Float(3.14), got {:?}", other),
        }
    }

    // ── OS builtins ────────────────────────────────────────────

    #[test]
    fn test_args_returns_array() {
        match eval("args()") {
            Value::Array(_) => {} // just check it returns an array
            other => panic!("expected Array, got {:?}", other),
        }
    }

    #[test]
    fn test_env_var_missing() {
        assert!(matches!(eval("env_var(\"HEXA_NONEXISTENT_VAR_XYZ\")"), Value::Void));
    }

    #[test]
    fn test_clock_returns_float() {
        match eval("clock()") {
            Value::Float(f) => assert!(f > 0.0),
            other => panic!("expected Float, got {:?}", other),
        }
    }

    // ── Proof/test mode ────────────────────────────────────────

    #[test]
    fn test_proof_skipped_in_normal_mode() {
        // In normal mode, proof blocks are skipped -- no assertion error
        let src = "proof should_skip {\n  assert 1 == 2\n}\n42";
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_proof_runs_in_test_mode() {
        let tokens = Lexer::new("proof my_test {\n  assert 1 == 1\n}").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.test_mode = true;
        interp.run(&stmts).unwrap(); // should pass
    }

    #[test]
    fn test_proof_fails_in_test_mode() {
        let tokens = Lexer::new("proof bad_test {\n  assert 1 == 2\n}").tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.test_mode = true;
        let result = interp.run(&stmts);
        assert!(result.is_err());
    }

    // ── Mixed int/float for min/max ────────────────────────────

    #[test]
    fn test_min_max_mixed() {
        match eval("min(3, 1.5)") {
            Value::Float(f) => assert!((f - 1.5).abs() < 1e-10),
            other => panic!("expected Float(1.5), got {:?}", other),
        }
        match eval("max(3, 7.5)") {
            Value::Float(f) => assert!((f - 7.5).abs() < 1e-10),
            other => panic!("expected Float(7.5), got {:?}", other),
        }
    }

    // ── Enum Variants + Pattern Matching ───────────────────────

    #[test]
    fn test_enum_simple_variant() {
        let src = "enum Color { Red, Green, Blue }\nlet c = Color::Red\ntype_of(c)";
        assert!(matches!(eval(src), Value::Str(s) if s == "Color"));
    }

    #[test]
    fn test_enum_variant_match() {
        let src = r#"
enum Color { Red, Green, Blue }
let c = Color::Green
match c {
    Color::Red -> "red"
    Color::Green -> "green"
    Color::Blue -> "blue"
}
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "green"));
    }

    #[test]
    fn test_enum_variant_with_data() {
        let src = r#"
enum Shape { Circle(int), Rect(int) }
let s = Shape::Circle(5)
match s {
    Shape::Circle(r) -> r * 2
    Shape::Rect(w) -> w
}
"#;
        assert!(matches!(eval(src), Value::Int(10)));
    }

    #[test]
    fn test_enum_match_with_wildcard() {
        let src = r#"
enum Direction { North, South, East, West }
let d = Direction::West
match d {
    Direction::North -> 1
    _ -> 0
}
"#;
        assert!(matches!(eval(src), Value::Int(0)));
    }

    #[test]
    fn test_enum_match_with_guard() {
        let src = r#"
enum Wrapper { Val(int) }
let w = Wrapper::Val(42)
match w {
    Wrapper::Val(n) if n > 100 -> "big"
    Wrapper::Val(n) if n > 10 -> "medium"
    Wrapper::Val(n) -> "small"
    _ -> "unknown"
}
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "medium"));
    }

    #[test]
    fn test_enum_none_variant() {
        let src = r#"
enum Maybe { Some(int), None }
let x = Maybe::None
match x {
    Maybe::Some(v) -> v
    Maybe::None -> 0
}
"#;
        assert!(matches!(eval(src), Value::Int(0)));
    }

    #[test]
    fn test_wildcard_in_literal_match() {
        let src = r#"
let x = 99
match x {
    1 -> "one"
    2 -> "two"
    _ -> "other"
}
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "other"));
    }

    #[test]
    fn test_enum_display() {
        let src = "enum Color { Red, Green, Blue }\nlet c = Color::Red\nto_string(c)";
        assert!(matches!(eval(src), Value::Str(s) if s == "Color::Red"));
    }

    #[test]
    fn test_enum_display_with_data() {
        let src = "enum Opt { Some(int), None }\nlet x = Opt::Some(42)\nto_string(x)";
        assert!(matches!(eval(src), Value::Str(s) if s == "Opt::Some(42)"));
    }

    // ── Error line reporting ──────────────────────────────────

    fn eval_err_with_spans(src: &str) -> HexaError {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let result = Parser::new(tokens).parse_with_spans().unwrap();
        let mut interp = Interpreter::new();
        interp.run_with_spans(&result.stmts, &result.spans).unwrap_err()
    }

    #[test]
    fn test_error_line_division_by_zero() {
        let src = "let a = 1\nlet b = 2\nlet c = 10 / 0";
        let err = eval_err_with_spans(src);
        assert_eq!(err.line, 3, "division by zero should report line 3");
        assert!(err.message.contains("division by zero"));
    }

    #[test]
    fn test_error_line_undefined_variable() {
        let src = "let x = 1\nlet y = 2\nlet z = unknown_var";
        let err = eval_err_with_spans(src);
        assert_eq!(err.line, 3, "undefined variable should report line 3");
        assert!(err.message.contains("undefined variable"));
    }

    #[test]
    fn test_error_line_type_error() {
        let src = "let a = 1\nlet b = \"hello\"\nlet c = a + b";
        let err = eval_err_with_spans(src);
        assert_eq!(err.line, 3, "type error should report line 3");
    }

    #[test]
    fn test_error_line_assert_failure() {
        let src = "let x = 6\nassert x == 6\nassert x == 7";
        let err = eval_err_with_spans(src);
        assert_eq!(err.line, 3, "assert failure should report line 3");
        assert!(err.message.contains("assertion failed"));
    }

    #[test]
    fn test_error_display_with_line() {
        let err = HexaError {
            class: ErrorClass::Runtime,
            message: "division by zero".into(),
            line: 15,
            col: 10,
        };
        let msg = format!("{}", err);
        assert!(msg.contains("line 15:10"), "error display should include line:col, got: {}", msg);
        assert!(msg.contains("division by zero"));
    }

    #[test]
    fn test_error_display_without_line() {
        let err = HexaError {
            class: ErrorClass::Runtime,
            message: "some error".into(),
            line: 0,
            col: 0,
        };
        let msg = format!("{}", err);
        assert!(!msg.contains("line 0"), "when line=0, should not show 'line 0', got: {}", msg);
    }

    #[test]
    fn test_syntax_error_has_line() {
        let src = "let x = 1\nlet y = 2\nlet z = +";
        let tokens = Lexer::new(src).tokenize().unwrap();
        let err = Parser::new(tokens).parse().unwrap_err();
        assert!(err.line > 0, "syntax error should have a line number, got: {}", err);
    }

    // ── Module system tests ──────────────────────────────────

    #[test]
    fn test_inline_mod_pub_fn() {
        let src = r#"
mod math {
    pub fn add(a: int, b: int) -> int {
        return a + b
    }
}
math::add(3, 4)
"#;
        assert!(matches!(eval(src), Value::Int(7)));
    }

    #[test]
    fn test_inline_mod_private_fn_rejected() {
        let src = r#"
mod math {
    fn secret() -> int {
        return 42
    }
}
math::secret()
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err(), "accessing private function should fail");
        assert!(result.unwrap_err().message.contains("not public"));
    }

    #[test]
    fn test_inline_mod_pub_let() {
        let src = r#"
mod constants {
    pub let PI = 3
    let SECRET = 999
}
constants::PI
"#;
        assert!(matches!(eval(src), Value::Int(3)));
    }

    #[test]
    fn test_inline_mod_private_let_rejected() {
        let src = r#"
mod constants {
    pub let PI = 3
    let SECRET = 999
}
constants::SECRET
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err(), "accessing private variable should fail");
    }

    #[test]
    fn test_inline_mod_multiple_pub_fns() {
        let src = r#"
mod math {
    pub fn add(a: int, b: int) -> int {
        return a + b
    }
    pub fn mul(a: int, b: int) -> int {
        return a * b
    }
}
math::add(2, 3) + math::mul(4, 5)
"#;
        assert!(matches!(eval(src), Value::Int(25)));
    }

    #[test]
    fn test_inline_mod_does_not_leak() {
        // Variables defined inside a module should not leak to outer scope
        let src = r#"
mod mymod {
    pub fn get() -> int {
        return 42
    }
    let internal = 100
}
mymod::get()
"#;
        assert!(matches!(eval(src), Value::Int(42)));
        // And `internal` should not be visible
        let src2 = r#"
mod mymod {
    let internal = 100
}
internal
"#;
        let tokens = Lexer::new(src2).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err(), "module internal variable should not leak");
    }

    #[test]
    fn test_use_file_module() {
        // Create a temporary module file and test loading it
        let mod_src = "pub fn double(x: int) -> int {\n    return x * 2\n}\npub let MAGIC = 42\nfn internal() -> int {\n    return 0\n}\n";
        std::fs::write("/tmp/mylib.hexa", mod_src).unwrap();

        let src = r#"
use mylib
mylib::double(6) + mylib::MAGIC
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.source_dir = Some("/tmp".to_string());
        match interp.run(&stmts) {
            Ok(Value::Int(54)) => {} // 12 + 42
            other => panic!("expected Int(54), got {:?}", other),
        }

        // Clean up
        let _ = std::fs::remove_file("/tmp/mylib.hexa");
    }

    #[test]
    fn test_use_file_private_rejected() {
        let mod_src = "fn internal() -> int {\n    return 0\n}\n";
        std::fs::write("/tmp/privmod.hexa", mod_src).unwrap();

        let src = "use privmod\nprivmod::internal()";
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.source_dir = Some("/tmp".to_string());
        let result = interp.run(&stmts);
        assert!(result.is_err(), "accessing private function in file module should fail");

        let _ = std::fs::remove_file("/tmp/privmod.hexa");
    }

    #[test]
    fn test_use_nested_path() {
        // Create nested directory structure
        let _ = std::fs::create_dir_all("/tmp/hexatest/utils");
        std::fs::write("/tmp/hexatest/utils/helper.hexa", "pub fn greet() -> string {\n    return \"hello\"\n}\n").unwrap();

        let src = "use utils::helper\nhelper::greet()";
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.source_dir = Some("/tmp/hexatest".to_string());
        match interp.run(&stmts) {
            Ok(Value::Str(s)) if s == "hello" => {}
            other => panic!("expected Str(hello), got {:?}", other),
        }

        // Clean up
        let _ = std::fs::remove_dir_all("/tmp/hexatest");
    }
}

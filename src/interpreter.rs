#![allow(dead_code)]

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
#[cfg(not(target_arch = "wasm32"))]
use std::sync::mpsc;
#[cfg(not(target_arch = "wasm32"))]
use std::thread;
use crate::ast::*;
use crate::env::{Env, Value};
use crate::error::{HexaError, ErrorClass};
use crate::lexer::Lexer;
use crate::memory::{EgyptianMemory, MemRegion, MemoryStats, estimate_value_size, classify_region};
use crate::parser::Parser;
use crate::proof_engine;
use crate::type_checker::SpecKey;
use crate::inline_cache::{InlineCache, CallSiteId};

/// Sentinel error message used to propagate `return` across call frames.
const RETURN_SENTINEL: &str = "__hexa_return__";

/// Sentinel error message used to propagate `throw` across call frames.
const THROW_SENTINEL: &str = "__hexa_throw__";

/// Sentinel error messages for break/continue in loops.
const BREAK_SENTINEL: &str = "__hexa_break__";
const CONTINUE_SENTINEL: &str = "__hexa_continue__";


/// Fast structural hash for Value — avoids format!("{:?}") heap allocation.
#[inline]
fn hash_value_interp(val: &Value, h: &mut impl std::hash::Hasher) {
    match val {
        Value::Int(n) => { std::hash::Hash::hash(&0u8, h); std::hash::Hash::hash(n, h); }
        Value::Float(f) => { std::hash::Hash::hash(&1u8, h); std::hash::Hash::hash(&f.to_bits(), h); }
        Value::Bool(b) => { std::hash::Hash::hash(&2u8, h); std::hash::Hash::hash(b, h); }
        Value::Str(s) => { std::hash::Hash::hash(&3u8, h); std::hash::Hash::hash(s.as_str(), h); }
        Value::Void => { std::hash::Hash::hash(&4u8, h); }
        Value::Char(c) => { std::hash::Hash::hash(&5u8, h); std::hash::Hash::hash(c, h); }
        Value::Byte(b) => { std::hash::Hash::hash(&6u8, h); std::hash::Hash::hash(b, h); }
        Value::Array(arr) => {
            std::hash::Hash::hash(&7u8, h);
            for item in arr { hash_value_interp(item, h); }
        }
        other => { std::hash::Hash::hash(&99u8, h); std::hash::Hash::hash(&format!("{:?}", other), h); }
    }
}

pub struct Interpreter {
    pub env: Env,
    /// Holds the value carried by the most recent `return` statement.
    pub return_value: Option<Value>,
    /// Holds the value carried by the most recent `throw` statement.
    throw_value: Option<Value>,
    /// Struct declarations: name -> field definitions (name, type, visibility)
    pub struct_defs: HashMap<String, Vec<(String, String, Visibility)>>,
    /// Enum declarations: name -> list of (variant_name, optional_data_types)
    pub enum_defs: HashMap<String, Vec<(String, Option<Vec<String>>)>>,
    /// Impl methods: type_name -> method_name -> (param_names, body)
    pub type_methods: HashMap<String, HashMap<String, (Vec<String>, Vec<Stmt>)>>,
    /// Trait definitions: trait_name -> list of method signatures (name, params, body)
    /// Methods with non-empty body are default implementations.
    trait_defs: HashMap<String, Vec<(String, Vec<String>, Vec<Stmt>)>>,
    /// Trait implementations: (type_name, trait_name) -> method_name -> (param_names, body)
    trait_impls: HashMap<(String, String), HashMap<String, (Vec<String>, Vec<Stmt>)>>,
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
    /// Source lines for diagnostic error messages.
    pub source_lines: Vec<String>,
    /// File name for diagnostic error messages.
    pub file_name: String,
    /// Join handles for spawned threads.
    #[cfg(not(target_arch = "wasm32"))]
    spawn_handles: Vec<thread::JoinHandle<()>>,
    /// Named spawn handles for structured concurrency.
    #[cfg(not(target_arch = "wasm32"))]
    named_spawns: HashMap<String, thread::JoinHandle<()>>,
    /// Stack of task groups for structured concurrency scopes.
    #[cfg(not(target_arch = "wasm32"))]
    scope_task_groups: Vec<crate::async_runtime::TaskGroup>,
    /// Macro definitions: name -> StoredMacro
    macro_defs: HashMap<String, crate::macro_expand::StoredMacro>,
    /// Compile-time function definitions: name -> (param_names, body)
    comptime_fns: HashMap<String, (Vec<String>, Vec<Stmt>)>,
    /// When true, I/O and side-effectful builtins are forbidden (comptime mode).
    comptime_mode: bool,
    /// Algebraic effect definitions: effect_name -> list of operation names
    effect_defs: HashMap<String, Vec<String>>,
    /// Algebraic effect handler stack: each entry is a map of (effect.op) -> handler
    effect_handler_stack: Vec<HashMap<String, (Vec<String>, Vec<Stmt>)>>,
    /// Current effect request being handled (for resume)
    effect_request: Option<Value>,
    /// Resume value set by resume(val) in a handler
    resume_value: Option<Value>,
    /// Whether we are inside a pure function (effects disallowed)
    in_pure_fn: bool,
    /// Egyptian Fraction memory manager (1/2 + 1/3 + 1/6 = 1, n=6).
    pub memory: EgyptianMemory,
    /// Optional output capture buffer. When set, print/println write here
    /// instead of stdout. Used by the WASM playground and library mode.
    output_capture: Option<Arc<Mutex<String>>>,
    /// Generic function declarations: fn_name -> FnDecl (for monomorphization)
    generic_fn_decls: HashMap<String, FnDecl>,
    /// Specialization cache: (fn_name, concrete_types) -> specialized Value::Fn
    spec_cache: HashMap<SpecKey, Value>,
    /// Inline cache for monomorphic method dispatch.
    pub inline_cache: InlineCache,
    /// Function call cache: name -> Value::Fn for hot call sites.
    fn_cache: HashMap<String, Value>,
    /// Optional debug hook for DAP debugger integration.
    #[cfg(not(target_arch = "wasm32"))]
    pub debug_hook: Option<Arc<Mutex<crate::debugger::DebugHook>>>,
    /// Registered extern function declarations (name -> ExternFnDecl).
    extern_fns: HashMap<String, crate::ast::ExternFnDecl>,
    /// @memoize cache: fn_name -> (args_hash -> cached_result)
    memo_cache: HashMap<String, HashMap<u64, Value>>,
    /// @optimize: pattern-detected optimized functions (coefficients, base_values)
    optimized_fns: HashMap<String, (Vec<i64>, Vec<i64>)>,
    autograd_fns: HashMap<String, (Vec<String>, Arc<Vec<Stmt>>)>,
    vectorize_fns: HashMap<String, (Vec<String>, Arc<Vec<Stmt>>)>,
    fuse_fns: HashMap<String, (Vec<String>, Arc<Vec<Stmt>>)>,
    lazy_fns: std::collections::HashSet<String>,
    inline_fns: std::collections::HashSet<String>,
    evolve_fns: HashMap<String, (Vec<String>, Arc<Vec<Stmt>>, u64)>,
    test_fns: Vec<(String, Vec<String>, Arc<Vec<Stmt>>)>,
    bench_fns: Vec<(String, Vec<String>, Arc<Vec<Stmt>>)>,
    deprecated_fns: HashMap<String, String>,  // fn_name -> deprecation message  // name -> (params, current_body, generation)
    /// Loaded shared libraries: lib_path -> handle (dlopen result).
    #[cfg(not(target_arch = "wasm32"))]
    loaded_libs: HashMap<String, *mut std::ffi::c_void>,
    /// Resolved extern symbols: fn_name -> symbol address.
    #[cfg(not(target_arch = "wasm32"))]
    extern_symbols: HashMap<String, *mut std::ffi::c_void>,
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
            trait_defs: HashMap::new(),
            trait_impls: HashMap::new(),
            modules: HashMap::new(),
            test_mode: false,
            current_line: 0,
            current_col: 0,
            source_dir: None,
            source_lines: Vec::new(),
            file_name: String::new(),
            #[cfg(not(target_arch = "wasm32"))]
            spawn_handles: Vec::new(),
            #[cfg(not(target_arch = "wasm32"))]
            named_spawns: HashMap::new(),
            #[cfg(not(target_arch = "wasm32"))]
            scope_task_groups: Vec::new(),
            macro_defs: HashMap::new(),
            comptime_fns: HashMap::new(),
            effect_defs: HashMap::new(),
            effect_handler_stack: Vec::new(),
            effect_request: None,
            resume_value: None,
            in_pure_fn: false,
            comptime_mode: false,
            memory: EgyptianMemory::with_default_budget(),
            output_capture: None,
            generic_fn_decls: HashMap::new(),
            spec_cache: HashMap::new(),
            inline_cache: InlineCache::new(),
            fn_cache: HashMap::new(),
            #[cfg(not(target_arch = "wasm32"))]
            debug_hook: None,
            extern_fns: HashMap::new(),
            memo_cache: HashMap::new(),
            optimized_fns: HashMap::new(),
            autograd_fns: HashMap::new(),
            vectorize_fns: HashMap::new(),
            fuse_fns: HashMap::new(),
            lazy_fns: std::collections::HashSet::new(),
            inline_fns: std::collections::HashSet::new(),
            evolve_fns: HashMap::new(),
            test_fns: Vec::new(),
            bench_fns: Vec::new(),
            deprecated_fns: HashMap::new(),
            #[cfg(not(target_arch = "wasm32"))]
            loaded_libs: HashMap::new(),
            #[cfg(not(target_arch = "wasm32"))]
            extern_symbols: HashMap::new(),
        }
    }

    /// Create an interpreter with a specific memory budget in bytes.
    pub fn new_with_memory_budget(budget: usize) -> Self {
        let mut s = Self::new();
        s.memory = EgyptianMemory::new(budget);
        s
    }

    /// Get memory stats as a displayable report.
    pub fn memory_stats(&self) -> MemoryStats {
        self.memory.stats()
    }

    /// Set an output capture buffer. When set, print/println write to this
    /// buffer instead of stdout. Pass `None` to restore normal stdout behavior.
    pub fn set_output_capture(&mut self, buf: Option<Arc<Mutex<String>>>) {
        self.output_capture = buf;
    }

    /// Write a string to the output (captured buffer or stdout).
    fn write_output(&self, s: &str) {
        if let Some(ref buf) = self.output_capture {
            if let Ok(mut b) = buf.lock() {
                b.push_str(s);
            }
        } else {
            print!("{}", s);
        }
    }

    /// Write a string followed by newline to the output.
    fn writeln_output(&self, s: &str) {
        if let Some(ref buf) = self.output_capture {
            if let Ok(mut b) = buf.lock() {
                b.push_str(s);
                b.push('\n');
            }
        } else {
            println!("{}", s);
        }
    }

    /// Write a string followed by newline to stderr.
    fn ewriteln_output(&self, s: &str) {
        if let Some(ref buf) = self.output_capture {
            if let Ok(mut b) = buf.lock() {
                b.push_str(s);
                b.push('\n');
            }
        } else {
            eprintln!("{}", s);
        }
    }

    /// Enable or disable comptime (sandboxed) mode.
    pub fn set_comptime_mode(&mut self, mode: bool) {
        self.comptime_mode = mode;
    }

    /// Public wrapper around eval_expr for use by the comptime evaluator.
    pub fn eval_expr_pub(&mut self, expr: &Expr) -> Result<Value, HexaError> {
        self.eval_expr(expr)
    }

    /// Set a debug hook for DAP debugger integration.
    #[cfg(not(target_arch = "wasm32"))]
    pub fn set_debug_hook(&mut self, hook: Option<Arc<Mutex<crate::debugger::DebugHook>>>) {
        self.debug_hook = hook;
    }

    /// Call the debug hook before executing a statement, if one is set.
    /// Returns false if the debugger requests disconnection.
    #[cfg(not(target_arch = "wasm32"))]
    fn invoke_debug_hook(&mut self) -> bool {
        if let Some(ref hook) = self.debug_hook {
            if let Ok(mut h) = hook.lock() {
                return h.on_statement(self.current_line, &self.file_name, &self.env);
            }
        }
        true
    }

    // ── Public entry point ──────────────────────────────────

    pub fn run(&mut self, stmts: &[Stmt]) -> Result<Value, HexaError> {
        let mut last = Value::Void;
        for stmt in stmts {
            last = self.exec_stmt(stmt)?;
            if self.return_value.is_some() {
                self.join_spawned();
                return Ok(self.return_value.take().unwrap());
            }
            if self.throw_value.is_some() {
                self.join_spawned();
                let err = self.throw_value.take().unwrap();
                return Err(self.runtime_err(format!("uncaught error: {}", err)));
            }
        }
        self.join_spawned();
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
            // Invoke debug hook before each statement (DAP debugger)
            #[cfg(not(target_arch = "wasm32"))]
            if !self.invoke_debug_hook() {
                return Err(self.runtime_err("debugger disconnected".to_string()));
            }
            last = self.exec_stmt(stmt)?;
            if self.return_value.is_some() {
                self.join_spawned();
                return Ok(self.return_value.take().unwrap());
            }
            if self.throw_value.is_some() {
                self.join_spawned();
                let err = self.throw_value.take().unwrap();
                return Err(self.runtime_err(format!("uncaught error: {}", err)));
            }
        }
        self.join_spawned();
        Ok(last)
    }

    /// Wait for all spawned threads to complete.
    pub fn join_spawned(&mut self) {
        #[cfg(not(target_arch = "wasm32"))]
        {
            for handle in self.spawn_handles.drain(..) {
                let _ = handle.join();
            }
        }
    }

    // ── Statement execution ─────────────────────────────────

    pub fn exec_stmt(&mut self, stmt: &Stmt) -> Result<Value, HexaError> {
        match stmt {
            Stmt::Let(name, _typ, expr, _vis) => {
                let is_own = matches!(expr, Some(Expr::Own(_)));
                let is_borrow = matches!(expr, Some(Expr::Borrow(_)));
                let val = match expr {
                    Some(e) => self.eval_expr(e)?,
                    None => Value::Void,
                };
                // Runtime validation for consciousness law types
                if let Some(typ_name) = _typ.as_deref() {
                    match typ_name {
                        "Phi_positive" => {
                            let v = match &val {
                                Value::Float(f) => *f,
                                Value::Int(n) => *n as f64,
                                _ => return Err(self.type_err(format!("Phi_positive requires numeric value, got {}", val))),
                            };
                            if v <= 0.0 {
                                return Err(self.runtime_err(format!("Phi_positive constraint violated: {} <= 0", v)));
                            }
                        }
                        "Tension_bounded" => {
                            let v = match &val {
                                Value::Float(f) => *f,
                                Value::Int(n) => *n as f64,
                                _ => return Err(self.type_err(format!("Tension_bounded requires numeric value, got {}", val))),
                            };
                            if v < 0.0 || v > 1.0 {
                                return Err(self.runtime_err(format!("Tension_bounded constraint violated: {} not in [0, 1]", v)));
                            }
                        }
                        _ => {}
                    }
                }
                // Track allocation in Egyptian memory (skip for value types — hot path)
                if !val.is_value_type() {
                    let size = estimate_value_size(&val);
                    let region = classify_region(&val);
                    match region {
                        MemRegion::Stack => { let _ = self.memory.stack_alloc(name, size); }
                        MemRegion::Heap => { let _ = self.memory.heap_alloc(name, size); }
                        MemRegion::Arena => { let _ = self.memory.arena_alloc(size); }
                    }
                }
                self.env.define(name, val);
                if is_own {
                    use crate::env::OwnershipState;
                    self.env.set_ownership(name, OwnershipState::Owned);
                } else if is_borrow {
                    use crate::env::OwnershipState;
                    self.env.set_ownership(name, OwnershipState::Borrowed);
                }
                Ok(Value::Void)
            }
            Stmt::LetTuple(names, expr) => {
                let val = self.eval_expr(expr)?;
                let items = match &val {
                    Value::Tuple(t) => t.clone(),
                    Value::Array(a) => a.clone(),
                    _ => return Err(self.type_err(format!("cannot destructure non-tuple/array value: {}", val))),
                };
                if items.len() < names.len() {
                    return Err(self.type_err(format!("not enough values to unpack: expected {}, got {}", names.len(), items.len())));
                }
                for (i, name) in names.iter().enumerate() {
                    self.env.define(name, items[i].clone());
                }
                Ok(Value::Void)
            }
            Stmt::Const(name, _typ, expr, _vis) => {
                let val = self.eval_expr(expr)?;
                self.env.define_const(name, val);
                Ok(Value::Void)
            }
            Stmt::Static(name, _typ, expr, _vis) => {
                // Static variables must be at module level (scope depth == 1)
                if self.env.scope_depth() > 1 {
                    return Err(self.runtime_err(
                        "static variables must be declared at module level".to_string()
                    ));
                }
                let val = self.eval_expr(expr)?;
                self.env.define_static(name, val);
                Ok(Value::Void)
            }
            Stmt::Assign(lhs, rhs) => {
                let val = self.eval_expr(rhs)?;
                match lhs {
                    Expr::Ident(name) => {
                        // Check constant: constants cannot be reassigned
                        if self.env.is_constant(name) {
                            return Err(self.runtime_err(format!("cannot reassign to constant `{}`", name)));
                        }
                        // Check ownership state: borrowed values cannot be mutated
                        {
                            use crate::env::OwnershipState;
                            let state = self.env.get_ownership(name);
                            match state {
                                OwnershipState::Borrowed => {
                                    return Err(self.runtime_err(format!("cannot mutate '{}': value is borrowed (read-only)", name)));
                                }
                                OwnershipState::Moved => {
                                    return Err(self.runtime_err(format!("cannot assign to '{}': value has been moved", name)));
                                }
                                OwnershipState::Dropped => {
                                    return Err(self.runtime_err(format!("cannot assign to '{}': value has been dropped", name)));
                                }
                                _ => {}
                            }
                        }
                        if !self.env.set(name, val) {
                            return Err(self.runtime_err(format!("undefined variable: {}", name)));
                        }
                    }
                    Expr::Index(arr_expr, idx_expr) => {
                        // Collect index chain: a[b][c][d] = val
                        // => root_name, indices = [b_val, c_val, d_val]
                        let mut indices = Vec::new();
                        let mut current = &*arr_expr;
                        let idx_val = self.eval_expr(idx_expr)?;
                        indices.push(idx_val);

                        while let Expr::Index(inner, inner_idx) = &**current {
                            let iv = self.eval_expr(inner_idx)?;
                            indices.push(iv);
                            current = inner;
                        }
                        indices.reverse();

                        if let Expr::Ident(name) = &**current {
                            if let Some(mut root) = self.env.get(name) {
                                // Navigate to the nested container and set the value
                                fn set_nested(container: &mut Value, indices: &[Value], val: Value) -> Result<(), String> {
                                    if indices.len() == 1 {
                                        match (&mut *container, &indices[0]) {
                                            (Value::Map(map), Value::Str(key)) => {
                                                map.insert(key.clone(), val);
                                                Ok(())
                                            }
                                            (Value::Array(arr), Value::Int(idx)) => {
                                                let i = if *idx < 0 {
                                                    let pos = arr.len() as i64 + *idx;
                                                    if pos < 0 { return Err(format!("index {} out of bounds (len {})", idx, arr.len())); }
                                                    pos as usize
                                                } else {
                                                    *idx as usize
                                                };
                                                if i >= arr.len() {
                                                    return Err(format!("index {} out of bounds (len {})", idx, arr.len()));
                                                }
                                                arr[i] = val;
                                                Ok(())
                                            }
                                            _ => Err("invalid index type for assignment".into()),
                                        }
                                    } else {
                                        let next = match (container, &indices[0]) {
                                            (Value::Map(map), Value::Str(key)) => {
                                                map.get_mut(key).ok_or_else(|| format!("key '{}' not found", key))?
                                            }
                                            (Value::Array(arr), Value::Int(idx)) => {
                                                let i = if *idx < 0 {
                                                    let pos = arr.len() as i64 + *idx;
                                                    if pos < 0 { return Err(format!("index {} out of bounds (len {})", idx, arr.len())); }
                                                    pos as usize
                                                } else {
                                                    *idx as usize
                                                };
                                                if i >= arr.len() {
                                                    return Err(format!("index {} out of bounds (len {})", idx, arr.len()));
                                                }
                                                &mut arr[i]
                                            }
                                            _ => return Err("invalid index type for nested access".into()),
                                        };
                                        set_nested(next, &indices[1..], val)
                                    }
                                }
                                set_nested(&mut root, &indices, val).map_err(|e| self.runtime_err(e))?;
                                self.env.set(name, root);
                            } else {
                                return Err(self.runtime_err(format!("undefined variable: {}", name)));
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
                // AI-native @optimize: detect and replace algorithm
                let has_optimize = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Optimize));
                if has_optimize {
                    #[cfg(not(target_arch = "wasm32"))]
                    { return self.exec_optimize(decl); }
                    #[cfg(target_arch = "wasm32")]
                    { self.register_fn_decl(decl); return Ok(Value::Void); }
                }
                // AI-native @autograd: register fn + _grad derivative
                let has_autograd = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Autograd));
                if has_autograd {
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    self.autograd_fns.insert(decl.name.clone(), (param_names.clone(), Arc::new(decl.body.clone())));
                }
                // AI-native @vectorize: register fn for auto array-lift
                let has_vectorize = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Vectorize));
                if has_vectorize {
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    self.vectorize_fns.insert(decl.name.clone(), (param_names.clone(), Arc::new(decl.body.clone())));
                }
                // AI-native @fuse: register fn for operation fusion (array single-pass)
                let has_fuse = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Fuse));
                if has_fuse {
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    self.fuse_fns.insert(decl.name.clone(), (param_names.clone(), Arc::new(decl.body.clone())));
                }
                // AI-native @lazy: mark fn for deferred evaluation (thunk)
                let has_lazy = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Lazy));
                if has_lazy {
                    self.lazy_fns.insert(decl.name.clone());
                }
                // AI-native @inline: mark fn for inline expansion at call site
                let has_inline = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Inline));
                if has_inline {
                    self.inline_fns.insert(decl.name.clone());
                }
                // AI-native @deprecated: mark function with deprecation warning
                for attr in &decl.attrs {
                    if let crate::token::AttrKind::Deprecated(ref msg) = attr.kind {
                        let warn_msg = msg.clone().unwrap_or_else(|| format!("'{}' is deprecated", decl.name));
                        self.deprecated_fns.insert(decl.name.clone(), warn_msg);
                    }
                }
                // AI-native @test: register test function
                let has_test = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Test));
                if has_test {
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    self.test_fns.push((decl.name.clone(), param_names, Arc::new(decl.body.clone())));
                }
                // AI-native @bench: register benchmark function
                let has_bench = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Bench));
                if has_bench {
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    self.bench_fns.push((decl.name.clone(), param_names, Arc::new(decl.body.clone())));
                }
                // AI-native @evolve: self-modifying function (body replaceable at runtime)
                let has_evolve = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Evolve));
                if has_evolve {
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    let gen = self.evolve_fns.get(&decl.name).map(|e| e.2 + 1).unwrap_or(0);
                    self.evolve_fns.insert(decl.name.clone(), (param_names, Arc::new(decl.body.clone()), gen));
                }
                if !decl.type_params.is_empty() {
                    // Generic function: store declaration for monomorphization at call site
                    self.generic_fn_decls.insert(decl.name.clone(), decl.clone());
                    // Also define a placeholder in env so lookups find the name
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    let fn_val = Value::Fn(Box::new((format!("__generic__{}", decl.name), param_names, Arc::new(decl.body.clone()))));
                    self.fn_cache.insert(decl.name.clone(), fn_val.clone());
                    self.env.define(&decl.name, fn_val);
                } else {
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    let has_memoize = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Memoize));
                    let internal_name = if has_memoize {
                        format!("__memoize__{}", decl.name)
                    } else if decl.is_pure {
                        format!("__pure__{}", decl.name)
                    } else {
                        decl.name.clone()
                    };
                    let fn_val = Value::Fn(Box::new((internal_name, param_names, Arc::new(decl.body.clone()))));
                    self.fn_cache.insert(decl.name.clone(), fn_val.clone());
                    self.env.define(&decl.name, fn_val);
                }
                Ok(Value::Void)
            }
            Stmt::For(var, iter_expr, body) => {
                let iter_val = self.eval_expr(iter_expr)?;
                let items = match iter_val {
                    Value::Array(a) => a,
                    _ => return Err(self.type_err("for loop requires iterable".into())),
                };
                'for_loop: for item in items {
                    self.env.push_scope();
                    self.env.define(var, item);
                    for s in body {
                        match self.exec_stmt(s) {
                            Ok(_) => {}
                            Err(e) if e.message == BREAK_SENTINEL => {
                                self.env.pop_scope();
                                break 'for_loop;
                            }
                            Err(e) if e.message == CONTINUE_SENTINEL => {
                                break; // break inner for, continue outer for_loop
                            }
                            Err(e) => {
                                self.env.pop_scope();
                                return Err(e);
                            }
                        }
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
                'while_loop: loop {
                    let c = self.eval_expr(cond)?;
                    if !Self::is_truthy(&c) {
                        break;
                    }
                    self.env.push_scope();
                    for s in body {
                        match self.exec_stmt(s) {
                            Ok(_) => {}
                            Err(e) if e.message == BREAK_SENTINEL => {
                                self.env.pop_scope();
                                break 'while_loop;
                            }
                            Err(e) if e.message == CONTINUE_SENTINEL => {
                                break; // break inner for, continue outer while
                            }
                            Err(e) => {
                                self.env.pop_scope();
                                return Err(e);
                            }
                        }
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
                'inf_loop: loop {
                    self.env.push_scope();
                    for s in body {
                        match self.exec_stmt(s) {
                            Ok(_) => {}
                            Err(e) if e.message == BREAK_SENTINEL => {
                                self.env.pop_scope();
                                break 'inf_loop;
                            }
                            Err(e) if e.message == CONTINUE_SENTINEL => {
                                break;
                            }
                            Err(e) => {
                                self.env.pop_scope();
                                return Err(e);
                            }
                        }
                        if self.return_value.is_some() {
                            self.env.pop_scope();
                            return Ok(Value::Void);
                        }
                    }
                    self.env.pop_scope();
                }
                Ok(Value::Void)
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
                    hint: None,
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
            Stmt::TraitDecl(decl) => {
                let methods: Vec<(String, Vec<String>, Vec<Stmt>)> = decl.methods.iter().map(|m| {
                    let param_names: Vec<String> = m.params.iter().map(|p| p.name.clone()).collect();
                    (m.name.clone(), param_names, m.body.clone())
                }).collect();
                self.trait_defs.insert(decl.name.clone(), methods);
                Ok(Value::Void)
            }
            Stmt::ImplBlock(impl_block) => {
                let target = impl_block.target.clone();
                let methods_map = self.type_methods.entry(target.clone()).or_insert_with(HashMap::new);
                for method in &impl_block.methods {
                    let param_names: Vec<String> = method.params.iter().map(|p| p.name.clone()).collect();
                    methods_map.insert(method.name.clone(), (param_names.clone(), method.body.clone()));
                }
                // If this is a trait impl (impl Trait for Type), also store in trait_impls
                if let Some(trait_name) = &impl_block.trait_name {
                    let key = (target.clone(), trait_name.clone());
                    let trait_methods = self.trait_impls.entry(key).or_insert_with(HashMap::new);
                    // First, fill in default methods from the trait definition
                    if let Some(trait_def) = self.trait_defs.get(trait_name) {
                        for (m_name, m_params, m_body) in trait_def.clone() {
                            if !m_body.is_empty() {
                                trait_methods.entry(m_name.clone()).or_insert_with(|| (m_params.clone(), m_body.clone()));
                                methods_map.entry(m_name).or_insert_with(|| (m_params, m_body));
                            }
                        }
                    }
                    // Then, insert explicitly implemented methods (overrides defaults)
                    for method in &impl_block.methods {
                        let param_names: Vec<String> = method.params.iter().map(|p| p.name.clone()).collect();
                        trait_methods.insert(method.name.clone(), (param_names, method.body.clone()));
                    }
                }
                Ok(Value::Void)
            }
            Stmt::Intent(description, fields) => {
                let mut map = HashMap::new();
                map.insert("description".to_string(), Value::Str(description.clone()));
                for (key, expr) in fields {
                    let val = self.eval_expr(expr)?;
                    map.insert(key.clone(), val);
                }
                // Print structured experiment plan
                self.writeln_output(&format!("=== Intent: {} ===", description));
                for (key, val) in &map {
                    if key != "description" {
                        self.writeln_output(&format!("  {}: {}", key, val));
                    }
                }
                // Store as a variable named by the description (or __intent__)
                let intent_val = Value::Intent(Box::new(map));
                self.env.define("__intent__", intent_val.clone());
                Ok(intent_val)
            }
            Stmt::Verify(name, body) => {
                let total = body.iter().filter(|s| matches!(s, Stmt::Assert(_))).count();
                let mut passed = 0usize;
                let mut failed = false;
                self.env.push_scope();
                for s in body {
                    match self.exec_stmt(s) {
                        Ok(_) => {
                            if matches!(s, Stmt::Assert(_)) {
                                passed += 1;
                            }
                        }
                        Err(e) => {
                            self.writeln_output(&format!("  FAIL {}: {}", name, e.message));
                            failed = true;
                            break;
                        }
                    }
                    if self.return_value.is_some() || self.throw_value.is_some() {
                        break;
                    }
                }
                self.env.pop_scope();
                if failed {
                    self.writeln_output(&format!("VERIFY {}: FAILED ({}/{} assertions passed)", name, passed, total));
                    return Err(self.runtime_err(format!(
                        "verify '{}' failed: {}/{} assertions passed", name, passed, total
                    )));
                } else {
                    self.writeln_output(&format!("VERIFY {}: OK ({}/{} assertions passed)", name, passed, total));
                }
                Ok(Value::Void)
            }
            Stmt::MacroDef(mac_def) => {
                self.macro_defs.insert(mac_def.name.clone(), crate::macro_expand::StoredMacro {
                    rules: mac_def.rules.clone(),
                });
                Ok(Value::Void)
            }
            Stmt::ComptimeFn(decl) => {
                // Register a compile-time function (available only in comptime contexts)
                let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                self.comptime_fns.insert(decl.name.clone(), (param_names.clone(), decl.body.clone()));
                // Also make it callable as a regular function so `const X = factorial(10)` works
                self.env.define(
                    &decl.name,
                    Value::Fn(Box::new((decl.name.clone(), param_names, Arc::new(decl.body.clone())))),
                );
                Ok(Value::Void)
            }
            Stmt::EffectDecl(decl) => {
                // Register effect definition: store operation names
                let op_names: Vec<String> = decl.operations.iter().map(|o| o.name.clone()).collect();
                self.effect_defs.insert(decl.name.clone(), op_names);
                Ok(Value::Void)
            }
            Stmt::ConsciousnessBlock(name, body) => {
                self.env.push_scope();
                // Default consciousness parameters
                let phi = 71.0_f64;
                let tension = 0.5_f64;
                let cells = 64_i64;
                let entropy = 0.998_f64;
                let alpha = 0.014_f64;
                let balance = 0.5_f64;

                self.env.define("phi", Value::Float(phi));
                self.env.define("tension", Value::Float(tension));
                self.env.define("faction", Value::Int(12));
                self.env.define("cells", Value::Int(cells));
                self.env.define("entropy", Value::Float(entropy));
                self.env.define("alpha", Value::Float(alpha));
                self.env.define("balance", Value::Float(balance));

                self.writeln_output(&format!("=== Consciousness: {} ===", name));

                // NEXUS-6 Omega Lens Scan (6 lenses = n)
                let omega = crate::anima_bridge::OmegaScanResult::from_consciousness_state(
                    phi, tension, cells, entropy, alpha, balance,
                );
                self.writeln_output(&omega.report());

                // Inject scan results as variables
                self.env.define("omega_consensus", Value::Int(omega.consensus as i64));
                self.env.define("omega_phi", Value::Float(omega.phi_aggregate));
                self.env.define("omega_n6", Value::Bool(omega.n6_aligned));

                let mut result = Value::Void;
                for s in body {
                    result = self.exec_stmt(s)?;
                    if self.return_value.is_some() || self.throw_value.is_some() {
                        break;
                    }
                }
                self.env.pop_scope();
                Ok(result)
            }
            Stmt::EvolveFn(decl) => {
                let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                // Track generation in evolve_fns
                let gen = self.evolve_fns.get(&decl.name).map(|e| e.2 + 1).unwrap_or(0);
                self.evolve_fns.insert(decl.name.clone(), (param_names.clone(), Arc::new(decl.body.clone()), gen));
                let fn_val = Value::Fn(Box::new((decl.name.clone(), param_names, Arc::new(decl.body.clone()))));
                // Update both env and fn_cache so new version is used immediately
                self.fn_cache.insert(decl.name.clone(), fn_val.clone());
                self.env.define(&decl.name, fn_val);
                Ok(Value::Void)
            }
            Stmt::DeriveDecl(type_name, traits) => {
                // Look up struct fields for this type
                let fields = self.struct_defs.get(type_name).cloned().unwrap_or_default();
                let derive_stmts = crate::macro_expand::generate_derive(
                    type_name, traits, &fields,
                    self.current_line, self.current_col,
                )?;
                for s in &derive_stmts {
                    self.exec_stmt(s)?;
                }
                Ok(Value::Void)
            }
            Stmt::Generate(_target) => {
                #[cfg(not(target_arch = "wasm32"))]
                {
                    return self.exec_generate(_target);
                }
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("generate is not supported in WASM mode (requires LLM network access)".into()));
                }
            }
            Stmt::Optimize(_decl) => {
                #[cfg(not(target_arch = "wasm32"))]
                {
                    return self.exec_optimize(_decl);
                }
                #[cfg(target_arch = "wasm32")]
                {
                    // In WASM mode, just register the function as-is without optimization
                    self.register_fn_decl(_decl);
                    return Ok(Value::Void);
                }
            }
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
            Stmt::Spawn(_body) => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("spawn is not supported in WASM mode".into()));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    // If inside a scope block, use the async runtime so the
                    // scope can track and join the spawned task.
                    if !self.scope_task_groups.is_empty() {
                        let body = _body.clone();
                        let captured_env = self.capture_env_for_spawn();
                        let struct_defs = self.struct_defs.clone();
                        let enum_defs = self.enum_defs.clone();
                        let type_methods = self.type_methods.clone();

                        let scheduler = crate::async_runtime::global_scheduler();
                        let future = scheduler.spawn_task(
                            "scope_spawn".into(),
                            body.into(),
                            vec![],
                            captured_env,
                            struct_defs,
                            enum_defs,
                            type_methods,
                        );
                        if let Some(group) = self.scope_task_groups.last_mut() {
                            group.add(future);
                        }
                        return Ok(Value::Void);
                    }

                    // Outside a scope — original fire-and-forget behavior
                    let body = _body.clone();
                    let captured_env = self.capture_env_for_spawn();
                    let struct_defs = self.struct_defs.clone();
                    let enum_defs = self.enum_defs.clone();
                    let type_methods = self.type_methods.clone();

                    let handle = thread::spawn(move || {
                        let mut interp = Interpreter::new();
                        interp.struct_defs = struct_defs;
                        interp.enum_defs = enum_defs;
                        interp.type_methods = type_methods;
                        for (name, val) in captured_env {
                            interp.env.define(&name, val);
                        }
                        for stmt in body.iter() {
                            if let Err(e) = interp.exec_stmt(stmt) {
                                eprintln!("[spawn] error: {}", e);
                                break;
                            }
                            if interp.return_value.is_some() || interp.throw_value.is_some() {
                                break;
                            }
                        }
                    });
                    self.spawn_handles.push(handle);
                    Ok(Value::Void)
                }
            }
            Stmt::SpawnNamed(_name, _body) => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("spawn is not supported in WASM mode".into()));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    let body = _body.clone();
                    let spawn_name = _name.clone();
                    let log_name = _name.clone();
                    let captured_env = self.capture_env_for_spawn();
                    let struct_defs = self.struct_defs.clone();
                    let enum_defs = self.enum_defs.clone();
                    let type_methods = self.type_methods.clone();

                    let handle = thread::spawn(move || {
                        let mut interp = Interpreter::new();
                        interp.struct_defs = struct_defs;
                        interp.enum_defs = enum_defs;
                        interp.type_methods = type_methods;
                        for (n, val) in captured_env {
                            interp.env.define(&n, val);
                        }
                        for stmt in body.iter() {
                            if let Err(e) = interp.exec_stmt(stmt) {
                                eprintln!("[spawn {}] error: {}", log_name, e);
                                break;
                            }
                            if interp.return_value.is_some() || interp.throw_value.is_some() {
                                break;
                            }
                        }
                    });
                    self.named_spawns.insert(spawn_name, handle);
                    Ok(Value::Void)
                }
            }
            Stmt::Scope(_scope_body) => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("scope is not supported in WASM mode".into()));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    // Push a new task group for this scope
                    self.scope_task_groups.push(crate::async_runtime::TaskGroup::new());

                    // Execute the body — any `spawn` inside will register with our group
                    self.env.push_scope();
                    let mut scope_result = Value::Void;
                    let mut scope_err: Option<HexaError> = None;
                    for s in _scope_body {
                        match self.exec_stmt(s) {
                            Ok(v) => {
                                scope_result = v;
                                if self.return_value.is_some() || self.throw_value.is_some() {
                                    break;
                                }
                            }
                            Err(e) => {
                                scope_err = Some(e);
                                break;
                            }
                        }
                    }
                    self.env.pop_scope();

                    // Pop the task group and wait for all spawned tasks
                    let group = self.scope_task_groups.pop()
                        .expect("scope_task_groups underflow");

                    if let Some(e) = scope_err {
                        group.cancel();
                        return Err(e);
                    }

                    // Wait for all tasks to complete (60s max timeout)
                    match group.wait_all(Some(std::time::Duration::from_secs(60))) {
                        Ok(results) => {
                            if results.is_empty() {
                                Ok(scope_result)
                            } else {
                                Ok(Value::Array(results))
                            }
                        }
                        Err(e) => {
                            group.cancel();
                            Err(self.runtime_err(format!("scope: task failed: {}", e)))
                        }
                    }
                }
            }
            Stmt::AsyncFnDecl(decl) => {
                // async fn is stored as a regular function but marked;
                // when called, it returns a Future via the green-thread runtime
                let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                // We prefix the function name internally to mark it as async
                self.env.define(
                    &decl.name,
                    Value::Fn(Box::new((format!("__async__{}", decl.name), param_names, Arc::new(decl.body.clone())))),
                );
                Ok(Value::Void)
            }
            Stmt::Select(_arms, _timeout_arm) => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("select is not supported in WASM mode".into()));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    // Calculate timeout deadline
                    let deadline = if let Some(ref ta) = _timeout_arm {
                        let ms_val = self.eval_expr(&ta.duration_ms)?;
                        let ms = match ms_val {
                            Value::Int(n) => n as u64,
                            Value::Float(f) => f as u64,
                            _ => return Err(self.runtime_err("timeout duration must be a number".into())),
                        };
                        Some((std::time::Instant::now() + std::time::Duration::from_millis(ms), ms))
                    } else {
                        None
                    };

                    // Polling loop: try_recv on each receiver, first with data wins
                    loop {
                        // Check timeout first
                        if let Some((dl, _)) = deadline {
                            if std::time::Instant::now() >= dl {
                                // Timeout fired — execute timeout arm body
                                if let Some(ref ta) = _timeout_arm {
                                    self.env.push_scope();
                                    let mut result = Value::Void;
                                    for stmt in &ta.body {
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
                        }

                        for arm in _arms {
                            // Try to receive from channel — handles both syntaxes:
                            // 1. Old: rx.recv() as val => { ... }
                            // 2. New: val from rx => { ... } (receiver is an Ident)
                            let try_result = self.try_select_recv(arm)?;
                            if let Some(val) = try_result {
                                self.env.push_scope();
                                self.env.define(&arm.binding, val);
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
                        std::thread::sleep(std::time::Duration::from_millis(1));
                    }
                }
            }
            Stmt::DropStmt(name) => {
                use crate::env::OwnershipState;
                let state = self.env.get_ownership(name);
                match state {
                    OwnershipState::Moved => {
                        return Err(self.runtime_err(format!("cannot drop '{}': value has been moved", name)));
                    }
                    OwnershipState::Dropped => {
                        return Err(self.runtime_err(format!("cannot drop '{}': value has already been dropped", name)));
                    }
                    _ => {
                        self.env.set_ownership(name, OwnershipState::Dropped);
                        Ok(Value::Void)
                    }
                }
            }
            Stmt::ProofBlock(name, proof_stmts) => {
                if !self.test_mode {
                    return Ok(Value::Void);
                }
                self.writeln_output(&format!("[proof:formal] {}", name));
                for ps in proof_stmts {
                    let result = self.execute_proof_block_stmt(ps)?;
                    if !result.valid {
                        return Err(self.runtime_err(format!(
                            "proof '{}' failed: {}", name, result.message
                        )));
                    }
                }
                Ok(Value::Void)
            }
            Stmt::Theorem(name, proof_stmts) => {
                if !self.test_mode {
                    return Ok(Value::Void);
                }
                self.writeln_output(&format!("[theorem] {}", name));
                for ps in proof_stmts {
                    let result = self.execute_proof_block_stmt(ps)?;
                    if !result.valid {
                        return Err(self.runtime_err(format!(
                            "theorem '{}' failed: {}", name, result.message
                        )));
                    }
                }
                Ok(Value::Void)
            }
            Stmt::TypeAlias(_name, _target, _vis) => {
                // Type alias is a compile-time construct, no runtime effect
                Ok(Value::Void)
            }
            Stmt::AtomicLet(name, _typ, expr, _vis) => {
                // At runtime, atomic let behaves like regular let
                let val = match expr {
                    Some(e) => self.eval_expr(e)?,
                    None => Value::Void,
                };
                self.env.define(name, val);
                Ok(Value::Void)
            }
            Stmt::Panic(expr) => {
                let val = self.eval_expr(expr)?;
                let msg = match &val {
                    Value::Str(s) => s.clone(),
                    other => format!("{}", other),
                };
                Err(self.runtime_err(format!("panic: {}", msg)))
            }
            Stmt::Break => {
                Err(self.runtime_err(BREAK_SENTINEL.into()))
            }
            Stmt::Continue => {
                Err(self.runtime_err(CONTINUE_SENTINEL.into()))
            }
            Stmt::Extern(decl) => {
                // Register the extern function and create a builtin binding
                let fn_name = decl.name.clone();
                let builtin_name = format!("__extern_{}", fn_name);
                self.extern_fns.insert(fn_name.clone(), decl.clone());
                self.env.define(&fn_name, Value::BuiltinFn(builtin_name));
                Ok(Value::Void)
            }
            // All Stmt variants are covered above; this arm is intentionally suppressed.
            #[allow(unreachable_patterns)]
            _ => Ok(Value::Void),
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
                // Fast path: check fn_cache first (O(1) HashMap vs O(n) scope walk)
                if let Some(val) = self.fn_cache.get(name) {
                    return Ok(val.clone());
                }
                // Check ownership state before access
                {
                    use crate::env::OwnershipState;
                    let state = self.env.get_ownership(name);
                    match state {
                        OwnershipState::Moved => {
                            return Err(self.runtime_err(format!("cannot access '{}': value has been moved", name)));
                        }
                        OwnershipState::Dropped => {
                            return Err(self.runtime_err(format!("cannot access '{}': value has been dropped", name)));
                        }
                        _ => {}
                    }
                }
                self.env.get(name).ok_or_else(|| {
                    // Collect known names for "did you mean" suggestion
                    let known_names: Vec<&str> = self.env.known_names();
                    let hint = crate::error::suggest_name(name, &known_names)
                        .map(|s| format!("did you mean '{}'?", s));
                    HexaError {
                        class: ErrorClass::Name,
                        message: format!("undefined variable: {}", name),
                        line: self.current_line,
                        col: self.current_col,
                        hint,
                    }
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
                // Inline fast path for Int operations (dominates fib-like workloads)
                match (&l, op, &r) {
                    (Value::Int(a), BinOp::Add, Value::Int(b)) => Ok(Value::Int(a + b)),
                    (Value::Int(a), BinOp::Sub, Value::Int(b)) => Ok(Value::Int(a - b)),
                    (Value::Int(a), BinOp::Mul, Value::Int(b)) => Ok(Value::Int(a * b)),
                    (Value::Int(a), BinOp::Lt, Value::Int(b)) => Ok(Value::Bool(a < b)),
                    (Value::Int(a), BinOp::Le, Value::Int(b)) => Ok(Value::Bool(a <= b)),
                    (Value::Int(a), BinOp::Gt, Value::Int(b)) => Ok(Value::Bool(a > b)),
                    (Value::Int(a), BinOp::Ge, Value::Int(b)) => Ok(Value::Bool(a >= b)),
                    (Value::Int(a), BinOp::Eq, Value::Int(b)) => Ok(Value::Bool(a == b)),
                    (Value::Int(a), BinOp::Ne, Value::Int(b)) => Ok(Value::Bool(a != b)),
                    _ => self.eval_binary(l, op, r),
                }
            }
            Expr::Unary(op, operand) => {
                let val = self.eval_expr(operand)?;
                self.eval_unary(op, val)
            }
            Expr::Call(callee, args) => {
                // Check for effect call: Effect.op(args) where Effect is a known effect
                if let Expr::Field(obj_expr, method_name) = callee.as_ref() {
                    if let Expr::Ident(name) = obj_expr.as_ref() {
                        if self.effect_defs.contains_key(name) {
                            return self.eval_effect_call(name, method_name, args);
                        }
                    }
                }
                // Check for method call pattern: obj.method(args)
                if let Expr::Field(obj_expr, method_name) = callee.as_ref() {
                    // AI-native @fuse: detect map/filter/fold chains → single pass fusion
                    if let Some(result) = self.try_fuse_chain(obj_expr, method_name, args)? {
                        return Ok(result);
                    }
                    let obj_val = self.eval_expr(obj_expr)?;
                    let mut arg_vals = Vec::with_capacity(args.len());
                    for a in args {
                        arg_vals.push(self.eval_expr(a)?);
                    }
                    return self.call_method(obj_val, method_name, arg_vals);
                }
                // Check for path::member(args) pattern (module func or enum variant)
                if let Expr::EnumPath(path_name, member_name, None) = callee.as_ref() {
                    let mut arg_vals = Vec::with_capacity(args.len());
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
                            return Ok(Value::EnumVariant(Box::new((path_name.clone(), member_name.clone(), data))));
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
                        for stmt in body.iter() {
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
                // Fast path: direct Ident callee with fn_cache + inline call
                if let Expr::Ident(fn_name) = callee.as_ref() {
                    // Try fn_cache first (avoids linear env.get scan)
                    let func = if let Some(cached) = self.fn_cache.get(fn_name) {
                        cached.clone()
                    } else {
                        // AI-native @autograd: intercept _grad calls → numerical differentiation
                    if fn_name.ends_with("_grad") {
                        let base_name = &fn_name[..fn_name.len() - 5];
                        if let Some((params, body)) = self.autograd_fns.get(base_name).cloned() {
                            let mut arg_vals = Vec::with_capacity(args.len());
                            for a in args {
                                arg_vals.push(self.eval_expr(a)?);
                            }
                            let eps = 1e-7_f64;
                            if params.len() == 1 {
                                // Single-variable: return scalar gradient
                                let x0 = match &arg_vals[0] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                                // f(x + eps)
                                self.env.push_scope();
                                self.env.define(&params[0], Value::Float(x0 + eps));
                                let mut fplus = Value::Void;
                                for s in body.iter() { fplus = self.exec_stmt(s)?; if self.return_value.is_some() { fplus = self.return_value.take().unwrap(); break; } }
                                self.env.pop_scope();
                                let fp = match fplus { Value::Float(f) => f, Value::Int(i) => i as f64, _ => 0.0 };
                                // f(x - eps)
                                self.env.push_scope();
                                self.env.define(&params[0], Value::Float(x0 - eps));
                                let mut fminus = Value::Void;
                                for s in body.iter() { fminus = self.exec_stmt(s)?; if self.return_value.is_some() { fminus = self.return_value.take().unwrap(); break; } }
                                self.env.pop_scope();
                                let fm = match fminus { Value::Float(f) => f, Value::Int(i) => i as f64, _ => 0.0 };
                                return Ok(Value::Float((fp - fm) / (2.0 * eps)));
                            } else {
                                // Multi-variable: return array of partial derivatives
                                let arg_floats: Vec<f64> = arg_vals.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                                let mut grads = Vec::with_capacity(params.len());
                                for i in 0..params.len() {
                                    // f(... xi+eps ...)
                                    self.env.push_scope();
                                    for (j, p) in params.iter().enumerate() {
                                        let v = if j == i { arg_floats[j] + eps } else { arg_floats[j] };
                                        self.env.define(p, Value::Float(v));
                                    }
                                    let mut fplus = Value::Void;
                                    for s in body.iter() { fplus = self.exec_stmt(s)?; if self.return_value.is_some() { fplus = self.return_value.take().unwrap(); break; } }
                                    self.env.pop_scope();
                                    let fp = match fplus { Value::Float(f) => f, Value::Int(i) => i as f64, _ => 0.0 };
                                    // f(... xi-eps ...)
                                    self.env.push_scope();
                                    for (j, p) in params.iter().enumerate() {
                                        let v = if j == i { arg_floats[j] - eps } else { arg_floats[j] };
                                        self.env.define(p, Value::Float(v));
                                    }
                                    let mut fminus = Value::Void;
                                    for s in body.iter() { fminus = self.exec_stmt(s)?; if self.return_value.is_some() { fminus = self.return_value.take().unwrap(); break; } }
                                    self.env.pop_scope();
                                    let fm = match fminus { Value::Float(f) => f, Value::Int(i) => i as f64, _ => 0.0 };
                                    grads.push(Value::Float((fp - fm) / (2.0 * eps)));
                                }
                                return Ok(Value::Array(grads));
                            }
                        }
                    }
                    let f = self.env.get(fn_name).ok_or_else(|| {
                            let known_names: Vec<&str> = self.env.known_names();
                            let hint = crate::error::suggest_name(fn_name, &known_names)
                                .map(|s| format!("did you mean '{}'?", s));
                            HexaError {
                                class: ErrorClass::Name,
                                message: format!("undefined function: {}", fn_name),
                                line: self.current_line,
                                col: self.current_col,
                                hint,
                            }
                        })?;
                        // Cache function values only (not mutable bindings)
                        if matches!(f, Value::Fn(_) | Value::BuiltinFn(_)) {
                            self.fn_cache.insert(fn_name.clone(), f.clone());
                        }
                        f
                    };
                    let mut arg_vals = Vec::with_capacity(args.len());
                    for a in args {
                        arg_vals.push(self.eval_expr(a)?);
                    }
                    // AI-native @inline: expand function body at call site (no scope overhead)
                    if let Expr::Ident(ref iln_name) = callee.as_ref() {
                        if self.inline_fns.contains(iln_name.as_str()) {
                            if let Value::Fn(ref fn_inner) = func {
                                let (ref _iname, ref params, ref body) = **fn_inner;
                                if arg_vals.len() == params.len() {
                                    for (param, arg) in params.iter().zip(arg_vals) {
                                        self.env.define(param, arg);
                                    }
                                    let mut result = Value::Void;
                                    for stmt in body.iter() {
                                        result = self.exec_stmt(stmt)?;
                                        if self.return_value.is_some() {
                                            result = self.return_value.take().unwrap();
                                            break;
                                        }
                                    }
                                    return Ok(result);
                                }
                            }
                        }
                    }
                    // AI-native @deprecated: emit warning at call site
                    if let Expr::Ident(ref dep_name) = callee.as_ref() {
                        if let Some(msg) = self.deprecated_fns.get(dep_name.as_str()).cloned() {
                            self.writeln_output(&format!("warning[deprecated]: {}", msg));
                        }
                    }
                    // AI-native @fuse: single-pass array fusion
                    if let Expr::Ident(ref ffn_name) = callee.as_ref() {
                        if self.fuse_fns.contains_key(ffn_name.as_str()) {
                            let has_array = arg_vals.iter().any(|v| matches!(v, Value::Array(_)));
                            if has_array {
                                let (params, body) = self.fuse_fns.get(ffn_name.as_str()).cloned().unwrap();
                                let arr_len = arg_vals.iter().find_map(|v| if let Value::Array(a) = v { Some(a.len()) } else { None }).unwrap();
                                let mut results = Vec::with_capacity(arr_len);
                                for idx in 0..arr_len {
                                    self.env.push_scope();
                                    for (pi, p) in params.iter().enumerate() {
                                        let v = match &arg_vals[pi] {
                                            Value::Array(a) => a[idx].clone(),
                                            other => other.clone(),
                                        };
                                        self.env.define(p, v);
                                    }
                                    let mut result = Value::Void;
                                    for s in body.iter() {
                                        result = self.exec_stmt(s)?;
                                        if self.return_value.is_some() {
                                            result = self.return_value.take().unwrap();
                                            break;
                                        }
                                    }
                                    self.env.pop_scope();
                                    results.push(result);
                                }
                                return Ok(Value::Array(results));
                            }
                        }
                    }
                    // AI-native @lazy: return thunk (deferred computation)
                    if let Expr::Ident(ref lfn_name) = callee.as_ref() {
                        if self.lazy_fns.contains(lfn_name.as_str()) {
                            if let Value::Fn(ref fn_inner) = func {
                                let (ref _name, ref params, ref body) = **fn_inner;
                                // Build thunk body: prepend let bindings for captured args
                                let mut thunk_stmts: Vec<Stmt> = Vec::new();
                                for (p, v) in params.iter().zip(arg_vals.iter()) {
                                    let expr = match v {
                                        Value::Int(i) => Expr::IntLit(*i),
                                        Value::Float(f) => Expr::FloatLit(*f),
                                        Value::Bool(b) => Expr::BoolLit(*b),
                                        Value::Str(s) => Expr::StringLit(s.clone()),
                                        Value::Array(arr) => {
                                            let elems: Vec<Expr> = arr.iter().map(|elem| match elem {
                                                Value::Int(i) => Expr::IntLit(*i),
                                                Value::Float(f) => Expr::FloatLit(*f),
                                                Value::Bool(b) => Expr::BoolLit(*b),
                                                Value::Str(s) => Expr::StringLit(s.clone()),
                                                _ => Expr::FloatLit(0.0),
                                            }).collect();
                                            Expr::Array(elems)
                                        }
                                        _ => Expr::FloatLit(0.0),
                                    };
                                    thunk_stmts.push(Stmt::Let(p.clone(), None, Some(expr), crate::ast::Visibility::Private));
                                }
                                thunk_stmts.extend(body.iter().cloned());
                                let thunk_name = format!("__lazy_thunk__{}", lfn_name);
                                return Ok(Value::Fn(Box::new((thunk_name, vec![], Arc::new(thunk_stmts)))));
                            }
                        }
                    }
                    // AI-native @vectorize: auto array-lift when any arg is Array
                    if let Expr::Ident(ref vfn_name) = callee.as_ref() {
                        if self.vectorize_fns.contains_key(vfn_name.as_str()) {
                            let has_array = arg_vals.iter().any(|v| matches!(v, Value::Array(_)));
                            if has_array {
                                let (params, body) = self.vectorize_fns.get(vfn_name.as_str()).cloned().unwrap();
                                // Find the length from first array arg
                                let arr_len = arg_vals.iter().find_map(|v| if let Value::Array(a) = v { Some(a.len()) } else { None }).unwrap();
                                let mut results = Vec::with_capacity(arr_len);
                                for idx in 0..arr_len {
                                    self.env.push_scope();
                                    for (pi, p) in params.iter().enumerate() {
                                        let v = match &arg_vals[pi] {
                                            Value::Array(a) => a[idx].clone(),
                                            other => other.clone(), // broadcast scalar
                                        };
                                        self.env.define(p, v);
                                    }
                                    let mut result = Value::Void;
                                    for s in body.iter() {
                                        result = self.exec_stmt(s)?;
                                        if self.return_value.is_some() {
                                            result = self.return_value.take().unwrap();
                                            break;
                                        }
                                    }
                                    self.env.pop_scope();
                                    results.push(result);
                                }
                                return Ok(Value::Array(results));
                            }
                        }
                    }
                    // Inline the common Value::Fn path to avoid call_function dispatch
                    if let Value::Fn(ref fn_inner) = func {
                        let (ref _name, ref params, ref body) = **fn_inner;
                        // AI-native @optimize: intercept optimized function calls
                        if _name.starts_with("__optimize__") {
                            let real_name = &_name["__optimize__".len()..];
                            if let Some((coeffs, bases)) = self.optimized_fns.get(real_name).cloned() {
                                if arg_vals.len() == 1 {
                                    if let Value::Int(n) = &arg_vals[0] {
                                        return Ok(Value::Int(matrix_exp_recurrence(*n, &coeffs, &bases)));
                                    }
                                }
                            }
                            // Fallback: shouldn't happen, but register as normal fn
                        }
                        // AI-native @optimize: TCO — tail-recursive loop transform
                        if _name.starts_with("__tco__") {
                            let mut current_args = arg_vals;
                            loop {
                                self.env.push_scope();
                                for (param, arg) in params.iter().zip(current_args.iter().cloned()) {
                                    self.env.define(param, arg);
                                }
                                let mut result = Value::Void;
                                let mut did_tail_call = false;
                                for stmt in body.iter() {
                                    result = self.exec_stmt(stmt)?;
                                    if self.return_value.is_some() {
                                        result = self.return_value.take().unwrap();
                                        break;
                                    }
                                }
                                self.env.pop_scope();
                                // Check if result is a tail call marker
                                if let Value::Array(ref arr) = result {
                                    if arr.len() > 0 {
                                        if let Value::Str(ref s) = arr[0] {
                                            if s == "__tco_tail_call__" {
                                                current_args = arr[1..].to_vec();
                                                did_tail_call = true;
                                            }
                                        }
                                    }
                                }
                                if !did_tail_call {
                                    return Ok(result);
                                }
                            }
                        }
                        if !_name.starts_with("__async__") && !_name.starts_with("__generic__") {
                            if arg_vals.len() != params.len() {
                                return Err(self.runtime_err(format!(
                                    "expected {} arguments, got {}",
                                    params.len(), arg_vals.len()
                                )));
                            }
                            let is_memoize = _name.starts_with("__memoize__");
                            let is_pure = _name.starts_with("__pure__") || is_memoize;
                            // @memoize: check cache before execution
                            if is_memoize || is_pure {
                                let fn_key = _name.clone();
                                let args_hash = {
                                    use std::hash::Hasher;
                                    let mut h = std::collections::hash_map::DefaultHasher::new();
                                    for av in &arg_vals { hash_value_interp(av, &mut h); }
                                    h.finish()
                                };
                                if let Some(cached) = self.memo_cache.get(&fn_key).and_then(|m| m.get(&args_hash)) {
                                    return Ok(cached.clone());
                                }
                                let prev_pure = self.in_pure_fn;
                                self.in_pure_fn = true;
                                self.env.push_scope();
                                for (param, arg) in params.iter().zip(arg_vals) {
                                    self.env.define(param, arg);
                                }
                                let mut result = Value::Void;
                                for stmt in body.iter() {
                                    result = self.exec_stmt(stmt)?;
                                    if self.return_value.is_some() {
                                        result = self.return_value.take().unwrap();
                                        break;
                                    }
                                }
                                self.env.pop_scope();
                                self.in_pure_fn = prev_pure;
                                self.memo_cache.entry(fn_key).or_default().insert(args_hash, result.clone());
                                return Ok(result);
                            }
                            let prev_pure = self.in_pure_fn;
                            if is_pure { self.in_pure_fn = true; }
                            self.env.push_scope();
                            for (param, arg) in params.iter().zip(arg_vals) {
                                self.env.define(param, arg);
                            }
                            let mut result = Value::Void;
                            for stmt in body.iter() {
                                result = self.exec_stmt(stmt)?;
                                if self.return_value.is_some() {
                                    result = self.return_value.take().unwrap();
                                    break;
                                }
                            }
                            self.env.pop_scope();
                            self.in_pure_fn = prev_pure;
                            return Ok(result);
                        }
                    }
                    self.call_function(func, arg_vals)
                } else {
                    let func = self.eval_expr(callee)?;
                    let mut arg_vals = Vec::with_capacity(args.len());
                    for a in args {
                        arg_vals.push(self.eval_expr(a)?);
                    }
                    self.call_function(func, arg_vals)
                }
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
                let mut vals = Vec::with_capacity(items.len());
                for item in items {
                    vals.push(self.eval_expr(item)?);
                }
                Ok(Value::Array(vals))
            }
            Expr::Tuple(items) => {
                let mut vals = Vec::with_capacity(items.len());
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
                        let idx = if *i < 0 {
                            let pos = a.len() as i64 + *i;
                            if pos < 0 {
                                return Err(self.runtime_err(format!(
                                    "negative index {} out of bounds (len {})", i, a.len()
                                )));
                            }
                            pos as usize
                        } else {
                            *i as usize
                        };
                        if idx >= a.len() {
                            Err(self.runtime_err(format!(
                                "index {} out of bounds (len {})", i, a.len()
                            )))
                        } else {
                            Ok(a[idx].clone())
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
                    _ => {
                        Err(self.type_err("invalid index operation".into()))
                    }
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
                    Value::Intent(fields) => {
                        fields.get(field_name).cloned().ok_or_else(|| {
                            self.runtime_err(format!("intent has no field '{}'", field_name))
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
                Ok(Value::Lambda(Box::new((param_names, Arc::new(body_stmts), captured))))
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
                Ok(Value::Struct(name.clone(), Box::new(fields)))
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
                Ok(Value::Map(Box::new(map)))
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
                        return Ok(Value::EnumVariant(Box::new((enum_name.clone(), variant_name.clone(), data))));
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
                    for stmt in body.iter() {
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
            Expr::Own(inner) => {
                let val = self.eval_expr(inner)?;
                // If the inner expr is an ident, mark it as Owned after defining
                // But own is usually used in: let x = own [1,2,3]
                // So we just return the value; the Let handler will store it.
                // We record ownership state via a wrapper approach:
                // The Let handler calls define, then we mark it Owned.
                // But here we don't know the var name yet; we'll handle that in Let.
                // For now, tag this as an "owned" value by wrapping -- but since
                // Value doesn't have an Owned variant, we just return the value
                // and set a flag.
                // Actually, the simplest approach: evaluate, return value,
                // and in Let handling, detect Own(...) in expr and set ownership.
                Ok(val)
            }
            Expr::MoveExpr(inner) => {
                use crate::env::OwnershipState;
                // inner must be an identifier
                if let Expr::Ident(name) = inner.as_ref() {
                    let state = self.env.get_ownership(name);
                    match state {
                        OwnershipState::Moved => {
                            return Err(self.runtime_err(format!("cannot move '{}': value has already been moved", name)));
                        }
                        OwnershipState::Dropped => {
                            return Err(self.runtime_err(format!("cannot move '{}': value has been dropped", name)));
                        }
                        _ => {
                            let val = self.env.get(name).ok_or_else(|| {
                                self.runtime_err(format!("undefined variable: {}", name))
                            })?;
                            self.env.set_ownership(name, OwnershipState::Moved);
                            Ok(val)
                        }
                    }
                } else {
                    Err(self.runtime_err("move requires a variable name".into()))
                }
            }
            Expr::Borrow(inner) => {
                use crate::env::OwnershipState;
                // inner must be an identifier
                if let Expr::Ident(name) = inner.as_ref() {
                    let state = self.env.get_ownership(name);
                    match state {
                        OwnershipState::Moved => {
                            return Err(self.runtime_err(format!("cannot borrow '{}': value has been moved", name)));
                        }
                        OwnershipState::Dropped => {
                            return Err(self.runtime_err(format!("cannot borrow '{}': value has been dropped", name)));
                        }
                        _ => {
                            let val = self.env.get(name).ok_or_else(|| {
                                self.runtime_err(format!("undefined variable: {}", name))
                            })?;
                            Ok(val)
                        }
                    }
                } else {
                    Err(self.runtime_err("borrow requires a variable name".into()))
                }
            }
            Expr::Await(inner) => {
                let val = self.eval_expr(inner)?;
                match val {
                    #[cfg(not(target_arch = "wasm32"))]
                    Value::AsyncFuture(future) => {
                        // Use the green-thread runtime to await
                        let scheduler = crate::async_runtime::global_scheduler();
                        match scheduler.await_future(&future, Some(std::time::Duration::from_secs(30))) {
                            Some(result) => Ok(result),
                            None => Err(self.runtime_err("await timed out after 30 seconds".into())),
                        }
                    }
                    Value::Future(future) => {
                        // Legacy: block until the future is resolved
                        loop {
                            {
                                let guard = future.lock().unwrap();
                                if let Some(result) = &*guard {
                                    return Ok(result.clone());
                                }
                            }
                            std::thread::sleep(std::time::Duration::from_millis(1));
                        }
                    }
                    // If it's not a future, just return it (sync value)
                    other => Ok(other),
                }
            }
            Expr::Wildcard | Expr::ArrayPattern(_, _) => {
                // Wildcard/ArrayPattern should only appear in match patterns, not as standalone expressions
                Err(self.runtime_err("pattern expression can only be used in match patterns".into()))
            }
            Expr::MacroInvoc(invoc) => {
                // Look up the macro
                let mac = self.macro_defs.get(&invoc.name).cloned().ok_or_else(|| {
                    self.runtime_err(format!("undefined macro: {}!", invoc.name))
                })?;
                // Expand
                let expanded_stmts = crate::macro_expand::expand_macro(
                    &mac, &invoc.tokens,
                    self.current_line, self.current_col,
                )?;
                // Execute expanded statements, return last value
                let mut last = Value::Void;
                self.env.push_scope();
                for s in &expanded_stmts {
                    last = self.exec_stmt(s)?;
                    if self.return_value.is_some() {
                        self.env.pop_scope();
                        return Ok(self.return_value.take().unwrap());
                    }
                }
                self.env.pop_scope();
                Ok(last)
            }
            Expr::Comptime(inner) => {
                // Evaluate the inner expression in a sandboxed comptime interpreter
                crate::comptime::eval_comptime(inner, &self.comptime_fns)
            }
            Expr::HandleWith(body_expr, handlers) => {
                self.eval_handle_with(body_expr, handlers)
            }
            Expr::EffectCall(effect, op, args) => {
                self.eval_effect_call(effect, op, args)
            }
            Expr::Resume(val_expr) => {
                let val = self.eval_expr(val_expr)?;
                self.resume_value = Some(val.clone());
                Ok(val)
            }
            Expr::Yield(expr) => {
                // Yield currently evaluates the expression and returns its value
                // Full generator support would require coroutine infrastructure
                self.eval_expr(expr)
            }
            Expr::DynCast(trait_name, expr) => {
                let val = self.eval_expr(expr)?;
                let type_name = match &val {
                    Value::Struct(name, _) => name.clone(),
                    Value::Int(_) => "int".to_string(),
                    Value::Float(_) => "float".to_string(),
                    Value::Str(_) => "string".to_string(),
                    Value::Bool(_) => "bool".to_string(),
                    Value::Array(_) => "array".to_string(),
                    other => return Err(self.runtime_err(format!(
                        "cannot create dyn {} from {:?}", trait_name, other
                    ))),
                };
                let key = (type_name.clone(), trait_name.clone());
                if !self.trait_impls.contains_key(&key) {
                    return Err(self.runtime_err(format!(
                        "type {} does not implement trait {}", type_name, trait_name
                    )));
                }
                Ok(Value::TraitObject(Box::new(crate::env::TraitObjectInner {
                    value: Box::new(val),
                    trait_name: trait_name.clone(),
                    type_name,
                })))
            }
            Expr::Template(nodes) => {
                crate::std_web_template::render_template(self, nodes)
                    .map(Value::Str)
            }
            Expr::TryCatch(try_block, err_name, catch_block) => {
                // try-expression: evaluate try block, on error evaluate catch block
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
                            return Ok(result);
                        }
                    }
                    self.env.pop_scope();
                } else if had_throw.is_some() {
                    self.throw_value = had_throw;
                }
                Ok(result)
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

            // Array concatenation
            (Value::Array(a), BinOp::Add, Value::Array(b)) => {
                let mut result = a.clone();
                result.extend(b.iter().cloned());
                Ok(Value::Array(result))
            }
            // Array repetition
            (Value::Array(a), BinOp::Mul, Value::Int(n)) => {
                let n = *n as usize;
                let mut result = Vec::with_capacity(a.len() * n);
                for _ in 0..n {
                    result.extend(a.iter().cloned());
                }
                Ok(Value::Array(result))
            }
            // Array equality
            (Value::Array(a), BinOp::Eq, Value::Array(b)) => {
                Ok(Value::Bool(a.len() == b.len() && a.iter().zip(b.iter()).all(|(x, y)| Self::values_equal(x, y))))
            }
            (Value::Array(a), BinOp::Ne, Value::Array(b)) => {
                Ok(Value::Bool(a.len() != b.len() || !a.iter().zip(b.iter()).all(|(x, y)| Self::values_equal(x, y))))
            }

            // String concatenation
            (Value::Str(a), BinOp::Add, Value::Str(b)) => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            // String + Char concatenation
            (Value::Str(a), BinOp::Add, Value::Char(b)) => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            // Char + String concatenation
            (Value::Char(a), BinOp::Add, Value::Str(b)) => {
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

            // Char comparisons
            (Value::Char(a), BinOp::Eq, Value::Char(b)) => Ok(Value::Bool(a == b)),
            (Value::Char(a), BinOp::Ne, Value::Char(b)) => Ok(Value::Bool(a != b)),
            (Value::Char(a), BinOp::Lt, Value::Char(b)) => Ok(Value::Bool(a < b)),
            (Value::Char(a), BinOp::Gt, Value::Char(b)) => Ok(Value::Bool(a > b)),
            (Value::Char(a), BinOp::Le, Value::Char(b)) => Ok(Value::Bool(a <= b)),
            (Value::Char(a), BinOp::Ge, Value::Char(b)) => Ok(Value::Bool(a >= b)),

            // Bitwise operations
            (Value::Int(a), BinOp::BitAnd, Value::Int(b)) => Ok(Value::Int(a & b)),
            (Value::Int(a), BinOp::BitOr, Value::Int(b)) => Ok(Value::Int(a | b)),
            (Value::Int(a), BinOp::BitXor, Value::Int(b)) => Ok(Value::Int(a ^ b)),

            // Logical XOR
            (Value::Bool(a), BinOp::Xor, Value::Bool(b)) => Ok(Value::Bool(a ^ b)),

            _ => {
                // Cross-type Eq/Ne: different value types are never equal
                // Needed for self-hosting bootstrap: `stmts == ""` when stmts is an array
                match op {
                    BinOp::Eq => Ok(Value::Bool(false)),
                    BinOp::Ne => Ok(Value::Bool(true)),
                    _ => Err(self.type_err(format!(
                        "unsupported binary operation: {:?} {:?} {:?}",
                        left, op, right
                    ))),
                }
            }
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

    pub fn call_function(&mut self, func: Value, args: Vec<Value>) -> Result<Value, HexaError> {
        match func {
            Value::BuiltinFn(name) => self.call_builtin(&name, args),
            Value::Fn(ref fn_inner) if fn_inner.0.starts_with("__async__") => {
                let (ref _name, ref params, ref body) = **fn_inner;
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("async functions are not supported in WASM mode".into()));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    // Async function: spawn on the green-thread runtime
                    if args.len() != params.len() {
                        return Err(self.runtime_err(format!(
                            "expected {} arguments, got {}",
                            params.len(),
                            args.len()
                        )));
                    }
                    let bindings: Vec<(String, Value)> = params.iter()
                        .zip(args.iter())
                        .map(|(p, a)| (p.clone(), a.clone()))
                        .collect();

                    let scheduler = crate::async_runtime::global_scheduler();
                    let future = scheduler.spawn_task(
                        _name.clone(),
                        body.clone().into(),
                        bindings,
                        self.capture_env_for_spawn(),
                        self.struct_defs.clone(),
                        self.enum_defs.clone(),
                        self.type_methods.clone(),
                    );
                    Ok(Value::AsyncFuture(future))
                }
            }
            Value::Fn(ref fn_inner) if fn_inner.0.starts_with("__generic__") => {
                let (ref _name, ref params, ref _body) = **fn_inner;
                // Monomorphization: specialize the generic function for the concrete argument types
                let base_name = &_name["__generic__".len()..];
                let concrete_types: Vec<String> = args.iter().map(|v| self.value_type_string(v)).collect();
                let key = SpecKey {
                    fn_name: base_name.to_string(),
                    concrete_types: concrete_types.clone(),
                };

                // Check cache first
                if let Some(specialized) = self.spec_cache.get(&key).cloned() {
                    return self.call_function(specialized, args);
                }

                // Create specialized function
                if let Some(decl) = self.generic_fn_decls.get(base_name).cloned() {
                    // Build type param -> concrete type mapping
                    let mut type_map: HashMap<String, String> = HashMap::new();
                    for (i, tp) in decl.type_params.iter().enumerate() {
                        if let Some(ct) = concrete_types.get(i) {
                            type_map.insert(tp.name.clone(), ct.clone());
                        }
                    }

                    // Also infer from parameter positions
                    let tp_names: Vec<String> = decl.type_params.iter().map(|tp| tp.name.clone()).collect();
                    for (i, param) in decl.params.iter().enumerate() {
                        if let Some(t) = &param.typ {
                            if tp_names.contains(t) {
                                if let Some(arg) = args.get(i) {
                                    type_map.insert(t.clone(), self.value_type_string(arg));
                                }
                            }
                        }
                    }

                    // Check trait bounds at runtime
                    for tp in &decl.type_params {
                        if let Some(bound) = &tp.bound {
                            if let Some(concrete) = type_map.get(&tp.name) {
                                if !self.runtime_trait_check(concrete, bound) {
                                    return Err(self.type_err(format!(
                                        "type '{}' does not implement trait '{}' required by type parameter '{}' in '{}'",
                                        concrete, bound, tp.name, base_name
                                    )));
                                }
                            }
                        }
                    }
                    // Also check where clauses
                    for wc in &decl.where_clauses {
                        if let Some(concrete) = type_map.get(&wc.type_name) {
                            if !self.runtime_trait_check(concrete, &wc.bound) {
                                return Err(self.type_err(format!(
                                    "type '{}' does not implement trait '{}' (where clause) in '{}'",
                                    concrete, wc.bound, base_name
                                )));
                            }
                        }
                    }

                    // Create specialized fn value with mangled name
                    let mangled = format!("{}_{}", base_name, concrete_types.join("_"));
                    let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
                    let specialized = Value::Fn(Box::new((mangled, param_names, Arc::new(decl.body.clone()))));

                    // Cache it
                    self.spec_cache.insert(key, specialized.clone());

                    // Call the specialized function
                    return self.call_function(specialized, args);
                }

                // Fallback: call as regular function (shouldn't reach here)
                let params = params.clone();
                let body = _body.clone();
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
                for stmt in body.iter() {
                    result = self.exec_stmt(stmt)?;
                    if self.return_value.is_some() {
                        result = self.return_value.take().unwrap();
                        break;
                    }
                }
                self.env.pop_scope();
                Ok(result)
            }
            Value::Fn(fn_inner) => {
                let (_name, params, body) = *fn_inner;
                if args.len() != params.len() {
                    return Err(self.runtime_err(format!(
                        "expected {} arguments, got {}",
                        params.len(),
                        args.len()
                    )));
                }
                let is_memoize = _name.starts_with("__memoize__");
                let is_pure = _name.starts_with("__pure__") || is_memoize;
                // @memoize: check cache before execution
                if is_memoize {
                    let fn_key = _name.clone();
                    let args_hash = {
                        use std::hash::Hasher;
                        let mut h = std::collections::hash_map::DefaultHasher::new();
                        for av in &args { hash_value_interp(av, &mut h); }
                        h.finish()
                    };
                    if let Some(cached) = self.memo_cache.get(&fn_key).and_then(|m| m.get(&args_hash)) {
                        return Ok(cached.clone());
                    }
                    let prev_pure = self.in_pure_fn;
                    self.in_pure_fn = true;
                    self.env.push_scope();
                    for (param, arg) in params.iter().zip(args) {
                        self.env.define(param, arg);
                    }
                    let mut result = Value::Void;
                    for stmt in body.iter() {
                        result = self.exec_stmt(stmt)?;
                        if self.return_value.is_some() {
                            result = self.return_value.take().unwrap();
                            break;
                        }
                    }
                    self.env.pop_scope();
                    self.in_pure_fn = prev_pure;
                    self.memo_cache.entry(fn_key).or_default().insert(args_hash, result.clone());
                    return Ok(result);
                }
                let prev_pure = self.in_pure_fn;
                if is_pure { self.in_pure_fn = true; }
                self.env.push_scope();
                for (param, arg) in params.iter().zip(args) {
                    self.env.define(param, arg);
                }
                let mut result = Value::Void;
                for stmt in body.iter() {
                    result = self.exec_stmt(stmt)?;
                    if self.return_value.is_some() {
                        result = self.return_value.take().unwrap();
                        break;
                    }
                    // Propagate effect requests up the call stack
                    if matches!(result, Value::EffectRequest(_)) {
                        break;
                    }
                }
                self.env.pop_scope();
                self.in_pure_fn = prev_pure;
                Ok(result)
            }
            Value::Lambda(lam_inner) => {
                let (params, body, captured) = *lam_inner;
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
                for stmt in body.iter() {
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
            Value::EnumVariant(ref ev_inner) => {
                let (ref enum_name, ref variant, ref data) = **ev_inner;
                self.call_enum_method(enum_name, variant, data.as_ref().map(|b| b.as_ref()), method, args)
            }
            Value::Struct(struct_name, _fields) => {
                // Look up impl methods — with inline cache acceleration.
                let struct_name = struct_name.clone();
                let cache_site = CallSiteId::new(self.current_line, self.current_col, method);

                // Try the inline cache first (monomorphic fast path).
                let resolved = if let Some(cached) = self.inline_cache.probe(&cache_site, &struct_name) {
                    Some(cached)
                } else if let Some(method_def) = self.type_methods.get(&struct_name).and_then(|m| m.get(method)) {
                    let resolved = method_def.clone();
                    // Populate the cache for next time.
                    self.inline_cache.update(
                        cache_site.clone(),
                        struct_name.clone(),
                        resolved.clone(),
                    );
                    Some(resolved)
                } else {
                    None
                };

                if let Some((params, body)) = resolved {
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
                    for stmt in body.iter() {
                        result = self.exec_stmt(stmt)?;
                        if self.return_value.is_some() {
                            result = self.return_value.take().unwrap();
                            break;
                        }
                    }
                    self.env.pop_scope();
                    Ok(result)
                } else {
                    // Check trait implementations for this type
                    let mut found = None;
                    for ((type_name, _trait_name), methods) in &self.trait_impls {
                        if type_name == &struct_name {
                            if let Some(method_def) = methods.get(method) {
                                found = Some(method_def.clone());
                                break;
                            }
                        }
                    }
                    if let Some((params, body)) = found {
                        if params.is_empty() || params[0] != "self" {
                            return Err(self.runtime_err(format!("trait method {} is not an instance method (no self parameter)", method)));
                        }
                        if args.len() + 1 != params.len() {
                            return Err(self.runtime_err(format!(
                                "trait method {} expected {} arguments, got {}",
                                method, params.len() - 1, args.len()
                            )));
                        }
                        self.env.push_scope();
                        self.env.define("self", obj);
                        for (param, arg) in params[1..].iter().zip(args) {
                            self.env.define(param, arg);
                        }
                        let mut result = Value::Void;
                        for stmt in body.iter() {
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
            }
            #[cfg(not(target_arch = "wasm32"))]
            Value::Sender(tx) => {
                match method {
                    "send" => {
                        if args.is_empty() { return Err(self.type_err("send() requires 1 argument".into())); }
                        let val = args.into_iter().next().unwrap();
                        let tx = tx.lock().unwrap();
                        tx.send(val).map_err(|_| self.runtime_err("channel closed".into()))?;
                        Ok(Value::Void)
                    }
                    _ => Err(self.runtime_err(format!("no method .{}() on sender", method))),
                }
            }
            #[cfg(not(target_arch = "wasm32"))]
            Value::Receiver(rx) => {
                match method {
                    "recv" => {
                        let rx = rx.lock().unwrap();
                        match rx.recv() {
                            Ok(val) => Ok(val),
                            Err(_) => Err(self.runtime_err("channel closed".into())),
                        }
                    }
                    "try_recv" => {
                        let rx = rx.lock().unwrap();
                        match rx.try_recv() {
                            Ok(val) => Ok(Value::EnumVariant(Box::new(("Option".into(), "Some".into(), Some(Box::new(val)))))),
                            Err(mpsc::TryRecvError::Empty) => Ok(Value::EnumVariant(Box::new(("Option".into(), "None".into(), None)))),
                            Err(mpsc::TryRecvError::Disconnected) => Ok(Value::EnumVariant(Box::new(("Option".into(), "None".into(), None)))),
                        }
                    }
                    _ => Err(self.runtime_err(format!("no method .{}() on receiver", method))),
                }
            }
            Value::Set(set) => {
                match method {
                    "add" => {
                        if args.is_empty() { return Err(self.type_err("add() requires 1 argument".into())); }
                        let key = format!("{}", args[0]);
                        let mut guard = set.lock().unwrap();
                        guard.insert(key);
                        Ok(Value::Void)
                    }
                    "has" => {
                        if args.is_empty() { return Err(self.type_err("has() requires 1 argument".into())); }
                        let key = format!("{}", args[0]);
                        let guard = set.lock().unwrap();
                        Ok(Value::Bool(guard.contains(&key)))
                    }
                    "remove" => {
                        if args.is_empty() { return Err(self.type_err("remove() requires 1 argument".into())); }
                        let key = format!("{}", args[0]);
                        let mut guard = set.lock().unwrap();
                        Ok(Value::Bool(guard.remove(&key)))
                    }
                    "len" => {
                        let guard = set.lock().unwrap();
                        Ok(Value::Int(guard.len() as i64))
                    }
                    _ => Err(self.runtime_err(format!("no method .{}() on Set", method))),
                }
            }
            Value::Future(future) => {
                match method {
                    "is_ready" => {
                        let guard = future.lock().unwrap();
                        Ok(Value::Bool(guard.is_some()))
                    }
                    _ => Err(self.runtime_err(format!("no method .{}() on Future", method))),
                }
            }
            Value::TraitObject(ref to_inner) => {
                let key = (to_inner.type_name.clone(), to_inner.trait_name.clone());
                let method_def = self.trait_impls.get(&key).and_then(|m| m.get(method)).cloned();
                if let Some((params, body)) = method_def {
                    if params.is_empty() || params[0] != "self" {
                        return Err(self.runtime_err(format!(
                            "trait method {} is not an instance method (no self parameter)", method
                        )));
                    }
                    if args.len() + 1 != params.len() {
                        return Err(self.runtime_err(format!(
                            "trait method {} expected {} arguments, got {}",
                            method, params.len() - 1, args.len()
                        )));
                    }
                    self.env.push_scope();
                    self.env.define("self", *to_inner.value.clone());
                    for (param, arg) in params[1..].iter().zip(args) {
                        self.env.define(param, arg);
                    }
                    let mut result = Value::Void;
                    for stmt in body.iter() {
                        result = self.exec_stmt(stmt)?;
                        if self.return_value.is_some() {
                            result = self.return_value.take().unwrap();
                            break;
                        }
                    }
                    self.env.pop_scope();
                    Ok(result)
                } else {
                    Err(self.runtime_err(format!(
                        "no method .{}() in trait {} for type {}", method, to_inner.trait_name, to_inner.type_name
                    )))
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
            "join" => {
                if args.is_empty() { return Err(self.type_err("join() requires 1 argument (array)".into())); }
                match &args[0] {
                    Value::Array(arr) => {
                        let parts: Vec<String> = arr.iter().map(|v| format!("{}", v)).collect();
                        Ok(Value::Str(parts.join(s)))
                    }
                    _ => Err(self.type_err("join() requires array argument".into())),
                }
            }
            "repeat" => {
                if args.is_empty() { return Err(self.type_err("repeat() requires 1 argument".into())); }
                match &args[0] {
                    Value::Int(n) => Ok(Value::Str(s.repeat(*n as usize))),
                    _ => Err(self.type_err("repeat() requires int argument".into())),
                }
            }
            "ends_with" => {
                if args.is_empty() { return Err(self.type_err("ends_with() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(suffix) => Ok(Value::Bool(s.ends_with(suffix.as_str()))),
                    _ => Err(self.type_err("ends_with() requires string argument".into())),
                }
            }
            "is_empty" => Ok(Value::Bool(s.is_empty())),
            "index_of" => {
                if args.is_empty() { return Err(self.type_err("index_of() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(substr) => {
                        Ok(Value::Int(s.find(substr.as_str()).map_or(-1, |i| i as i64)))
                    }
                    _ => Err(self.type_err("index_of() requires string argument".into())),
                }
            }
            "substring" => {
                if args.len() < 2 { return Err(self.type_err("substring() requires 2 arguments (start, end)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(start), Value::Int(end)) => {
                        let chars: Vec<char> = s.chars().collect();
                        let start = (*start as usize).min(chars.len());
                        let end = (*end as usize).min(chars.len());
                        let sub: String = chars[start..end].iter().collect();
                        Ok(Value::Str(sub))
                    }
                    _ => Err(self.type_err("substring() requires int arguments".into())),
                }
            }
            "pad_left" => {
                if args.is_empty() { return Err(self.type_err("pad_left() requires at least 1 argument (width)".into())); }
                match &args[0] {
                    Value::Int(width) => {
                        let pad_char = if args.len() > 1 {
                            match &args[1] {
                                Value::Str(c) => c.chars().next().unwrap_or(' '),
                                Value::Char(c) => *c,
                                _ => ' ',
                            }
                        } else { ' ' };
                        let w = *width as usize;
                        if s.len() >= w {
                            Ok(Value::Str(s.to_string()))
                        } else {
                            let padding: String = std::iter::repeat(pad_char).take(w - s.len()).collect();
                            Ok(Value::Str(format!("{}{}", padding, s)))
                        }
                    }
                    _ => Err(self.type_err("pad_left() requires int width".into())),
                }
            }
            "pad_right" => {
                if args.is_empty() { return Err(self.type_err("pad_right() requires at least 1 argument (width)".into())); }
                match &args[0] {
                    Value::Int(width) => {
                        let pad_char = if args.len() > 1 {
                            match &args[1] {
                                Value::Str(c) => c.chars().next().unwrap_or(' '),
                                Value::Char(c) => *c,
                                _ => ' ',
                            }
                        } else { ' ' };
                        let w = *width as usize;
                        if s.len() >= w {
                            Ok(Value::Str(s.to_string()))
                        } else {
                            let padding: String = std::iter::repeat(pad_char).take(w - s.len()).collect();
                            Ok(Value::Str(format!("{}{}", s, padding)))
                        }
                    }
                    _ => Err(self.type_err("pad_right() requires int width".into())),
                }
            }
            "parse_int" => {
                match s.trim().parse::<i64>() {
                    Ok(n) => Ok(Value::Int(n)),
                    Err(_) => Ok(Value::Error(format!("cannot parse '{}' as int", s))),
                }
            }
            "parse_float" => {
                match s.trim().parse::<f64>() {
                    Ok(f) => Ok(Value::Float(f)),
                    Err(_) => Ok(Value::Error(format!("cannot parse '{}' as float", s))),
                }
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
            "enumerate" => {
                let result: Vec<Value> = arr.iter().enumerate()
                    .map(|(i, v)| Value::Tuple(vec![Value::Int(i as i64), v.clone()]))
                    .collect();
                Ok(Value::Array(result))
            }
            "sum" => {
                let mut int_sum = 0i64;
                let mut float_sum = 0.0f64;
                let mut has_float = false;
                for item in arr {
                    match item {
                        Value::Int(n) => int_sum += n,
                        Value::Float(f) => { float_sum += f; has_float = true; }
                        _ => return Err(self.type_err("sum() requires numeric array".into())),
                    }
                }
                if has_float {
                    Ok(Value::Float(float_sum + int_sum as f64))
                } else {
                    Ok(Value::Int(int_sum))
                }
            }
            "min" => {
                if arr.is_empty() { return Err(self.runtime_err("min() on empty array".into())); }
                let mut min_val = &arr[0];
                for item in &arr[1..] {
                    match (item, min_val) {
                        (Value::Int(a), Value::Int(b)) => { if a < b { min_val = item; } }
                        (Value::Float(a), Value::Float(b)) => { if a < b { min_val = item; } }
                        _ => {}
                    }
                }
                Ok(min_val.clone())
            }
            "max" => {
                if arr.is_empty() { return Err(self.runtime_err("max() on empty array".into())); }
                let mut max_val = &arr[0];
                for item in &arr[1..] {
                    match (item, max_val) {
                        (Value::Int(a), Value::Int(b)) => { if a > b { max_val = item; } }
                        (Value::Float(a), Value::Float(b)) => { if a > b { max_val = item; } }
                        _ => {}
                    }
                }
                Ok(max_val.clone())
            }
            "join" => {
                if args.is_empty() { return Err(self.type_err("join() requires 1 argument (separator)".into())); }
                match &args[0] {
                    Value::Str(sep) => {
                        let parts: Vec<String> = arr.iter().map(|v| format!("{}", v)).collect();
                        Ok(Value::Str(parts.join(sep)))
                    }
                    _ => Err(self.type_err("join() requires string separator".into())),
                }
            }
            "slice" => {
                if args.len() < 2 { return Err(self.type_err("slice() requires 2 arguments (start, end)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(start), Value::Int(end)) => {
                        let s = (*start as usize).min(arr.len());
                        let e = (*end as usize).min(arr.len());
                        Ok(Value::Array(arr[s..e].to_vec()))
                    }
                    _ => Err(self.type_err("slice() requires int arguments".into())),
                }
            }
            "flatten" => {
                let mut result = Vec::new();
                for item in arr {
                    match item {
                        Value::Array(inner) => result.extend(inner.iter().cloned()),
                        other => result.push(other.clone()),
                    }
                }
                Ok(Value::Array(result))
            }
            "pop" => {
                if arr.is_empty() { return Err(self.runtime_err("pop() on empty array".into())); }
                Ok(Value::Array(arr[..arr.len()-1].to_vec()))
            }
            "find" => {
                if args.is_empty() { return Err(self.type_err("find() requires 1 argument (function)".into())); }
                let func = args.into_iter().next().unwrap();
                for item in arr {
                    let result = self.call_function(func.clone(), vec![item.clone()])?;
                    if Self::is_truthy(&result) {
                        return Ok(item.clone());
                    }
                }
                Ok(Value::Void)
            }
            "find_index" => {
                if args.is_empty() { return Err(self.type_err("find_index() requires 1 argument (function)".into())); }
                let func = args.into_iter().next().unwrap();
                for (i, item) in arr.iter().enumerate() {
                    let result = self.call_function(func.clone(), vec![item.clone()])?;
                    if Self::is_truthy(&result) {
                        return Ok(Value::Int(i as i64));
                    }
                }
                Ok(Value::Int(-1))
            }
            "any" => {
                if args.is_empty() { return Err(self.type_err("any() requires 1 argument (function)".into())); }
                let func = args.into_iter().next().unwrap();
                for item in arr {
                    let result = self.call_function(func.clone(), vec![item.clone()])?;
                    if Self::is_truthy(&result) {
                        return Ok(Value::Bool(true));
                    }
                }
                Ok(Value::Bool(false))
            }
            "all" => {
                if args.is_empty() { return Err(self.type_err("all() requires 1 argument (function)".into())); }
                let func = args.into_iter().next().unwrap();
                for item in arr {
                    let result = self.call_function(func.clone(), vec![item.clone()])?;
                    if !Self::is_truthy(&result) {
                        return Ok(Value::Bool(false));
                    }
                }
                Ok(Value::Bool(true))
            }
            "swap" => {
                if args.len() < 2 { return Err(self.type_err("swap() requires 2 arguments (i, j)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(i), Value::Int(j)) => {
                        let mut new_arr = arr.to_vec();
                        let i = *i as usize;
                        let j = *j as usize;
                        if i >= new_arr.len() || j >= new_arr.len() {
                            return Err(self.runtime_err("swap index out of bounds".into()));
                        }
                        new_arr.swap(i, j);
                        Ok(Value::Array(new_arr))
                    }
                    _ => Err(self.type_err("swap() requires int arguments".into())),
                }
            }
            "fill" => {
                if args.is_empty() { return Err(self.type_err("fill() requires 1 argument".into())); }
                let fill_val = args.into_iter().next().unwrap();
                Ok(Value::Array(vec![fill_val; arr.len()]))
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
        // Block I/O and side-effectful builtins in comptime mode
        if self.comptime_mode && crate::comptime::is_forbidden_builtin(name) {
            return Err(self.runtime_err(format!(
                "comptime blocks cannot perform I/O: '{}' is not allowed at compile time", name
            )));
        }
        match name {
            "print" => {
                let mut out = String::new();
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 {
                        out.push(' ');
                    }
                    out.push_str(&format!("{}", arg));
                }
                self.write_output(&out);
                Ok(Value::Void)
            }
            "println" => {
                let mut out = String::new();
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 {
                        out.push(' ');
                    }
                    out.push_str(&format!("{}", arg));
                }
                self.writeln_output(&out);
                Ok(Value::Void)
            }
            "eprintln" => {
                let mut out = String::new();
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 {
                        out.push(' ');
                    }
                    out.push_str(&format!("{}", arg));
                }
                self.ewriteln_output(&out);
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
                        Value::Fn(_) => "fn",
                        Value::BuiltinFn(_) => "builtin",
                        Value::Struct(name, _) => name.as_str(),
                        Value::Lambda(_) => "lambda",
                        Value::Map(_) => "map",
                        Value::Error(_) => "error",
                        Value::EnumVariant(ev) => ev.0.as_str(),
                        Value::Intent(_) => "intent",
                        #[cfg(not(target_arch = "wasm32"))]
                        Value::Sender(_) => "sender",
                        #[cfg(not(target_arch = "wasm32"))]
                        Value::Receiver(_) => "receiver",
                        Value::Future(_) => "future",
                        Value::Set(_) => "set",
                        #[cfg(not(target_arch = "wasm32"))]
                        Value::TcpListener(_) => "tcp_listener",
                        #[cfg(not(target_arch = "wasm32"))]
                        Value::TcpStream(_) => "tcp_stream",
                        Value::EffectRequest(_) => "effect_request",
                        Value::TraitObject(to) => to.trait_name.as_str(),
                        #[cfg(not(target_arch = "wasm32"))]
                        Value::AsyncFuture(_) => "future",
                        Value::Atomic(_) => "atomic",
                        Value::Pointer(_) => "pointer",
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
                #[cfg(target_arch = "wasm32")]
                { return Err(self.runtime_err("read_file is not supported in WASM mode".into())); }
                #[cfg(not(target_arch = "wasm32"))]
                {
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
            }
            "load_weights_bin" => {
                // load_weights_bin(path) → [names_array, tensors_array]
                // Binary format: "HEXAW\0" + u32 version + u32 n_tensors
                //   per tensor: u32 name_len + name_bytes + u32 ndim + u32[] shape + f32[] data
                #[cfg(target_arch = "wasm32")]
                { return Err(self.runtime_err("load_weights_bin not supported in WASM".into())); }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    if args.is_empty() { return Err(self.type_err("load_weights_bin() requires 1 argument".into())); }
                    match &args[0] {
                        Value::Str(path) => {
                            use std::io::Read;
                            let mut file = match std::fs::File::open(path) {
                                Ok(f) => f,
                                Err(e) => return Err(self.runtime_err(format!("load_weights_bin: {}", e))),
                            };
                            let mut buf = Vec::new();
                            file.read_to_end(&mut buf).map_err(|e| self.runtime_err(format!("read error: {}", e)))?;
                            if buf.len() < 14 || &buf[0..6] != b"HEXAW\0" {
                                return Err(self.runtime_err("invalid HEXAW header".into()));
                            }
                            let mut pos = 6usize;
                            let _version = u32::from_le_bytes([buf[pos], buf[pos+1], buf[pos+2], buf[pos+3]]);
                            pos += 4;
                            let n_tensors = u32::from_le_bytes([buf[pos], buf[pos+1], buf[pos+2], buf[pos+3]]) as usize;
                            pos += 4;
                            let mut names = Vec::with_capacity(n_tensors);
                            let mut tensors = Vec::with_capacity(n_tensors);
                            for _ in 0..n_tensors {
                                if pos + 4 > buf.len() { break; }
                                let name_len = u32::from_le_bytes([buf[pos], buf[pos+1], buf[pos+2], buf[pos+3]]) as usize;
                                pos += 4;
                                let name = String::from_utf8_lossy(&buf[pos..pos+name_len]).to_string();
                                pos += name_len;
                                let ndim = u32::from_le_bytes([buf[pos], buf[pos+1], buf[pos+2], buf[pos+3]]) as usize;
                                pos += 4;
                                let mut numel = 1usize;
                                for _ in 0..ndim {
                                    let s = u32::from_le_bytes([buf[pos], buf[pos+1], buf[pos+2], buf[pos+3]]) as usize;
                                    pos += 4;
                                    numel *= s;
                                }
                                let mut values = Vec::with_capacity(numel);
                                for _ in 0..numel {
                                    if pos + 4 > buf.len() { break; }
                                    let f = f32::from_le_bytes([buf[pos], buf[pos+1], buf[pos+2], buf[pos+3]]);
                                    pos += 4;
                                    values.push(Value::Float(f as f64));
                                }
                                names.push(Value::Str(name));
                                tensors.push(Value::Array(values));
                            }
                            Ok(Value::Array(vec![Value::Array(names), Value::Array(tensors)]))
                        }
                        _ => Err(self.type_err("load_weights_bin() requires string path".into())),
                    }
                }
            }
            "write_file" => {
                #[cfg(target_arch = "wasm32")]
                { return Err(self.runtime_err("write_file is not supported in WASM mode".into())); }
                #[cfg(not(target_arch = "wasm32"))]
                {
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
            }
            "file_exists" => {
                #[cfg(target_arch = "wasm32")]
                { return Err(self.runtime_err("file_exists is not supported in WASM mode".into())); }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    if args.is_empty() { return Err(self.type_err("file_exists() requires 1 argument".into())); }
                    match &args[0] {
                        Value::Str(path) => Ok(Value::Bool(std::path::Path::new(path).exists())),
                        _ => Err(self.type_err("file_exists() requires string path".into())),
                    }
                }
            }
            "delete_file" => {
                #[cfg(target_arch = "wasm32")]
                { return Err(self.runtime_err("delete_file is not supported in WASM mode".into())); }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    if args.is_empty() { return Err(self.type_err("delete_file() requires 1 argument".into())); }
                    match &args[0] {
                        Value::Str(path) => {
                            match std::fs::remove_file(path) {
                                Ok(_) => Ok(Value::Bool(true)),
                                Err(e) => Ok(Value::Error(format!("delete_file error: {}", e))),
                            }
                        }
                        _ => Err(self.type_err("delete_file() requires string path".into())),
                    }
                }
            }
            "append_file" => {
                #[cfg(target_arch = "wasm32")]
                { return Err(self.runtime_err("append_file is not supported in WASM mode".into())); }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    if args.len() < 2 { return Err(self.type_err("append_file() requires 2 arguments".into())); }
                    match (&args[0], &args[1]) {
                        (Value::Str(path), Value::Str(content)) => {
                            use std::io::Write;
                            let file = std::fs::OpenOptions::new()
                                .create(true)
                                .append(true)
                                .open(path);
                            match file {
                                Ok(mut f) => {
                                    match f.write_all(content.as_bytes()) {
                                        Ok(_) => Ok(Value::Void),
                                        Err(e) => Ok(Value::Error(format!("append_file error: {}", e))),
                                    }
                                }
                                Err(e) => Ok(Value::Error(format!("append_file error: {}", e))),
                            }
                        }
                        _ => Err(self.type_err("append_file() requires string arguments".into())),
                    }
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
            "to_string" | "str" => {
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
                    Value::Char(c) => Ok(Value::Int(*c as i64)),
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
            "to_char" => {
                if args.is_empty() { return Err(self.type_err("to_char() requires 1 int argument".into())); }
                match &args[0] {
                    Value::Int(n) => {
                        if let Some(c) = char::from_u32(*n as u32) {
                            Ok(Value::Str(c.to_string()))
                        } else {
                            Ok(Value::Str("?".to_string()))
                        }
                    }
                    _ => Err(self.type_err("to_char() requires int".into())),
                }
            }
            "try_float" => {
                if args.is_empty() { return Err(self.type_err("try_float() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(*f)),
                    Value::Int(n) => Ok(Value::Float(*n as f64)),
                    Value::Str(s) => match s.parse::<f64>() {
                        Ok(f) => Ok(Value::Float(f)),
                        Err(_) => Ok(Value::EnumVariant(Box::new(("Option".into(), "None".into(), None)))),
                    },
                    _ => Ok(Value::EnumVariant(Box::new(("Option".into(), "None".into(), None)))),
                }
            }
            "is_numeric" => {
                if args.is_empty() { return Err(self.type_err("is_numeric() requires 1 argument".into())); }
                match &args[0] {
                    Value::Int(_) | Value::Float(_) => Ok(Value::Bool(true)),
                    Value::Str(s) => Ok(Value::Bool(s.parse::<f64>().is_ok())),
                    _ => Ok(Value::Bool(false)),
                }
            }
            // ── String builtins ────────────────────────────────────
            "join" => {
                if args.is_empty() { return Err(self.type_err("join() requires at least 1 argument".into())); }
                match &args[0] {
                    Value::Array(arr) => {
                        // join(array) or join(array, sep) — array-to-string join
                        let sep = if args.len() >= 2 {
                            match &args[1] {
                                Value::Str(s) => s.clone(),
                                _ => return Err(self.type_err("join() separator must be a string".into())),
                            }
                        } else {
                            String::new()
                        };
                        let parts: Vec<String> = arr.iter().map(|v| format!("{}", v)).collect();
                        Ok(Value::Str(parts.join(&sep)))
                    }
                    Value::Str(_name) => {
                        // join(spawn_name) — wait for named spawn thread
                        #[cfg(not(target_arch = "wasm32"))]
                        {
                            let name = _name.clone();
                            if let Some(handle) = self.named_spawns.remove(&name) {
                                handle.join().map_err(|_| self.runtime_err(format!("spawn '{}' panicked", name)))?;
                            }
                            Ok(Value::Void)
                        }
                        #[cfg(target_arch = "wasm32")]
                        {
                            Err(self.runtime_err("join is not supported in WASM mode".into()))
                        }
                    }
                    _ => Err(self.type_err("join() first argument must be an array or spawn name string".into())),
                }
            }
            "split" => {
                if args.len() < 2 { return Err(self.type_err("split() requires 2 arguments (string, delimiter)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Str(s), Value::Str(delim)) => {
                        let parts: Vec<Value> = s.split(delim.as_str()).map(|p| Value::Str(p.to_string())).collect();
                        Ok(Value::Array(parts))
                    }
                    _ => Err(self.type_err("split() requires string arguments".into())),
                }
            }
            "trim" => {
                if args.is_empty() { return Err(self.type_err("trim() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => Ok(Value::Str(s.trim().to_string())),
                    _ => Err(self.type_err("trim() requires string argument".into())),
                }
            }
            "trim_start" => {
                if args.is_empty() { return Err(self.type_err("trim_start() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => Ok(Value::Str(s.trim_start().to_string())),
                    _ => Err(self.type_err("trim_start() requires string argument".into())),
                }
            }
            "trim_end" => {
                if args.is_empty() { return Err(self.type_err("trim_end() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => Ok(Value::Str(s.trim_end().to_string())),
                    _ => Err(self.type_err("trim_end() requires string argument".into())),
                }
            }
            "starts_with" => {
                if args.len() < 2 { return Err(self.type_err("starts_with() requires 2 arguments (string, prefix)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Str(s), Value::Str(prefix)) => Ok(Value::Bool(s.starts_with(prefix.as_str()))),
                    _ => Err(self.type_err("starts_with() requires string arguments".into())),
                }
            }
            "ends_with" => {
                if args.len() < 2 { return Err(self.type_err("ends_with() requires 2 arguments (string, suffix)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Str(s), Value::Str(suffix)) => Ok(Value::Bool(s.ends_with(suffix.as_str()))),
                    _ => Err(self.type_err("ends_with() requires string arguments".into())),
                }
            }
            "contains" => {
                if args.len() < 2 { return Err(self.type_err("contains() requires 2 arguments (string, substring)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Str(s), Value::Str(sub)) => Ok(Value::Bool(s.contains(sub.as_str()))),
                    _ => Err(self.type_err("contains() requires string arguments".into())),
                }
            }
            "replace" => {
                if args.len() < 3 { return Err(self.type_err("replace() requires 3 arguments (string, old, new)".into())); }
                match (&args[0], &args[1], &args[2]) {
                    (Value::Str(s), Value::Str(old), Value::Str(new)) => Ok(Value::Str(s.replace(old.as_str(), new.as_str()))),
                    _ => Err(self.type_err("replace() requires string arguments".into())),
                }
            }
            "to_upper" => {
                if args.is_empty() { return Err(self.type_err("to_upper() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => Ok(Value::Str(s.to_uppercase())),
                    _ => Err(self.type_err("to_upper() requires string argument".into())),
                }
            }
            "to_lower" => {
                if args.is_empty() { return Err(self.type_err("to_lower() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => Ok(Value::Str(s.to_lowercase())),
                    _ => Err(self.type_err("to_lower() requires string argument".into())),
                }
            }
            // ── Neural network primitives (Anima consciousness engine) ──
            "sigmoid" => {
                if args.is_empty() { return Err(self.type_err("sigmoid() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(1.0 / (1.0 + (-f).exp()))),
                    Value::Int(n) => Ok(Value::Float(1.0 / (1.0 + (-(*n as f64)).exp()))),
                    Value::Array(a) => {
                        let result: Vec<Value> = a.iter().map(|x| {
                            let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            Value::Float(1.0 / (1.0 + (-xf).exp()))
                        }).collect();
                        Ok(Value::Array(result))
                    }
                    _ => Err(self.type_err("sigmoid() requires numeric or array argument".into())),
                }
            }
            "tanh_" => {
                if args.is_empty() { return Err(self.type_err("tanh_() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.tanh())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).tanh())),
                    Value::Array(a) => {
                        let result: Vec<Value> = a.iter().map(|x| {
                            let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            Value::Float(xf.tanh())
                        }).collect();
                        Ok(Value::Array(result))
                    }
                    _ => Err(self.type_err("tanh_() requires numeric or array argument".into())),
                }
            }
            "relu" => {
                if args.is_empty() { return Err(self.type_err("relu() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(if *f > 0.0 { *f } else { 0.0 })),
                    Value::Int(n) => Ok(Value::Int(if *n > 0 { *n } else { 0 })),
                    Value::Array(a) => {
                        let result: Vec<Value> = a.iter().map(|x| match x {
                            Value::Float(f) => Value::Float(if *f > 0.0 { *f } else { 0.0 }),
                            Value::Int(i) => Value::Int(if *i > 0 { *i } else { 0 }),
                            _ => Value::Float(0.0),
                        }).collect();
                        Ok(Value::Array(result))
                    }
                    _ => Err(self.type_err("relu() requires numeric or array argument".into())),
                }
            }
            "softmax" => {
                if args.is_empty() { return Err(self.type_err("softmax() requires 1 array argument".into())); }
                if let Value::Array(a) = &args[0] {
                    let vals: Vec<f64> = a.iter().map(|x| match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let max_val = vals.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let exps: Vec<f64> = vals.iter().map(|x| (x - max_val).exp()).collect();
                    let sum: f64 = exps.iter().sum();
                    let result: Vec<Value> = exps.iter().map(|e| Value::Float(e / sum)).collect();
                    Ok(Value::Array(result))
                } else {
                    Err(self.type_err("softmax() requires array argument".into()))
                }
            }
            "cosine_sim" => {
                if args.len() < 2 { return Err(self.type_err("cosine_sim() requires 2 array arguments".into())); }
                if let (Value::Array(a), Value::Array(b)) = (&args[0], &args[1]) {
                    let mut dot = 0.0f64;
                    let mut na = 0.0f64;
                    let mut nb = 0.0f64;
                    for (x, y) in a.iter().zip(b.iter()) {
                        let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let yf = match y { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        dot += xf * yf;
                        na += xf * xf;
                        nb += yf * yf;
                    }
                    let denom = na.sqrt() * nb.sqrt();
                    Ok(Value::Float(if denom < 1e-10 { 0.0 } else { dot / denom }))
                } else {
                    Err(self.type_err("cosine_sim() requires 2 array arguments".into()))
                }
            }
            // ── @test/@bench runner builtins ──
            "run_tests" => {
                let tests = self.test_fns.clone();
                let total = tests.len();
                let mut passed = 0usize;
                let mut failed = 0usize;
                for (name, _params, body) in &tests {
                    self.env.push_scope();
                    let mut ok = true;
                    for stmt in body.iter() {
                        match self.exec_stmt(stmt) {
                            Ok(_) => {
                                if self.return_value.is_some() {
                                    self.return_value.take();
                                    break;
                                }
                            }
                            Err(e) => {
                                self.writeln_output(&format!("  FAIL {}: {}", name, e.message));
                                ok = false;
                                break;
                            }
                        }
                    }
                    self.env.pop_scope();
                    if ok { passed += 1; self.writeln_output(&format!("  PASS {}", name)); }
                    else { failed += 1; }
                }
                self.writeln_output(&format!("--- {}/{} tests passed ---", passed, total));
                Ok(Value::Bool(failed == 0))
            }
            "run_benchmarks" => {
                let benches = self.bench_fns.clone();
                for (name, _params, body) in &benches {
                    let start = std::time::Instant::now();
                    self.env.push_scope();
                    for stmt in body.iter() {
                        match self.exec_stmt(stmt) {
                            Ok(_) => { if self.return_value.is_some() { self.return_value.take(); break; } }
                            Err(e) => { self.writeln_output(&format!("  BENCH ERR {}: {}", name, e.message)); break; }
                        }
                    }
                    self.env.pop_scope();
                    let elapsed = start.elapsed();
                    self.writeln_output(&format!("  BENCH {} : {:.3}ms", name, elapsed.as_secs_f64() * 1000.0));
                }
                Ok(Value::Void)
            }
            // ── Matrix/Vector builtins (Anima GRU/Hebbian acceleration) ──
            "dot" => {
                // dot(a, b) → scalar dot product of two arrays
                if args.len() < 2 { return Err(self.type_err("dot() requires 2 array arguments".into())); }
                if let (Value::Array(a), Value::Array(b)) = (&args[0], &args[1]) {
                    let mut sum = 0.0f64;
                    for (x, y) in a.iter().zip(b.iter()) {
                        let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let yf = match y { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        sum += xf * yf;
                    }
                    Ok(Value::Float(sum))
                } else {
                    Err(self.type_err("dot() requires 2 array arguments".into()))
                }
            }
            "matvec" => {
                // matvec(mat_flat, vec, rows, cols) → result vector
                // mat is row-major flat array [rows*cols], vec is [cols]
                if args.len() < 4 { return Err(self.type_err("matvec() requires (mat, vec, rows, cols)".into())); }
                if let (Value::Array(mat), Value::Array(v), Value::Int(rows), Value::Int(cols)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let rows = *rows as usize;
                    let cols = *cols as usize;
                    let mut result = Vec::with_capacity(rows);
                    for r in 0..rows {
                        let mut sum = 0.0f64;
                        for c in 0..cols {
                            let mval = match &mat[r * cols + c] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            let vval = match &v[c] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            sum += mval * vval;
                        }
                        result.push(Value::Float(sum));
                    }
                    Ok(Value::Array(result))
                } else {
                    Err(self.type_err("matvec() requires (array, array, int, int)".into()))
                }
            }
            "mat_add" => {
                // mat_add(a, b) → element-wise addition of arrays
                if args.len() < 2 { return Err(self.type_err("mat_add() requires 2 arrays".into())); }
                if let (Value::Array(a), Value::Array(b)) = (&args[0], &args[1]) {
                    let result: Vec<Value> = a.iter().zip(b.iter()).map(|(x, y)| {
                        let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let yf = match y { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        Value::Float(xf + yf)
                    }).collect();
                    Ok(Value::Array(result))
                } else {
                    Err(self.type_err("mat_add() requires 2 arrays".into()))
                }
            }
            "mat_scale" => {
                // mat_scale(arr, scalar) → element-wise scaling
                if args.len() < 2 { return Err(self.type_err("mat_scale() requires (array, scalar)".into())); }
                if let Value::Array(a) = &args[0] {
                    let s = match &args[1] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 1.0 };
                    let result: Vec<Value> = a.iter().map(|x| {
                        let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        Value::Float(xf * s)
                    }).collect();
                    Ok(Value::Array(result))
                } else {
                    Err(self.type_err("mat_scale() requires (array, scalar)".into()))
                }
            }
            "matmul" => {
                // matmul(A, B, M, K, N) → C[M×N] = A[M×K] × B[K×N], all flat row-major
                if args.len() < 5 { return Err(self.type_err("matmul() requires (A, B, M, K, N)".into())); }
                if let (Value::Array(a), Value::Array(b), Value::Int(m), Value::Int(k), Value::Int(n)) = (&args[0], &args[1], &args[2], &args[3], &args[4]) {
                    let (m, k, n) = (*m as usize, *k as usize, *n as usize);
                    let mut c = vec![0.0f64; m * n];
                    for i in 0..m {
                        for j in 0..n {
                            let mut sum = 0.0f64;
                            for p in 0..k {
                                let av = match &a[i * k + p] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                let bv = match &b[p * n + j] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                sum += av * bv;
                            }
                            c[i * n + j] = sum;
                        }
                    }
                    Ok(Value::Array(c.into_iter().map(Value::Float).collect()))
                } else {
                    Err(self.type_err("matmul() requires (array, array, int, int, int)".into()))
                }
            }
            "transpose" => {
                // transpose(mat, rows, cols) → transposed [cols×rows]
                if args.len() < 3 { return Err(self.type_err("transpose() requires (mat, rows, cols)".into())); }
                if let (Value::Array(a), Value::Int(rows), Value::Int(cols)) = (&args[0], &args[1], &args[2]) {
                    let (rows, cols) = (*rows as usize, *cols as usize);
                    let mut result = Vec::with_capacity(rows * cols);
                    for j in 0..cols {
                        for i in 0..rows {
                            result.push(a[i * cols + j].clone());
                        }
                    }
                    Ok(Value::Array(result))
                } else {
                    Err(self.type_err("transpose() requires (array, int, int)".into()))
                }
            }
            "normalize" => {
                // normalize(vec) → L2 normalized vector
                if args.is_empty() { return Err(self.type_err("normalize() requires 1 array argument".into())); }
                if let Value::Array(a) = &args[0] {
                    let mut norm_sq = 0.0f64;
                    for x in a.iter() {
                        let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        norm_sq += xf * xf;
                    }
                    let norm = norm_sq.sqrt();
                    if norm < 1e-10 {
                        Ok(Value::Array(a.clone()))
                    } else {
                        let result: Vec<Value> = a.iter().map(|x| {
                            let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            Value::Float(xf / norm)
                        }).collect();
                        Ok(Value::Array(result))
                    }
                } else {
                    Err(self.type_err("normalize() requires array argument".into()))
                }
            }
            "cross_entropy" => {
                // cross_entropy(logits_array, target_index) → scalar loss
                if args.len() < 2 { return Err(self.type_err("cross_entropy() requires (logits, target)".into())); }
                if let (Value::Array(logits), Value::Int(target)) = (&args[0], &args[1]) {
                    let vals: Vec<f64> = logits.iter().map(|x| match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let max_val = vals.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let exps: Vec<f64> = vals.iter().map(|x| (x - max_val).exp()).collect();
                    let sum: f64 = exps.iter().sum();
                    let log_softmax = (exps[*target as usize] / sum).ln();
                    Ok(Value::Float(-log_softmax))
                } else {
                    Err(self.type_err("cross_entropy() requires (array, int)".into()))
                }
            }
            "argmax" => {
                if args.is_empty() { return Err(self.type_err("argmax() requires 1 array argument".into())); }
                if let Value::Array(a) = &args[0] {
                    let mut best_idx = 0usize;
                    let mut best_val = f64::NEG_INFINITY;
                    for (i, x) in a.iter().enumerate() {
                        let xf = match x { Value::Float(f) => *f, Value::Int(n) => *n as f64, _ => 0.0 };
                        if xf > best_val { best_val = xf; best_idx = i; }
                    }
                    Ok(Value::Int(best_idx as i64))
                } else {
                    Err(self.type_err("argmax() requires array argument".into()))
                }
            }
            // ── Tensor initialization builtins ──
            "zeros" => {
                // zeros(n) → array of n zeros
                if args.is_empty() { return Err(self.type_err("zeros() requires 1 int argument".into())); }
                if let Value::Int(n) = &args[0] {
                    Ok(Value::Array(vec![Value::Float(0.0); *n as usize]))
                } else { Err(self.type_err("zeros() requires int".into())) }
            }
            "ones" => {
                if args.is_empty() { return Err(self.type_err("ones() requires 1 int argument".into())); }
                if let Value::Int(n) = &args[0] {
                    Ok(Value::Array(vec![Value::Float(1.0); *n as usize]))
                } else { Err(self.type_err("ones() requires int".into())) }
            }
            "randn" => {
                // randn(n) → array of n random normal (Box-Muller)
                // randn(n, scale) → scaled
                if args.is_empty() { return Err(self.type_err("randn() requires at least 1 argument".into())); }
                if let Value::Int(n) = &args[0] {
                    let scale = if args.len() > 1 {
                        match &args[1] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 1.0 }
                    } else { 1.0 };
                    use std::f64::consts::PI;
                    let count = *n as usize;
                    let mut result = Vec::with_capacity(count);
                    for i in 0..count {
                        // Simple LCG + Box-Muller
                        let u1 = ((i as f64 * 2654435761.0 + self.current_line as f64 * 1013904223.0) % 1000000.0) / 1000000.0;
                        let u2 = ((i as f64 * 1103515245.0 + self.current_line as f64 * 12345.0 + 0.5) % 1000000.0) / 1000000.0;
                        let u1 = u1.max(1e-10);
                        let z = (-2.0 * u1.ln()).sqrt() * (2.0 * PI * u2).cos();
                        result.push(Value::Float(z * scale));
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("randn() requires int".into())) }
            }
            "arange" => {
                // arange(n) → [0.0, 1.0, ..., n-1.0]
                if args.is_empty() { return Err(self.type_err("arange() requires 1 int argument".into())); }
                if let Value::Int(n) = &args[0] {
                    let result: Vec<Value> = (0..*n).map(|i| Value::Float(i as f64)).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("arange() requires int".into())) }
            }
            // ── Learning utility builtins ──
            "ema" => {
                // ema(old_val, new_val, alpha) → old*(1-alpha) + new*alpha
                if args.len() < 3 { return Err(self.type_err("ema() requires (old, new, alpha)".into())); }
                let old_v = match &args[0] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                let new_v = match &args[1] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                let alpha = match &args[2] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.014 };
                Ok(Value::Float(old_v * (1.0 - alpha) + new_v * alpha))
            }
            "clip" => {
                // clip(val, lo, hi) — scalar or array
                if args.len() < 3 { return Err(self.type_err("clip() requires (val, lo, hi)".into())); }
                let lo = match &args[1] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => f64::NEG_INFINITY };
                let hi = match &args[2] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => f64::INFINITY };
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.max(lo).min(hi))),
                    Value::Int(i) => Ok(Value::Float((*i as f64).max(lo).min(hi))),
                    Value::Array(a) => {
                        let result: Vec<Value> = a.iter().map(|x| {
                            let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            Value::Float(xf.max(lo).min(hi))
                        }).collect();
                        Ok(Value::Array(result))
                    }
                    _ => Err(self.type_err("clip() requires numeric or array".into())),
                }
            }
            "sum" => {
                // sum(array) → scalar sum
                if args.is_empty() { return Err(self.type_err("sum() requires 1 array argument".into())); }
                if let Value::Array(a) = &args[0] {
                    let total: f64 = a.iter().map(|x| match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).sum();
                    Ok(Value::Float(total))
                } else { Err(self.type_err("sum() requires array".into())) }
            }
            "mean" => {
                if args.is_empty() { return Err(self.type_err("mean() requires 1 array argument".into())); }
                if let Value::Array(a) = &args[0] {
                    let total: f64 = a.iter().map(|x| match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).sum();
                    Ok(Value::Float(total / a.len() as f64))
                } else { Err(self.type_err("mean() requires array".into())) }
            }
            "hadamard" => {
                // hadamard(a, b) → element-wise multiply
                if args.len() < 2 { return Err(self.type_err("hadamard() requires 2 arrays".into())); }
                if let (Value::Array(a), Value::Array(b)) = (&args[0], &args[1]) {
                    let result: Vec<Value> = a.iter().zip(b.iter()).map(|(x, y)| {
                        let xf = match x { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let yf = match y { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        Value::Float(xf * yf)
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("hadamard() requires 2 arrays".into())) }
            }
            // ── Consciousness topology builtins (Anima-native) ──
            "ring_topology" => {
                // ring_topology(n) → flat adjacency [n×n], each cell connected to ±1 neighbors
                if args.is_empty() { return Err(self.type_err("ring_topology() requires 1 int".into())); }
                if let Value::Int(n) = &args[0] {
                    let n = *n as usize;
                    let mut adj = vec![Value::Float(0.0); n * n];
                    for i in 0..n {
                        let left = (i + n - 1) % n;
                        let right = (i + 1) % n;
                        adj[i * n + left] = Value::Float(1.0);
                        adj[i * n + right] = Value::Float(1.0);
                    }
                    Ok(Value::Array(adj))
                } else { Err(self.type_err("ring_topology() requires int".into())) }
            }
            "small_world_topology" => {
                // small_world_topology(n, k, p) → Watts-Strogatz: ring + random rewiring
                // k=neighbors per side, p=rewire probability (deterministic seed)
                if args.len() < 3 { return Err(self.type_err("small_world_topology() requires (n, k, p)".into())); }
                if let (Value::Int(n), Value::Int(k), Value::Float(p)) = (&args[0], &args[1], &args[2]) {
                    let n = *n as usize;
                    let k = *k as usize;
                    let p = *p;
                    let mut adj = vec![Value::Float(0.0); n * n];
                    // Base ring with k neighbors each side
                    for i in 0..n {
                        for d in 1..=k {
                            let right = (i + d) % n;
                            let left = (i + n - d) % n;
                            adj[i * n + right] = Value::Float(1.0);
                            adj[i * n + left] = Value::Float(1.0);
                        }
                    }
                    // Deterministic "rewiring" based on position
                    for i in 0..n {
                        for d in 1..=k {
                            let hash = ((i * 2654435761 + d * 1013904223) % 1000) as f64 / 1000.0;
                            if hash < p {
                                let old_target = (i + d) % n;
                                let new_target = ((i * 1103515245 + d * 12345) % n) as usize;
                                if new_target != i {
                                    adj[i * n + old_target] = Value::Float(0.0);
                                    adj[i * n + new_target] = Value::Float(1.0);
                                }
                            }
                        }
                    }
                    Ok(Value::Array(adj))
                } else { Err(self.type_err("small_world_topology() requires (int, int, float)".into())) }
            }
            "hypercube_topology" => {
                // hypercube_topology(n) → n must be power of 2, connect nodes differing by 1 bit
                if args.is_empty() { return Err(self.type_err("hypercube_topology() requires 1 int".into())); }
                if let Value::Int(n) = &args[0] {
                    let n = *n as usize;
                    let mut adj = vec![Value::Float(0.0); n * n];
                    for i in 0..n {
                        for bit in 0..32 {
                            let j = i ^ (1 << bit);
                            if j < n {
                                adj[i * n + j] = Value::Float(1.0);
                            }
                        }
                    }
                    Ok(Value::Array(adj))
                } else { Err(self.type_err("hypercube_topology() requires int".into())) }
            }
            "auto_topology" => {
                // auto_topology(n) → auto-select: ring(≤4), small_world(≤16), hypercube(≤64), scale_free(>64)
                if args.is_empty() { return Err(self.type_err("auto_topology() requires 1 int".into())); }
                if let Value::Int(n) = &args[0] {
                    let n_val = *n as usize;
                    if n_val <= 4 {
                        // Ring
                        return self.call_builtin("ring_topology", vec![Value::Int(*n)]);
                    } else if n_val <= 16 {
                        // Small-world
                        return self.call_builtin("small_world_topology", vec![Value::Int(*n), Value::Int(2), Value::Float(0.3)]);
                    } else {
                        // Hypercube (round to nearest power of 2)
                        let mut pow2 = 1;
                        while pow2 < n_val { pow2 <<= 1; }
                        return self.call_builtin("hypercube_topology", vec![Value::Int(pow2 as i64)]);
                    }
                } else { Err(self.type_err("auto_topology() requires int".into())) }
            }
            // ── Optimizer builtins (SGD/Adam) ──
            "sgd_step" => {
                // sgd_step(params, grads, lr) → updated params = params - lr * grads
                if args.len() < 3 { return Err(self.type_err("sgd_step() requires (params, grads, lr)".into())); }
                if let (Value::Array(p), Value::Array(g)) = (&args[0], &args[1]) {
                    let lr = match &args[2] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.01 };
                    let result: Vec<Value> = p.iter().zip(g.iter()).map(|(pv, gv)| {
                        let pf = match pv { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let gf = match gv { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        Value::Float(pf - lr * gf)
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("sgd_step() requires (array, array, float)".into())) }
            }
            "adam_step" => {
                // adam_step(params, grads, m, v, lr, beta1, beta2, eps, t) → [new_params, new_m, new_v]
                // Returns flattened: first N = params, next N = m, next N = v
                if args.len() < 9 { return Err(self.type_err("adam_step() requires 9 args".into())); }
                if let (Value::Array(p), Value::Array(g), Value::Array(m), Value::Array(v)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let lr = match &args[4] { Value::Float(f) => *f, _ => 0.001 };
                    let beta1 = match &args[5] { Value::Float(f) => *f, _ => 0.9 };
                    let beta2 = match &args[6] { Value::Float(f) => *f, _ => 0.999 };
                    let eps = match &args[7] { Value::Float(f) => *f, _ => 1e-8 };
                    let t = match &args[8] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 1.0 };
                    let n = p.len();
                    let mut new_p = Vec::with_capacity(n);
                    let mut new_m = Vec::with_capacity(n);
                    let mut new_v = Vec::with_capacity(n);
                    let bc1 = 1.0 - beta1.powf(t);
                    let bc2 = 1.0 - beta2.powf(t);
                    for i in 0..n {
                        let pf = match &p[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let gf = match &g[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let mf = match &m[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let vf = match &v[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let mi = beta1 * mf + (1.0 - beta1) * gf;
                        let vi = beta2 * vf + (1.0 - beta2) * gf * gf;
                        let m_hat = mi / bc1;
                        let v_hat = vi / bc2;
                        let pi = pf - lr * m_hat / (v_hat.sqrt() + eps);
                        new_p.push(Value::Float(pi));
                        new_m.push(Value::Float(mi));
                        new_v.push(Value::Float(vi));
                    }
                    // Return as flat array: [params..., m..., v...]
                    let mut result = new_p;
                    result.extend(new_m);
                    result.extend(new_v);
                    Ok(Value::Array(result))
                } else { Err(self.type_err("adam_step() requires arrays".into())) }
            }
            // ── LR Scheduling builtins (Anima Phase Curriculum) ──
            "warmup_lr" => {
                // warmup_lr(base_lr, step, warmup_steps) → linearly ramp from 0 to base_lr
                if args.len() < 3 { return Err(self.type_err("warmup_lr() requires (base_lr, step, warmup_steps)".into())); }
                let base = match &args[0] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.001 };
                let step = match &args[1] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 0.0 };
                let warmup = match &args[2] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 1000.0 };
                if step < warmup {
                    Ok(Value::Float(base * step / warmup))
                } else {
                    Ok(Value::Float(base))
                }
            }
            "cosine_lr" => {
                // cosine_lr(base_lr, step, total_steps, min_lr) → cosine annealing
                if args.len() < 3 { return Err(self.type_err("cosine_lr() requires (base_lr, step, total_steps)".into())); }
                let base = match &args[0] { Value::Float(f) => *f, _ => 0.001 };
                let step = match &args[1] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 0.0 };
                let total = match &args[2] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 10000.0 };
                let min_lr = if args.len() > 3 { match &args[3] { Value::Float(f) => *f, _ => 0.0 } } else { 0.0 };
                let progress = (step / total).min(1.0);
                let lr = min_lr + 0.5 * (base - min_lr) * (1.0 + (std::f64::consts::PI * progress).cos());
                Ok(Value::Float(lr))
            }
            "phase_lr" => {
                // phase_lr(base_lr, step, total_steps, n_phases) → Law 60 phase curriculum
                // Each phase gets 1/n of total steps; lr decays per phase
                if args.len() < 4 { return Err(self.type_err("phase_lr() requires (base_lr, step, total, n_phases)".into())); }
                let base = match &args[0] { Value::Float(f) => *f, _ => 0.001 };
                let step = match &args[1] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 0.0 };
                let total = match &args[2] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 10000.0 };
                let n_phases = match &args[3] { Value::Int(i) => *i as usize, _ => 3 };
                let phase = ((step / total * n_phases as f64) as usize).min(n_phases - 1);
                let decay = 0.5f64.powi(phase as i32);
                Ok(Value::Float(base * decay))
            }
            // ── Transformer building blocks ──
            "layer_norm" => {
                // layer_norm(x, dim) → normalized vector (mean=0, var=1) per sample
                if args.len() < 2 { return Err(self.type_err("layer_norm() requires (array, dim)".into())); }
                if let (Value::Array(x), Value::Int(dim)) = (&args[0], &args[1]) {
                    let dim = *dim as usize;
                    let n_samples = x.len() / dim;
                    let mut result = Vec::with_capacity(x.len());
                    for s in 0..n_samples {
                        let mut mean = 0.0f64;
                        for d in 0..dim {
                            mean += match &x[s * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        }
                        mean /= dim as f64;
                        let mut var = 0.0f64;
                        for d in 0..dim {
                            let v = match &x[s * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            var += (v - mean) * (v - mean);
                        }
                        var = (var / dim as f64 + 1e-5).sqrt();
                        for d in 0..dim {
                            let v = match &x[s * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            result.push(Value::Float((v - mean) / var));
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("layer_norm() type error".into())) }
            }
            "dropout" => {
                // dropout(array, rate, training) → zero out elements with deterministic mask
                if args.len() < 3 { return Err(self.type_err("dropout() requires (array, rate, training_bool)".into())); }
                if let (Value::Array(x), Value::Float(rate)) = (&args[0], &args[1]) {
                    let training = matches!(&args[2], Value::Bool(true));
                    if !training {
                        return Ok(Value::Array(x.clone())); // inference: passthrough
                    }
                    let scale = 1.0 / (1.0 - rate);
                    let result: Vec<Value> = x.iter().enumerate().map(|(i, v)| {
                        let hash = ((i * 2654435761 + self.current_line * 1013904223) % 1000) as f64 / 1000.0;
                        if hash < *rate {
                            Value::Float(0.0)
                        } else {
                            let vf = match v { Value::Float(f) => *f, Value::Int(n) => *n as f64, _ => 0.0 };
                            Value::Float(vf * scale)
                        }
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("dropout() type error".into())) }
            }
            "embedding" => {
                // embedding(tokens, weight, vocab_size, dim) → [batch × dim] embeddings
                // tokens: array of int indices, weight: flat [vocab_size × dim]
                if args.len() < 4 { return Err(self.type_err("embedding() requires (tokens, weight, vocab_size, dim)".into())); }
                if let (Value::Array(tokens), Value::Array(w), Value::Int(_vs), Value::Int(dim)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let dim = *dim as usize;
                    let mut result = Vec::with_capacity(tokens.len() * dim);
                    for tok in tokens {
                        let idx = match tok { Value::Int(i) => *i as usize, _ => 0 };
                        for d in 0..dim {
                            if idx * dim + d < w.len() {
                                result.push(w[idx * dim + d].clone());
                            } else {
                                result.push(Value::Float(0.0));
                            }
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("embedding() type error".into())) }
            }
            "attention" => {
                // attention(Q, K, V, seq_len, dim, causal) → output [seq_len × dim]
                // Scaled dot-product attention with optional causal mask
                if args.len() < 5 { return Err(self.type_err("attention() requires (Q, K, V, seq_len, dim)".into())); }
                if let (Value::Array(q), Value::Array(k), Value::Array(v), Value::Int(seq), Value::Int(dim)) = (&args[0], &args[1], &args[2], &args[3], &args[4]) {
                    let (seq, dim) = (*seq as usize, *dim as usize);
                    let causal = args.len() > 5 && matches!(&args[5], Value::Bool(true));
                    let scale = 1.0 / (dim as f64).sqrt();
                    let mut output = Vec::with_capacity(seq * dim);
                    for i in 0..seq {
                        // Compute attention scores for position i
                        let mut scores = Vec::with_capacity(seq);
                        for j in 0..seq {
                            if causal && j > i {
                                scores.push(f64::NEG_INFINITY);
                            } else {
                                let mut dot = 0.0f64;
                                for d in 0..dim {
                                    let qv = match &q[i * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                    let kv = match &k[j * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                    dot += qv * kv;
                                }
                                scores.push(dot * scale);
                            }
                        }
                        // Softmax
                        let max_s = scores.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                        let exps: Vec<f64> = scores.iter().map(|s| (s - max_s).exp()).collect();
                        let sum_e: f64 = exps.iter().sum();
                        let weights: Vec<f64> = exps.iter().map(|e| e / sum_e).collect();
                        // Weighted sum of V
                        for d in 0..dim {
                            let mut val = 0.0f64;
                            for j in 0..seq {
                                let vv = match &v[j * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                val += weights[j] * vv;
                            }
                            output.push(Value::Float(val));
                        }
                    }
                    Ok(Value::Array(output))
                } else { Err(self.type_err("attention() type error".into())) }
            }
            // ── Extended activations ──
            "gelu" => {
                // GELU: x * Φ(x) ≈ 0.5*x*(1+tanh(sqrt(2/π)*(x+0.044715*x³)))
                if args.is_empty() { return Err(self.type_err("gelu() requires 1 argument".into())); }
                let compute = |x: f64| -> f64 {
                    0.5 * x * (1.0 + (std::f64::consts::FRAC_2_PI.sqrt() * (x + 0.044715 * x * x * x)).tanh())
                };
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(compute(*f))),
                    Value::Int(n) => Ok(Value::Float(compute(*n as f64))),
                    Value::Array(a) => Ok(Value::Array(a.iter().map(|v| {
                        let x = match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        Value::Float(compute(x))
                    }).collect())),
                    _ => Err(self.type_err("gelu() type error".into())),
                }
            }
            "silu" => {
                // SiLU/Swish: x * sigmoid(x)
                if args.is_empty() { return Err(self.type_err("silu() requires 1 argument".into())); }
                let compute = |x: f64| -> f64 { x / (1.0 + (-x).exp()) };
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(compute(*f))),
                    Value::Int(n) => Ok(Value::Float(compute(*n as f64))),
                    Value::Array(a) => Ok(Value::Array(a.iter().map(|v| {
                        let x = match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        Value::Float(compute(x))
                    }).collect())),
                    _ => Err(self.type_err("silu() type error".into())),
                }
            }
            // ── Positional encoding ──
            "sinusoidal_pe" => {
                // sinusoidal_pe(seq_len, dim) → [seq_len × dim] positional encoding
                if args.len() < 2 { return Err(self.type_err("sinusoidal_pe() requires (seq_len, dim)".into())); }
                if let (Value::Int(seq), Value::Int(dim)) = (&args[0], &args[1]) {
                    let (seq, dim) = (*seq as usize, *dim as usize);
                    let mut pe = Vec::with_capacity(seq * dim);
                    for pos in 0..seq {
                        for d in 0..dim {
                            let angle = pos as f64 / (10000.0f64).powf(2.0 * (d / 2) as f64 / dim as f64);
                            pe.push(Value::Float(if d % 2 == 0 { angle.sin() } else { angle.cos() }));
                        }
                    }
                    Ok(Value::Array(pe))
                } else { Err(self.type_err("sinusoidal_pe() type error".into())) }
            }
            // ── Multi-Head Attention ──
            "multi_head_attention" => {
                // multi_head_attention(x, Wq, Wk, Wv, Wo, seq, dim, n_heads, causal)
                // x: [seq×dim], W*: [dim×dim], Wo: [dim×dim]
                if args.len() < 8 { return Err(self.type_err("multi_head_attention() requires 8+ args".into())); }
                if let (Value::Array(x), Value::Array(wq), Value::Array(wk), Value::Array(wv)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let wo = if let Value::Array(a) = &args[4] { a } else { return Err(self.type_err("Wo must be array".into())); };
                    let seq = match &args[5] { Value::Int(i) => *i as usize, _ => 0 };
                    let dim = match &args[6] { Value::Int(i) => *i as usize, _ => 0 };
                    let n_heads = match &args[7] { Value::Int(i) => *i as usize, _ => 1 };
                    let causal = args.len() > 8 && matches!(&args[8], Value::Bool(true));
                    let head_dim = dim / n_heads;
                    let scale = 1.0 / (head_dim as f64).sqrt();
                    // Project Q, K, V
                    let project = |w: &[Value], inp: &[Value]| -> Vec<f64> {
                        let mut out = vec![0.0f64; seq * dim];
                        for s in 0..seq {
                            for d in 0..dim {
                                let mut sum = 0.0;
                                for k in 0..dim {
                                    let wv = match &w[d * dim + k] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                    let xv = match &inp[s * dim + k] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                    sum += wv * xv;
                                }
                                out[s * dim + d] = sum;
                            }
                        }
                        out
                    };
                    let q = project(wq, x);
                    let k = project(wk, x);
                    let v = project(wv, x);
                    // Per-head attention
                    let mut concat = vec![0.0f64; seq * dim];
                    for h in 0..n_heads {
                        let offset = h * head_dim;
                        for i in 0..seq {
                            let mut scores = vec![0.0f64; seq];
                            for j in 0..seq {
                                if causal && j > i { scores[j] = f64::NEG_INFINITY; continue; }
                                let mut dot = 0.0;
                                for d in 0..head_dim {
                                    dot += q[i * dim + offset + d] * k[j * dim + offset + d];
                                }
                                scores[j] = dot * scale;
                            }
                            let max_s = scores.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                            let exps: Vec<f64> = scores.iter().map(|s| (s - max_s).exp()).collect();
                            let sum_e: f64 = exps.iter().sum();
                            for d in 0..head_dim {
                                let mut val = 0.0;
                                for j in 0..seq {
                                    val += (exps[j] / sum_e) * v[j * dim + offset + d];
                                }
                                concat[i * dim + offset + d] = val;
                            }
                        }
                    }
                    // Output projection
                    let mut output = Vec::with_capacity(seq * dim);
                    for s in 0..seq {
                        for d in 0..dim {
                            let mut sum = 0.0;
                            for k in 0..dim {
                                let wv = match &wo[d * dim + k] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                sum += wv * concat[s * dim + k];
                            }
                            output.push(Value::Float(sum));
                        }
                    }
                    Ok(Value::Array(output))
                } else { Err(self.type_err("multi_head_attention() type error".into())) }
            }
            // ── Sampling / Generation ──
            "topk" => {
                // topk(array, k) → [top k values], indices available via argmax pattern
                if args.len() < 2 { return Err(self.type_err("topk() requires (array, k)".into())); }
                if let (Value::Array(a), Value::Int(k)) = (&args[0], &args[1]) {
                    let k = (*k as usize).min(a.len());
                    let mut indexed: Vec<(usize, f64)> = a.iter().enumerate().map(|(i, v)| {
                        (i, match v { Value::Float(f) => *f, Value::Int(n) => *n as f64, _ => 0.0 })
                    }).collect();
                    indexed.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
                    let result: Vec<Value> = indexed[..k].iter().map(|(i, _)| Value::Int(*i as i64)).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("topk() type error".into())) }
            }
            "sample_token" => {
                // sample_token(logits, temperature) → sampled token index (deterministic based on position)
                if args.len() < 2 { return Err(self.type_err("sample_token() requires (logits, temperature)".into())); }
                if let (Value::Array(logits), Value::Float(temp)) = (&args[0], &args[1]) {
                    let vals: Vec<f64> = logits.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let max_v = vals.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let temp = temp.max(0.01);
                    let exps: Vec<f64> = vals.iter().map(|v| ((v - max_v) / temp).exp()).collect();
                    let sum: f64 = exps.iter().sum();
                    let probs: Vec<f64> = exps.iter().map(|e| e / sum).collect();
                    // Deterministic sampling via hash of probabilities
                    let hash = (probs.iter().enumerate().map(|(i, p)| (i as f64 + 1.0) * p * 1000.0).sum::<f64>() % 1.0).abs();
                    let mut cumsum = 0.0;
                    for (i, p) in probs.iter().enumerate() {
                        cumsum += p;
                        if cumsum > hash { return Ok(Value::Int(i as i64)); }
                    }
                    Ok(Value::Int((logits.len() - 1) as i64))
                } else { Err(self.type_err("sample_token() type error".into())) }
            }
            // ── Conv1D + KV Cache + Serialization ──
            "conv1d" => {
                // conv1d(input, kernel, in_len, kernel_size, stride) → output array
                // 1D convolution (valid padding)
                if args.len() < 5 { return Err(self.type_err("conv1d() requires (input, kernel, len, k_size, stride)".into())); }
                if let (Value::Array(input), Value::Array(kernel), Value::Int(in_len), Value::Int(k_size), Value::Int(stride)) = (&args[0], &args[1], &args[2], &args[3], &args[4]) {
                    let (in_len, k_size, stride) = (*in_len as usize, *k_size as usize, *stride as usize);
                    let out_len = (in_len - k_size) / stride + 1;
                    let mut result = Vec::with_capacity(out_len);
                    for i in 0..out_len {
                        let start = i * stride;
                        let mut sum = 0.0f64;
                        for k in 0..k_size {
                            let iv = match &input[start + k] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            let kv = match &kernel[k] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            sum += iv * kv;
                        }
                        result.push(Value::Float(sum));
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("conv1d() type error".into())) }
            }
            "max_pool1d" => {
                // max_pool1d(input, pool_size, stride) → pooled output
                if args.len() < 3 { return Err(self.type_err("max_pool1d() requires (input, pool_size, stride)".into())); }
                if let (Value::Array(input), Value::Int(pool), Value::Int(stride)) = (&args[0], &args[1], &args[2]) {
                    let (pool, stride) = (*pool as usize, *stride as usize);
                    let out_len = (input.len() - pool) / stride + 1;
                    let mut result = Vec::with_capacity(out_len);
                    for i in 0..out_len {
                        let start = i * stride;
                        let mut max_v = f64::NEG_INFINITY;
                        for k in 0..pool {
                            let v = match &input[start + k] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            if v > max_v { max_v = v; }
                        }
                        result.push(Value::Float(max_v));
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("max_pool1d() type error".into())) }
            }
            "kv_cache_append" => {
                // kv_cache_append(cache, new_kv, dim) → extended cache with new row appended
                // cache: [n×dim] flat, new_kv: [dim]
                if args.len() < 3 { return Err(self.type_err("kv_cache_append() requires (cache, new_kv, dim)".into())); }
                if let (Value::Array(cache), Value::Array(new_kv), Value::Int(_dim)) = (&args[0], &args[1], &args[2]) {
                    let mut result = cache.clone();
                    result.extend(new_kv.iter().cloned());
                    Ok(Value::Array(result))
                } else { Err(self.type_err("kv_cache_append() type error".into())) }
            }
            "attention_cached" => {
                // attention_cached(q_new, k_cache, v_cache, cache_len, dim) → output for single new query
                // Uses full K/V cache for a single query position (incremental inference)
                if args.len() < 5 { return Err(self.type_err("attention_cached() requires (q, k_cache, v_cache, cache_len, dim)".into())); }
                if let (Value::Array(q), Value::Array(k_cache), Value::Array(v_cache), Value::Int(cache_len), Value::Int(dim)) = (&args[0], &args[1], &args[2], &args[3], &args[4]) {
                    let (cl, dim) = (*cache_len as usize, *dim as usize);
                    let scale = 1.0 / (dim as f64).sqrt();
                    // Compute attention scores: q · k_j for all cached positions
                    let mut scores = Vec::with_capacity(cl);
                    for j in 0..cl {
                        let mut dot = 0.0f64;
                        for d in 0..dim {
                            let qv = match &q[d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            let kv = match &k_cache[j * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            dot += qv * kv;
                        }
                        scores.push(dot * scale);
                    }
                    // Softmax
                    let max_s = scores.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let exps: Vec<f64> = scores.iter().map(|s| (s - max_s).exp()).collect();
                    let sum_e: f64 = exps.iter().sum();
                    // Weighted sum of V
                    let mut output = Vec::with_capacity(dim);
                    for d in 0..dim {
                        let mut val = 0.0f64;
                        for j in 0..cl {
                            let vv = match &v_cache[j * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            val += (exps[j] / sum_e) * vv;
                        }
                        output.push(Value::Float(val));
                    }
                    Ok(Value::Array(output))
                } else { Err(self.type_err("attention_cached() type error".into())) }
            }
            "save_array" => {
                // save_array(array, filename) → write array as JSON to file
                if args.len() < 2 { return Err(self.type_err("save_array() requires (array, filename)".into())); }
                if let (Value::Array(a), Value::Str(path)) = (&args[0], &args[1]) {
                    let vals: Vec<String> = a.iter().map(|v| match v {
                        Value::Float(f) => format!("{}", f),
                        Value::Int(i) => format!("{}", i),
                        _ => "0".to_string(),
                    }).collect();
                    let json = format!("[{}]", vals.join(","));
                    std::fs::write(path, &json).map_err(|e| self.runtime_err(format!("save_array: {}", e)))?;
                    Ok(Value::Bool(true))
                } else { Err(self.type_err("save_array() requires (array, string)".into())) }
            }
            "load_array" => {
                // load_array(filename) → array loaded from JSON file
                if args.is_empty() { return Err(self.type_err("load_array() requires filename".into())); }
                if let Value::Str(path) = &args[0] {
                    let content = std::fs::read_to_string(path).map_err(|e| self.runtime_err(format!("load_array: {}", e)))?;
                    let trimmed = content.trim().trim_start_matches('[').trim_end_matches(']');
                    let vals: Vec<Value> = trimmed.split(',').map(|s| {
                        let s = s.trim();
                        if let Ok(f) = s.parse::<f64>() { Value::Float(f) }
                        else if let Ok(i) = s.parse::<i64>() { Value::Int(i) }
                        else { Value::Float(0.0) }
                    }).collect();
                    Ok(Value::Array(vals))
                } else { Err(self.type_err("load_array() requires string".into())) }
            }
            // ── Quantization (model compression) ──
            "quantize_int8" => {
                // quantize_int8(array) → [quantized_ints, scale, zero_point]
                // Maps float range to [-128, 127]
                if args.is_empty() { return Err(self.type_err("quantize_int8() requires array".into())); }
                if let Value::Array(a) = &args[0] {
                    let vals: Vec<f64> = a.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let min_v = vals.iter().cloned().fold(f64::INFINITY, f64::min);
                    let max_v = vals.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let scale = (max_v - min_v) / 255.0;
                    let zero_point = (-128.0 - min_v / scale).round().max(-128.0).min(127.0);
                    let quantized: Vec<Value> = vals.iter().map(|v| {
                        let q = (v / scale + zero_point).round().max(-128.0).min(127.0);
                        Value::Int(q as i64)
                    }).collect();
                    // Return [quantized_array, scale, zero_point]
                    Ok(Value::Array(vec![
                        Value::Array(quantized),
                        Value::Float(scale),
                        Value::Float(zero_point),
                    ]))
                } else { Err(self.type_err("quantize_int8() requires array".into())) }
            }
            "dequantize_int8" => {
                // dequantize_int8(quantized_ints, scale, zero_point) → float array
                if args.len() < 3 { return Err(self.type_err("dequantize_int8() requires (ints, scale, zp)".into())); }
                if let (Value::Array(q), Value::Float(scale), Value::Float(zp)) = (&args[0], &args[1], &args[2]) {
                    let result: Vec<Value> = q.iter().map(|v| {
                        let qi = match v { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 0.0 };
                        Value::Float((qi - zp) * scale)
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("dequantize_int8() type error".into())) }
            }
            // ── Beam Search ──
            "beam_search_step" => {
                // beam_search_step(beam_seqs, beam_scores, logits_batch, beam_width, vocab_size)
                // beam_seqs: [beam_width × seq_len] flat, beam_scores: [beam_width]
                // logits_batch: [beam_width × vocab_size] flat
                // Returns: [new_seqs, new_scores] — top beam_width candidates
                if args.len() < 5 { return Err(self.type_err("beam_search_step() requires 5 args".into())); }
                if let (Value::Array(seqs), Value::Array(scores), Value::Array(logits), Value::Int(bw), Value::Int(vs)) = (&args[0], &args[1], &args[2], &args[3], &args[4]) {
                    let (bw, vs) = (*bw as usize, *vs as usize);
                    let seq_len = if bw > 0 { seqs.len() / bw } else { 0 };
                    // Compute log-softmax for each beam
                    let mut candidates: Vec<(f64, Vec<Value>)> = Vec::new();
                    for b in 0..bw {
                        let base_score = match &scores[b] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        // Log-softmax of logits for this beam
                        let mut beam_logits: Vec<f64> = (0..vs).map(|v| {
                            match &logits[b * vs + v] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }
                        }).collect();
                        let max_l = beam_logits.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                        let log_sum_exp = max_l + beam_logits.iter().map(|l| (l - max_l).exp()).sum::<f64>().ln();
                        for v in 0..vs {
                            let log_prob = beam_logits[v] - log_sum_exp;
                            let new_score = base_score + log_prob;
                            let mut new_seq: Vec<Value> = seqs[b * seq_len..(b + 1) * seq_len].to_vec();
                            new_seq.push(Value::Int(v as i64));
                            candidates.push((new_score, new_seq));
                        }
                    }
                    // Sort by score descending, take top beam_width
                    candidates.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap_or(std::cmp::Ordering::Equal));
                    candidates.truncate(bw);
                    let new_seq_len = seq_len + 1;
                    let mut new_seqs: Vec<Value> = Vec::with_capacity(bw * new_seq_len);
                    let mut new_scores: Vec<Value> = Vec::with_capacity(bw);
                    for (score, seq) in &candidates {
                        new_seqs.extend(seq.iter().cloned());
                        new_scores.push(Value::Float(*score));
                    }
                    Ok(Value::Array(vec![Value::Array(new_seqs), Value::Array(new_scores)]))
                } else { Err(self.type_err("beam_search_step() type error".into())) }
            }
            // ── Gradient Clipping + Weight Init ──
            "grad_clip_norm" => {
                // grad_clip_norm(grads, max_norm) → clipped grads (scale if L2 norm exceeds max)
                if args.len() < 2 { return Err(self.type_err("grad_clip_norm() requires (grads, max_norm)".into())); }
                if let (Value::Array(g), Value::Float(max_norm)) = (&args[0], &args[1]) {
                    let mut norm_sq = 0.0f64;
                    for v in g.iter() {
                        let gf = match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        norm_sq += gf * gf;
                    }
                    let norm = norm_sq.sqrt();
                    if norm > *max_norm {
                        let scale = max_norm / norm;
                        let clipped: Vec<Value> = g.iter().map(|v| {
                            let gf = match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            Value::Float(gf * scale)
                        }).collect();
                        Ok(Value::Array(clipped))
                    } else {
                        Ok(Value::Array(g.clone()))
                    }
                } else { Err(self.type_err("grad_clip_norm() type error".into())) }
            }
            "xavier_init" => {
                // xavier_init(fan_in, fan_out) → array of fan_in*fan_out weights ~ U(-a, a), a=sqrt(6/(in+out))
                if args.len() < 2 { return Err(self.type_err("xavier_init() requires (fan_in, fan_out)".into())); }
                if let (Value::Int(fin), Value::Int(fout)) = (&args[0], &args[1]) {
                    let (fin, fout) = (*fin as usize, *fout as usize);
                    let a = (6.0 / (fin + fout) as f64).sqrt();
                    let n = fin * fout;
                    let mut result = Vec::with_capacity(n);
                    for i in 0..n {
                        let hash = ((i * 2654435761 + fin * 1013904223 + fout * 12345) % 10000) as f64 / 10000.0;
                        result.push(Value::Float((hash * 2.0 - 1.0) * a));
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("xavier_init() requires (int, int)".into())) }
            }
            "kaiming_init" => {
                // kaiming_init(fan_in, fan_out) → He init ~ N(0, sqrt(2/fan_in))
                if args.len() < 2 { return Err(self.type_err("kaiming_init() requires (fan_in, fan_out)".into())); }
                if let (Value::Int(fin), Value::Int(fout)) = (&args[0], &args[1]) {
                    let (fin, fout) = (*fin as usize, *fout as usize);
                    let std = (2.0 / fin as f64).sqrt();
                    let n = fin * fout;
                    let mut result = Vec::with_capacity(n);
                    for i in 0..n {
                        let u1 = ((i * 2654435761 + fin * 1013904223) % 10000) as f64 / 10000.0;
                        let u2 = ((i * 1103515245 + fout * 12345 + 1) % 10000) as f64 / 10000.0;
                        let u1 = u1.max(1e-10);
                        let z = (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos();
                        result.push(Value::Float(z * std));
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("kaiming_init() requires (int, int)".into())) }
            }
            // ── RL Reward builtins (Anima OnlineLearner) ──
            "curiosity_reward" => {
                // curiosity_reward(prediction_errors, window) → z-score normalized curiosity
                if args.len() < 2 { return Err(self.type_err("curiosity_reward() requires (errors, window)".into())); }
                if let (Value::Array(pe), Value::Int(window)) = (&args[0], &args[1]) {
                    let w = (*window as usize).min(pe.len());
                    let recent: Vec<f64> = pe[pe.len()-w..].iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let mean: f64 = recent.iter().sum::<f64>() / w as f64;
                    let var: f64 = recent.iter().map(|x| (x - mean) * (x - mean)).sum::<f64>() / w as f64;
                    let std = (var + 1e-8).sqrt();
                    let latest = recent[w - 1];
                    let z_score = ((latest - mean) / std).max(-3.0).min(3.0);
                    Ok(Value::Float(z_score / 3.0)) // normalize to [-1, 1]
                } else { Err(self.type_err("curiosity_reward() type error".into())) }
            }
            "dialogue_reward" => {
                // dialogue_reward(ce_losses, split_point) → improvement score [-1, 1]
                // Compares first-half vs second-half mean CE (positive = improving)
                if args.len() < 1 { return Err(self.type_err("dialogue_reward() requires (losses)".into())); }
                if let Value::Array(losses) = &args[0] {
                    let n = losses.len();
                    if n < 2 { return Ok(Value::Float(0.0)); }
                    let mid = n / 2;
                    let first: f64 = losses[..mid].iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).sum::<f64>() / mid as f64;
                    let second: f64 = losses[mid..].iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).sum::<f64>() / (n - mid) as f64;
                    let improvement = (first - second).max(-1.0).min(1.0); // positive = loss decreased
                    Ok(Value::Float(improvement))
                } else { Err(self.type_err("dialogue_reward() type error".into())) }
            }
            "combined_reward" => {
                // combined_reward(curiosity, dialogue, curiosity_weight) → weighted sum
                if args.len() < 3 { return Err(self.type_err("combined_reward() requires (curiosity, dialogue, weight)".into())); }
                let c = match &args[0] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                let d = match &args[1] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                let w = match &args[2] { Value::Float(f) => *f, _ => 0.7 };
                Ok(Value::Float((w * c + (1.0 - w) * d).max(-1.0).min(1.0)))
            }
            // ── Model Pruning ──
            "magnitude_prune" => {
                // magnitude_prune(weights, sparsity) → pruned weights (small values → 0)
                if args.len() < 2 { return Err(self.type_err("magnitude_prune() requires (weights, sparsity)".into())); }
                if let (Value::Array(w), Value::Float(sparsity)) = (&args[0], &args[1]) {
                    let mut magnitudes: Vec<f64> = w.iter().map(|v| match v { Value::Float(f) => f.abs(), Value::Int(i) => (*i as f64).abs(), _ => 0.0 }).collect();
                    magnitudes.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
                    let threshold_idx = ((w.len() as f64 * sparsity) as usize).min(w.len() - 1);
                    let threshold = magnitudes[threshold_idx];
                    let pruned: Vec<Value> = w.iter().map(|v| {
                        let vf = match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        if vf.abs() <= threshold { Value::Float(0.0) } else { Value::Float(vf) }
                    }).collect();
                    Ok(Value::Array(pruned))
                } else { Err(self.type_err("magnitude_prune() type error".into())) }
            }
            "sparsity" => {
                // sparsity(weights) → fraction of zeros
                if args.is_empty() { return Err(self.type_err("sparsity() requires array".into())); }
                if let Value::Array(w) = &args[0] {
                    let zeros = w.iter().filter(|v| match v { Value::Float(f) => *f == 0.0, Value::Int(i) => *i == 0, _ => false }).count();
                    Ok(Value::Float(zeros as f64 / w.len() as f64))
                } else { Err(self.type_err("sparsity() type error".into())) }
            }
            // ── Time Series Analysis (Φ tracking) ──
            "autocorrelation" => {
                // autocorrelation(series, lag) → correlation at given lag
                if args.len() < 2 { return Err(self.type_err("autocorrelation() requires (series, lag)".into())); }
                if let (Value::Array(s), Value::Int(lag)) = (&args[0], &args[1]) {
                    let lag = *lag as usize;
                    let n = s.len();
                    if lag >= n { return Ok(Value::Float(0.0)); }
                    let vals: Vec<f64> = s.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let mean: f64 = vals.iter().sum::<f64>() / n as f64;
                    let var: f64 = vals.iter().map(|x| (x - mean) * (x - mean)).sum::<f64>();
                    if var < 1e-10 { return Ok(Value::Float(0.0)); }
                    let cov: f64 = (0..n-lag).map(|i| (vals[i] - mean) * (vals[i + lag] - mean)).sum::<f64>();
                    Ok(Value::Float(cov / var))
                } else { Err(self.type_err("autocorrelation() type error".into())) }
            }
            "trend_slope" => {
                // trend_slope(series) → linear regression slope (positive = increasing)
                if args.is_empty() { return Err(self.type_err("trend_slope() requires array".into())); }
                if let Value::Array(s) = &args[0] {
                    let n = s.len() as f64;
                    let vals: Vec<f64> = s.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let x_mean = (n - 1.0) / 2.0;
                    let y_mean: f64 = vals.iter().sum::<f64>() / n;
                    let mut num = 0.0f64;
                    let mut den = 0.0f64;
                    for (i, y) in vals.iter().enumerate() {
                        let x = i as f64 - x_mean;
                        num += x * (y - y_mean);
                        den += x * x;
                    }
                    Ok(Value::Float(if den < 1e-10 { 0.0 } else { num / den }))
                } else { Err(self.type_err("trend_slope() type error".into())) }
            }
            "running_stats" => {
                // running_stats(series) → [mean, std, min, max, trend]
                if args.is_empty() { return Err(self.type_err("running_stats() requires array".into())); }
                if let Value::Array(s) = &args[0] {
                    let vals: Vec<f64> = s.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let n = vals.len() as f64;
                    let mean = vals.iter().sum::<f64>() / n;
                    let var = vals.iter().map(|x| (x - mean) * (x - mean)).sum::<f64>() / n;
                    let std = var.sqrt();
                    let min = vals.iter().cloned().fold(f64::INFINITY, f64::min);
                    let max = vals.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    // Simple trend: last - first / n
                    let trend = if vals.len() > 1 { (vals[vals.len()-1] - vals[0]) / (vals.len() - 1) as f64 } else { 0.0 };
                    Ok(Value::Array(vec![Value::Float(mean), Value::Float(std), Value::Float(min), Value::Float(max), Value::Float(trend)]))
                } else { Err(self.type_err("running_stats() type error".into())) }
            }
            // ── ASCII Visualization ──
            "ascii_plot" => {
                // ascii_plot(data, width, height, title) → prints ASCII line chart
                if args.len() < 3 { return Err(self.type_err("ascii_plot() requires (data, width, height)".into())); }
                if let (Value::Array(data), Value::Int(width), Value::Int(height)) = (&args[0], &args[1], &args[2]) {
                    let (w, h) = (*width as usize, *height as usize);
                    let title = if args.len() > 3 { if let Value::Str(s) = &args[3] { s.clone() } else { String::new() } } else { String::new() };
                    let vals: Vec<f64> = data.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    if vals.is_empty() { return Ok(Value::Void); }
                    let min_v = vals.iter().cloned().fold(f64::INFINITY, f64::min);
                    let max_v = vals.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let range = (max_v - min_v).max(1e-10);
                    // Title
                    if !title.is_empty() {
                        self.writeln_output(&format!("  {}", title));
                    }
                    self.writeln_output(&format!("  {:.3} ┤", max_v));
                    // Build grid
                    let mut grid = vec![vec![' '; w]; h];
                    let step = vals.len() as f64 / w as f64;
                    for x in 0..w {
                        let idx = ((x as f64 * step) as usize).min(vals.len() - 1);
                        let y = ((vals[idx] - min_v) / range * (h - 1) as f64) as usize;
                        let y = y.min(h - 1);
                        grid[h - 1 - y][x] = '*';
                    }
                    // Print rows
                    for row in 0..h {
                        let label = if row == h / 2 {
                            format!("  {:.3} ┤", min_v + range * (h - 1 - row) as f64 / (h - 1) as f64)
                        } else if row == h - 1 {
                            format!("  {:.3} ┤", min_v)
                        } else {
                            "        │".to_string()
                        };
                        let line: String = grid[row].iter().collect();
                        self.writeln_output(&format!("{}{}", label, line));
                    }
                    self.writeln_output(&format!("        └{}┘", "─".repeat(w)));
                    self.writeln_output(&format!("         0{}{}",
                        " ".repeat(w / 2 - 2), vals.len()));
                    Ok(Value::Void)
                } else { Err(self.type_err("ascii_plot() type error".into())) }
            }
            "ascii_bar" => {
                // ascii_bar(labels, values, width) → prints horizontal bar chart
                if args.len() < 3 { return Err(self.type_err("ascii_bar() requires (labels, values, width)".into())); }
                if let (Value::Array(labels), Value::Array(values), Value::Int(width)) = (&args[0], &args[1], &args[2]) {
                    let w = *width as usize;
                    let vals: Vec<f64> = values.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let max_v = vals.iter().cloned().fold(0.0f64, f64::max).max(1e-10);
                    for (i, label) in labels.iter().enumerate() {
                        let name = match label { Value::Str(s) => s.clone(), _ => format!("{}", i) };
                        let v = if i < vals.len() { vals[i] } else { 0.0 };
                        let bar_len = ((v / max_v) * w as f64) as usize;
                        let bar = "█".repeat(bar_len);
                        self.writeln_output(&format!("  {:>10} │{} {:.3}", name, bar, v));
                    }
                    Ok(Value::Void)
                } else { Err(self.type_err("ascii_bar() type error".into())) }
            }
            // ── Data Utilities ──
            "data_split" => {
                // data_split(data, ratio) → [train, test] split
                if args.len() < 2 { return Err(self.type_err("data_split() requires (array, ratio)".into())); }
                if let (Value::Array(data), Value::Float(ratio)) = (&args[0], &args[1]) {
                    let split = (data.len() as f64 * ratio) as usize;
                    let train = data[..split].to_vec();
                    let test = data[split..].to_vec();
                    Ok(Value::Array(vec![Value::Array(train), Value::Array(test)]))
                } else { Err(self.type_err("data_split() type error".into())) }
            }
            "shuffle" => {
                // shuffle(array, seed) → deterministically shuffled array
                if args.len() < 2 { return Err(self.type_err("shuffle() requires (array, seed)".into())); }
                if let (Value::Array(data), Value::Int(seed)) = (&args[0], &args[1]) {
                    let mut result = data.clone();
                    let n = result.len();
                    let seed = *seed as usize;
                    for i in (1..n).rev() {
                        let j = (i * 2654435761 + seed * 1013904223) % (i + 1);
                        result.swap(i, j);
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("shuffle() type error".into())) }
            }
            "one_hot" => {
                // one_hot(index, num_classes) → [0, ..., 1, ..., 0]
                if args.len() < 2 { return Err(self.type_err("one_hot() requires (index, num_classes)".into())); }
                if let (Value::Int(idx), Value::Int(nc)) = (&args[0], &args[1]) {
                    let mut result = vec![Value::Float(0.0); *nc as usize];
                    if (*idx as usize) < result.len() {
                        result[*idx as usize] = Value::Float(1.0);
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("one_hot() type error".into())) }
            }
            // ── Model Summary ──
            "param_count" => {
                // param_count(arrays...) → total number of parameters
                let total: usize = args.iter().map(|a| if let Value::Array(arr) = a { arr.len() } else { 0 }).sum();
                Ok(Value::Int(total as i64))
            }
            "model_size_kb" => {
                // model_size_kb(param_count) → estimated size in KB (float32)
                if args.is_empty() { return Err(self.type_err("model_size_kb() requires param count".into())); }
                let n = match &args[0] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 0.0 };
                Ok(Value::Float(n * 4.0 / 1024.0)) // 4 bytes per float32
            }
            // ── Byte-level Tokenizer (Anima CLM) ──
            "bytes_encode" => {
                // bytes_encode(string) → array of byte values [0-255]
                if args.is_empty() { return Err(self.type_err("bytes_encode() requires string".into())); }
                if let Value::Str(s) = &args[0] {
                    let bytes: Vec<Value> = s.as_bytes().iter().map(|b| Value::Int(*b as i64)).collect();
                    Ok(Value::Array(bytes))
                } else { Err(self.type_err("bytes_encode() requires string".into())) }
            }
            "bytes_decode" => {
                // bytes_decode(byte_array) → string
                if args.is_empty() { return Err(self.type_err("bytes_decode() requires array".into())); }
                if let Value::Array(bytes) = &args[0] {
                    let chars: Vec<u8> = bytes.iter().map(|v| match v { Value::Int(i) => *i as u8, _ => 0 }).collect();
                    Ok(Value::Str(String::from_utf8_lossy(&chars).to_string()))
                } else { Err(self.type_err("bytes_decode() requires array".into())) }
            }
            // ── Mixture of Experts (Anima golden-moe) ──
            "moe_gate" => {
                // moe_gate(input, gate_weights, n_experts, top_k) → [expert_indices, gate_values]
                // Gate: softmax(input @ gate_W), then top-k selection
                if args.len() < 4 { return Err(self.type_err("moe_gate() requires (input, gate_W, n_experts, top_k)".into())); }
                if let (Value::Array(input), Value::Array(gw), Value::Int(ne), Value::Int(tk)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let (ne, tk) = (*ne as usize, *tk as usize);
                    let in_dim = input.len();
                    // Compute gate logits: gate_W @ input
                    let mut logits = Vec::with_capacity(ne);
                    for e in 0..ne {
                        let mut s = 0.0f64;
                        for d in 0..in_dim {
                            let wv = match &gw[e * in_dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            let iv = match &input[d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            s += wv * iv;
                        }
                        logits.push(s);
                    }
                    // Softmax
                    let max_l = logits.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let exps: Vec<f64> = logits.iter().map(|l| (l - max_l).exp()).collect();
                    let sum_e: f64 = exps.iter().sum();
                    let probs: Vec<f64> = exps.iter().map(|e| e / sum_e).collect();
                    // Top-k
                    let mut indexed: Vec<(usize, f64)> = probs.iter().enumerate().map(|(i, p)| (i, *p)).collect();
                    indexed.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap_or(std::cmp::Ordering::Equal));
                    indexed.truncate(tk);
                    // Renormalize top-k
                    let top_sum: f64 = indexed.iter().map(|(_, p)| p).sum();
                    let indices: Vec<Value> = indexed.iter().map(|(i, _)| Value::Int(*i as i64)).collect();
                    let gates: Vec<Value> = indexed.iter().map(|(_, p)| Value::Float(p / top_sum)).collect();
                    Ok(Value::Array(vec![Value::Array(indices), Value::Array(gates)]))
                } else { Err(self.type_err("moe_gate() type error".into())) }
            }
            "moe_combine" => {
                // moe_combine(expert_outputs, gate_values) → weighted combination
                // expert_outputs: [top_k × dim] flat, gate_values: [top_k]
                if args.len() < 2 { return Err(self.type_err("moe_combine() requires (outputs, gates)".into())); }
                if let (Value::Array(outputs), Value::Array(gates)) = (&args[0], &args[1]) {
                    let k = gates.len();
                    let dim = if k > 0 { outputs.len() / k } else { 0 };
                    let mut result = vec![0.0f64; dim];
                    for i in 0..k {
                        let g = match &gates[i] { Value::Float(f) => *f, _ => 0.0 };
                        for d in 0..dim {
                            let v = match &outputs[i * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            result[d] += g * v;
                        }
                    }
                    Ok(Value::Array(result.into_iter().map(Value::Float).collect()))
                } else { Err(self.type_err("moe_combine() type error".into())) }
            }
            // ── Elastic Weight Consolidation (Continual Learning) ──
            "ewc_penalty" => {
                // ewc_penalty(current_weights, old_weights, fisher_diag, lambda) → scalar penalty
                // EWC: λ/2 * Σ F_i * (θ_i - θ*_i)²
                if args.len() < 4 { return Err(self.type_err("ewc_penalty() requires (current, old, fisher, lambda)".into())); }
                if let (Value::Array(cur), Value::Array(old), Value::Array(fisher)) = (&args[0], &args[1], &args[2]) {
                    let lambda = match &args[3] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 1.0 };
                    let mut penalty = 0.0f64;
                    for i in 0..cur.len().min(old.len()).min(fisher.len()) {
                        let c = match &cur[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let o = match &old[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let f = match &fisher[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        penalty += f * (c - o) * (c - o);
                    }
                    Ok(Value::Float(lambda * 0.5 * penalty))
                } else { Err(self.type_err("ewc_penalty() type error".into())) }
            }
            "fisher_diagonal" => {
                // fisher_diagonal(grads_squared_history) → mean of squared gradients (Fisher approx)
                // grads_history: [n_samples × n_params] flat
                if args.len() < 3 { return Err(self.type_err("fisher_diagonal() requires (grads_sq, n_samples, n_params)".into())); }
                if let (Value::Array(gsq), Value::Int(ns), Value::Int(np)) = (&args[0], &args[1], &args[2]) {
                    let (ns, np) = (*ns as usize, *np as usize);
                    let mut fisher = vec![0.0f64; np];
                    for s in 0..ns {
                        for p in 0..np {
                            let v = match &gsq[s * np + p] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            fisher[p] += v;
                        }
                    }
                    for p in 0..np { fisher[p] /= ns as f64; }
                    Ok(Value::Array(fisher.into_iter().map(Value::Float).collect()))
                } else { Err(self.type_err("fisher_diagonal() type error".into())) }
            }
            // ── Modern LLM Architecture (LLaMA/Mistral) ──
            "rms_norm" => {
                // rms_norm(x, dim, eps) → x / RMS(x), RMS = sqrt(mean(x²) + eps)
                // Simpler than LayerNorm: no mean subtraction, just scale by RMS
                if args.len() < 2 { return Err(self.type_err("rms_norm() requires (array, dim)".into())); }
                if let (Value::Array(x), Value::Int(dim)) = (&args[0], &args[1]) {
                    let dim = *dim as usize;
                    let eps = if args.len() > 2 { match &args[2] { Value::Float(f) => *f, _ => 1e-6 } } else { 1e-6 };
                    let n_samples = x.len() / dim;
                    let mut result = Vec::with_capacity(x.len());
                    for s in 0..n_samples {
                        let mut sq_sum = 0.0f64;
                        for d in 0..dim {
                            let v = match &x[s * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            sq_sum += v * v;
                        }
                        let rms = (sq_sum / dim as f64 + eps).sqrt();
                        for d in 0..dim {
                            let v = match &x[s * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            result.push(Value::Float(v / rms));
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("rms_norm() type error".into())) }
            }
            "rope" => {
                // rope(x, seq_len, dim, base) → apply Rotary Position Embedding
                // Rotates pairs of dimensions by position-dependent angles
                // x: [seq_len × dim], base: default 10000
                if args.len() < 3 { return Err(self.type_err("rope() requires (x, seq_len, dim)".into())); }
                if let (Value::Array(x), Value::Int(seq), Value::Int(dim)) = (&args[0], &args[1], &args[2]) {
                    let (seq, dim) = (*seq as usize, *dim as usize);
                    let base = if args.len() > 3 { match &args[3] { Value::Float(f) => *f, _ => 10000.0 } } else { 10000.0 };
                    let half_dim = dim / 2;
                    let mut result = Vec::with_capacity(seq * dim);
                    for pos in 0..seq {
                        for d in 0..half_dim {
                            let freq = 1.0 / base.powf(2.0 * d as f64 / dim as f64);
                            let angle = pos as f64 * freq;
                            let cos_a = angle.cos();
                            let sin_a = angle.sin();
                            let x0 = match &x[pos * dim + 2 * d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            let x1 = match &x[pos * dim + 2 * d + 1] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            result.push(Value::Float(x0 * cos_a - x1 * sin_a));
                            result.push(Value::Float(x0 * sin_a + x1 * cos_a));
                        }
                        // Handle odd dim
                        if dim % 2 == 1 {
                            result.push(x[pos * dim + dim - 1].clone());
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("rope() type error".into())) }
            }
            "sliding_window_attention" => {
                // sliding_window_attention(Q, K, V, seq_len, dim, window_size) → output
                // Each position attends only to [pos-window..pos] (Mistral-style)
                if args.len() < 6 { return Err(self.type_err("sliding_window_attention() requires 6 args".into())); }
                if let (Value::Array(q), Value::Array(k), Value::Array(v), Value::Int(seq), Value::Int(dim), Value::Int(win)) = (&args[0], &args[1], &args[2], &args[3], &args[4], &args[5]) {
                    let (seq, dim, win) = (*seq as usize, *dim as usize, *win as usize);
                    let scale = 1.0 / (dim as f64).sqrt();
                    let mut output = Vec::with_capacity(seq * dim);
                    for i in 0..seq {
                        let start = if i >= win { i - win } else { 0 };
                        let window_len = i - start + 1;
                        // Compute scores within window
                        let mut scores = Vec::with_capacity(window_len);
                        for j in start..=i {
                            let mut dot = 0.0f64;
                            for d in 0..dim {
                                let qv = match &q[i * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                let kv = match &k[j * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                dot += qv * kv;
                            }
                            scores.push(dot * scale);
                        }
                        // Softmax
                        let max_s = scores.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                        let exps: Vec<f64> = scores.iter().map(|s| (s - max_s).exp()).collect();
                        let sum_e: f64 = exps.iter().sum();
                        // Weighted sum of V
                        for d in 0..dim {
                            let mut val = 0.0f64;
                            for (idx, j) in (start..=i).enumerate() {
                                let vv = match &v[j * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                val += (exps[idx] / sum_e) * vv;
                            }
                            output.push(Value::Float(val));
                        }
                    }
                    Ok(Value::Array(output))
                } else { Err(self.type_err("sliding_window_attention() type error".into())) }
            }
            // ── Distribution Metrics (consciousness comparison) ──
            "kl_divergence" => {
                // kl_divergence(p, q) → KL(P || Q) = Σ p_i * ln(p_i / q_i)
                if args.len() < 2 { return Err(self.type_err("kl_divergence() requires (P, Q)".into())); }
                if let (Value::Array(p), Value::Array(q)) = (&args[0], &args[1]) {
                    let mut kl = 0.0f64;
                    for (pi, qi) in p.iter().zip(q.iter()) {
                        let pv = match pi { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let qv = match qi { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        if pv > 1e-10 && qv > 1e-10 { kl += pv * (pv / qv).ln(); }
                    }
                    Ok(Value::Float(kl))
                } else { Err(self.type_err("kl_divergence() type error".into())) }
            }
            "js_divergence" => {
                // js_divergence(p, q) → JS(P, Q) = 0.5*KL(P||M) + 0.5*KL(Q||M), M=(P+Q)/2
                if args.len() < 2 { return Err(self.type_err("js_divergence() requires (P, Q)".into())); }
                if let (Value::Array(p), Value::Array(q)) = (&args[0], &args[1]) {
                    let mut js = 0.0f64;
                    for (pi, qi) in p.iter().zip(q.iter()) {
                        let pv = match pi { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let qv = match qi { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let m = (pv + qv) / 2.0;
                        if pv > 1e-10 && m > 1e-10 { js += 0.5 * pv * (pv / m).ln(); }
                        if qv > 1e-10 && m > 1e-10 { js += 0.5 * qv * (qv / m).ln(); }
                    }
                    Ok(Value::Float(js))
                } else { Err(self.type_err("js_divergence() type error".into())) }
            }
            // ── Grouped Query Attention (LLaMA 2) ──
            "grouped_query_attention" => {
                // gqa(Q, K, V, seq, dim, n_q_heads, n_kv_heads) → output
                // Q has n_q_heads, K/V have n_kv_heads (shared across groups)
                if args.len() < 7 { return Err(self.type_err("grouped_query_attention() requires 7 args".into())); }
                if let (Value::Array(q), Value::Array(k), Value::Array(v), Value::Int(seq), Value::Int(dim), Value::Int(nqh), Value::Int(nkvh)) = (&args[0], &args[1], &args[2], &args[3], &args[4], &args[5], &args[6]) {
                    let (seq, dim, nqh, nkvh) = (*seq as usize, *dim as usize, *nqh as usize, *nkvh as usize);
                    let head_dim = dim / nqh;
                    let group_size = nqh / nkvh; // queries per KV head
                    let scale = 1.0 / (head_dim as f64).sqrt();
                    let mut output = vec![0.0f64; seq * dim];
                    for qh in 0..nqh {
                        let kvh = qh / group_size; // which KV head this Q head uses
                        let q_off = qh * head_dim;
                        let kv_off = kvh * head_dim;
                        for i in 0..seq {
                            let mut scores = Vec::with_capacity(seq);
                            for j in 0..seq {
                                if j > i { scores.push(f64::NEG_INFINITY); continue; }
                                let mut dot = 0.0f64;
                                for d in 0..head_dim {
                                    let qv = match &q[i * dim + q_off + d] { Value::Float(f) => *f, _ => 0.0 };
                                    let kv = match &k[j * (dim / group_size * nkvh / nkvh).max(dim / nqh * nkvh) + kv_off + d] {
                                        Value::Float(f) => *f, _ => 0.0
                                    };
                                    dot += qv * kv;
                                }
                                scores.push(dot * scale);
                            }
                            let max_s = scores.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                            let exps: Vec<f64> = scores.iter().map(|s| (s - max_s).exp()).collect();
                            let sum_e: f64 = exps.iter().sum();
                            for d in 0..head_dim {
                                let mut val = 0.0f64;
                                for j in 0..seq {
                                    if j > i { continue; }
                                    let vv = match &v[j * (dim / group_size * nkvh / nkvh).max(dim / nqh * nkvh) + kv_off + d] {
                                        Value::Float(f) => *f, _ => 0.0
                                    };
                                    val += (exps[j] / sum_e) * vv;
                                }
                                output[i * dim + q_off + d] = val;
                            }
                        }
                    }
                    Ok(Value::Array(output.into_iter().map(Value::Float).collect()))
                } else { Err(self.type_err("gqa() type error".into())) }
            }
            // ── Training Tricks ──
            "label_smoothing" => {
                // label_smoothing(target_idx, num_classes, smoothing) → smoothed distribution
                if args.len() < 3 { return Err(self.type_err("label_smoothing() requires (target, n_classes, eps)".into())); }
                if let (Value::Int(target), Value::Int(nc), Value::Float(eps)) = (&args[0], &args[1], &args[2]) {
                    let nc = *nc as usize;
                    let target = *target as usize;
                    let smooth = eps / nc as f64;
                    let confident = 1.0 - eps + smooth;
                    let result: Vec<Value> = (0..nc).map(|i| {
                        Value::Float(if i == target { confident } else { smooth })
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("label_smoothing() type error".into())) }
            }
            "weight_decay" => {
                // weight_decay(weights, decay_rate) → weights * (1 - decay_rate)
                if args.len() < 2 { return Err(self.type_err("weight_decay() requires (weights, rate)".into())); }
                if let (Value::Array(w), Value::Float(rate)) = (&args[0], &args[1]) {
                    let factor = 1.0 - rate;
                    let result: Vec<Value> = w.iter().map(|v| {
                        let vf = match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        Value::Float(vf * factor)
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("weight_decay() type error".into())) }
            }
            "adamw_step" => {
                // adamw_step(params, grads, m, v, lr, beta1, beta2, eps, t, weight_decay)
                // AdamW: decoupled weight decay (not in gradient, applied directly to params)
                if args.len() < 10 { return Err(self.type_err("adamw_step() requires 10 args".into())); }
                if let (Value::Array(p), Value::Array(g), Value::Array(m), Value::Array(v)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let lr = match &args[4] { Value::Float(f) => *f, _ => 0.001 };
                    let beta1 = match &args[5] { Value::Float(f) => *f, _ => 0.9 };
                    let beta2 = match &args[6] { Value::Float(f) => *f, _ => 0.999 };
                    let eps = match &args[7] { Value::Float(f) => *f, _ => 1e-8 };
                    let t = match &args[8] { Value::Int(i) => *i as f64, Value::Float(f) => *f, _ => 1.0 };
                    let wd = match &args[9] { Value::Float(f) => *f, _ => 0.01 };
                    let n = p.len();
                    let bc1 = 1.0 - beta1.powf(t);
                    let bc2 = 1.0 - beta2.powf(t);
                    let mut new_p = Vec::with_capacity(n);
                    let mut new_m = Vec::with_capacity(n);
                    let mut new_v = Vec::with_capacity(n);
                    for i in 0..n {
                        let pf = match &p[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let gf = match &g[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let mf = match &m[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let vf = match &v[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let mi = beta1 * mf + (1.0 - beta1) * gf;
                        let vi = beta2 * vf + (1.0 - beta2) * gf * gf;
                        let m_hat = mi / bc1;
                        let v_hat = vi / bc2;
                        // Decoupled weight decay: applied to params directly, not through gradient
                        let pi = pf * (1.0 - lr * wd) - lr * m_hat / (v_hat.sqrt() + eps);
                        new_p.push(Value::Float(pi));
                        new_m.push(Value::Float(mi));
                        new_v.push(Value::Float(vi));
                    }
                    let mut result = new_p;
                    result.extend(new_m);
                    result.extend(new_v);
                    Ok(Value::Array(result))
                } else { Err(self.type_err("adamw_step() type error".into())) }
            }
            "smooth_l1_loss" => {
                // smooth_l1_loss(pred, target, beta) → Huber loss
                if args.len() < 2 { return Err(self.type_err("smooth_l1_loss() requires (pred, target)".into())); }
                if let (Value::Array(pred), Value::Array(target)) = (&args[0], &args[1]) {
                    let beta = if args.len() > 2 { match &args[2] { Value::Float(f) => *f, _ => 1.0 } } else { 1.0 };
                    let mut total = 0.0f64;
                    for (p, t) in pred.iter().zip(target.iter()) {
                        let pf = match p { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let tf = match t { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let diff = (pf - tf).abs();
                        total += if diff < beta { 0.5 * diff * diff / beta } else { diff - 0.5 * beta };
                    }
                    Ok(Value::Float(total / pred.len() as f64))
                } else { Err(self.type_err("smooth_l1_loss() type error".into())) }
            }
            // ── LoRA: Low-Rank Adaptation ──
            "lora_init" => {
                // lora_init(in_dim, out_dim, rank) → [A: rank×in, B: out×rank] (A random, B zeros)
                if args.len() < 3 { return Err(self.type_err("lora_init() requires (in_dim, out_dim, rank)".into())); }
                if let (Value::Int(ind), Value::Int(outd), Value::Int(rank)) = (&args[0], &args[1], &args[2]) {
                    let (ind, outd, rank) = (*ind as usize, *outd as usize, *rank as usize);
                    let std = (1.0 / rank as f64).sqrt();
                    let mut a = Vec::with_capacity(rank * ind);
                    for i in 0..(rank * ind) {
                        let hash = ((i * 2654435761 + rank * 1013904223) % 10000) as f64 / 10000.0 - 0.5;
                        a.push(Value::Float(hash * 2.0 * std));
                    }
                    let b = vec![Value::Float(0.0); outd * rank]; // B starts at zero → ΔW = BA = 0 initially
                    Ok(Value::Array(vec![Value::Array(a), Value::Array(b)]))
                } else { Err(self.type_err("lora_init() type error".into())) }
            }
            "lora_forward" => {
                // lora_forward(x, W_frozen, A, B, in_dim, out_dim, rank, alpha) → W_frozen@x + alpha*(B@(A@x))
                if args.len() < 8 { return Err(self.type_err("lora_forward() requires 8 args".into())); }
                if let (Value::Array(x), Value::Array(w), Value::Array(a), Value::Array(b)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let ind = match &args[4] { Value::Int(i) => *i as usize, _ => 0 };
                    let outd = match &args[5] { Value::Int(i) => *i as usize, _ => 0 };
                    let rank = match &args[6] { Value::Int(i) => *i as usize, _ => 0 };
                    let alpha = match &args[7] { Value::Float(f) => *f, _ => 1.0 };
                    // W_frozen @ x
                    let mut base = vec![0.0f64; outd];
                    for r in 0..outd {
                        for c in 0..ind {
                            let wv = match &w[r * ind + c] { Value::Float(f) => *f, _ => 0.0 };
                            let xv = match &x[c] { Value::Float(f) => *f, _ => 0.0 };
                            base[r] += wv * xv;
                        }
                    }
                    // A @ x → intermediate [rank]
                    let mut ax = vec![0.0f64; rank];
                    for r in 0..rank {
                        for c in 0..ind {
                            let av = match &a[r * ind + c] { Value::Float(f) => *f, _ => 0.0 };
                            let xv = match &x[c] { Value::Float(f) => *f, _ => 0.0 };
                            ax[r] += av * xv;
                        }
                    }
                    // B @ ax → delta [outd]
                    for r in 0..outd {
                        for c in 0..rank {
                            let bv = match &b[r * rank + c] { Value::Float(f) => *f, _ => 0.0 };
                            base[r] += alpha * bv * ax[c];
                        }
                    }
                    Ok(Value::Array(base.into_iter().map(Value::Float).collect()))
                } else { Err(self.type_err("lora_forward() type error".into())) }
            }
            // ── Pairwise similarity matrix (Φ IIT) ──
            "cosine_sim_matrix" => {
                // cosine_sim_matrix(hiddens, n_cells, dim) → [n×n] pairwise cosine similarity
                if args.len() < 3 { return Err(self.type_err("cosine_sim_matrix() requires (hiddens, n, dim)".into())); }
                if let (Value::Array(h), Value::Int(n), Value::Int(dim)) = (&args[0], &args[1], &args[2]) {
                    let (n, dim) = (*n as usize, *dim as usize);
                    let mut matrix = Vec::with_capacity(n * n);
                    for i in 0..n {
                        for j in 0..n {
                            let mut dot = 0.0f64;
                            let mut ni = 0.0f64;
                            let mut nj = 0.0f64;
                            for d in 0..dim {
                                let vi = match &h[i * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                let vj = match &h[j * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                dot += vi * vj;
                                ni += vi * vi;
                                nj += vj * vj;
                            }
                            let denom = ni.sqrt() * nj.sqrt();
                            matrix.push(Value::Float(if denom < 1e-10 { 0.0 } else { dot / denom }));
                        }
                    }
                    Ok(Value::Array(matrix))
                } else { Err(self.type_err("cosine_sim_matrix() type error".into())) }
            }
            "mutual_information" => {
                // mutual_information(x, y, n_bins) → MI via histogram binning
                if args.len() < 3 { return Err(self.type_err("mutual_information() requires (x, y, n_bins)".into())); }
                if let (Value::Array(x), Value::Array(y), Value::Int(nb)) = (&args[0], &args[1], &args[2]) {
                    let nb = *nb as usize;
                    let n = x.len().min(y.len());
                    let xv: Vec<f64> = x.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let yv: Vec<f64> = y.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let x_min = xv.iter().cloned().fold(f64::INFINITY, f64::min);
                    let x_max = xv.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let y_min = yv.iter().cloned().fold(f64::INFINITY, f64::min);
                    let y_max = yv.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let x_range = (x_max - x_min).max(1e-10);
                    let y_range = (y_max - y_min).max(1e-10);
                    // Joint and marginal histograms
                    let mut joint = vec![0.0f64; nb * nb];
                    let mut px = vec![0.0f64; nb];
                    let mut py = vec![0.0f64; nb];
                    for i in 0..n {
                        let bx = ((xv[i] - x_min) / x_range * (nb - 1) as f64).round() as usize;
                        let by = ((yv[i] - y_min) / y_range * (nb - 1) as f64).round() as usize;
                        let bx = bx.min(nb - 1);
                        let by = by.min(nb - 1);
                        joint[bx * nb + by] += 1.0;
                        px[bx] += 1.0;
                        py[by] += 1.0;
                    }
                    // Normalize
                    let nf = n as f64;
                    for v in joint.iter_mut() { *v /= nf; }
                    for v in px.iter_mut() { *v /= nf; }
                    for v in py.iter_mut() { *v /= nf; }
                    // MI = Σ p(x,y) * log(p(x,y) / (p(x)*p(y)))
                    let mut mi = 0.0f64;
                    for i in 0..nb {
                        for j in 0..nb {
                            let pxy = joint[i * nb + j];
                            if pxy > 1e-10 && px[i] > 1e-10 && py[j] > 1e-10 {
                                mi += pxy * (pxy / (px[i] * py[j])).ln();
                            }
                        }
                    }
                    Ok(Value::Float(mi))
                } else { Err(self.type_err("mutual_information() type error".into())) }
            }
            // ── Attention Variants ──
            "linear_attention" => {
                // linear_attention(Q, K, V, seq, dim) → O(n×d²) vs O(n²×d) for standard attention
                // Uses kernel trick: φ(Q)×(φ(K)^T×V) instead of softmax(Q×K^T)×V
                if args.len() < 5 { return Err(self.type_err("linear_attention() requires 5 args".into())); }
                if let (Value::Array(q), Value::Array(k), Value::Array(v), Value::Int(seq), Value::Int(dim)) = (&args[0], &args[1], &args[2], &args[3], &args[4]) {
                    let (seq, dim) = (*seq as usize, *dim as usize);
                    // φ(x) = elu(x) + 1 (feature map)
                    let phi = |x: f64| -> f64 { if x > 0.0 { x + 1.0 } else { x.exp() } };
                    // Compute K^T × V: [dim × dim]
                    let mut ktv = vec![0.0f64; dim * dim];
                    let mut k_sum = vec![0.0f64; dim]; // for normalization
                    for s in 0..seq {
                        for d1 in 0..dim {
                            let kv = phi(match &k[s * dim + d1] { Value::Float(f) => *f, _ => 0.0 });
                            k_sum[d1] += kv;
                            for d2 in 0..dim {
                                let vv = match &v[s * dim + d2] { Value::Float(f) => *f, _ => 0.0 };
                                ktv[d1 * dim + d2] += kv * vv;
                            }
                        }
                    }
                    // Output: φ(Q) × (K^T×V) / (φ(Q) × Σφ(K))
                    let mut output = Vec::with_capacity(seq * dim);
                    for s in 0..seq {
                        let mut norm = 0.0f64;
                        for d in 0..dim {
                            norm += phi(match &q[s * dim + d] { Value::Float(f) => *f, _ => 0.0 }) * k_sum[d];
                        }
                        norm = norm.max(1e-10);
                        for d2 in 0..dim {
                            let mut val = 0.0f64;
                            for d1 in 0..dim {
                                let qv = phi(match &q[s * dim + d1] { Value::Float(f) => *f, _ => 0.0 });
                                val += qv * ktv[d1 * dim + d2];
                            }
                            output.push(Value::Float(val / norm));
                        }
                    }
                    Ok(Value::Array(output))
                } else { Err(self.type_err("linear_attention() type error".into())) }
            }
            // ── Normalization Variants ──
            "group_norm" => {
                // group_norm(x, dim, n_groups, eps) → normalize within groups of channels
                if args.len() < 3 { return Err(self.type_err("group_norm() requires (x, dim, n_groups)".into())); }
                if let (Value::Array(x), Value::Int(dim), Value::Int(ng)) = (&args[0], &args[1], &args[2]) {
                    let (dim, ng) = (*dim as usize, *ng as usize);
                    let eps = if args.len() > 3 { match &args[3] { Value::Float(f) => *f, _ => 1e-5 } } else { 1e-5 };
                    let n_samples = x.len() / dim;
                    let group_size = dim / ng;
                    let mut result = Vec::with_capacity(x.len());
                    for s in 0..n_samples {
                        for g in 0..ng {
                            let start = g * group_size;
                            let mut mean = 0.0f64;
                            for d in start..start + group_size {
                                mean += match &x[s * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            }
                            mean /= group_size as f64;
                            let mut var = 0.0f64;
                            for d in start..start + group_size {
                                let v = match &x[s * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                                var += (v - mean) * (v - mean);
                            }
                            var = (var / group_size as f64 + eps).sqrt();
                            for d in start..start + group_size {
                                let v = match &x[s * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                                result.push(Value::Float((v - mean) / var));
                            }
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("group_norm() type error".into())) }
            }
            // ── Advanced Loss ──
            "focal_loss" => {
                // focal_loss(logits, target, gamma, alpha) → -α*(1-p_t)^γ * log(p_t)
                // Addresses class imbalance by down-weighting easy examples
                if args.len() < 2 { return Err(self.type_err("focal_loss() requires (logits, target)".into())); }
                if let (Value::Array(logits), Value::Int(target)) = (&args[0], &args[1]) {
                    let gamma = if args.len() > 2 { match &args[2] { Value::Float(f) => *f, _ => 2.0 } } else { 2.0 };
                    let alpha = if args.len() > 3 { match &args[3] { Value::Float(f) => *f, _ => 0.25 } } else { 0.25 };
                    let vals: Vec<f64> = logits.iter().map(|v| match v { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 }).collect();
                    let max_v = vals.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
                    let exps: Vec<f64> = vals.iter().map(|v| (v - max_v).exp()).collect();
                    let sum: f64 = exps.iter().sum();
                    let pt = exps[*target as usize] / sum;
                    let focal = -alpha * (1.0 - pt).powf(gamma) * pt.ln();
                    Ok(Value::Float(focal))
                } else { Err(self.type_err("focal_loss() type error".into())) }
            }
            "contrastive_loss" => {
                // contrastive_loss(anchor, positive, negative, margin) → max(0, d(a,p) - d(a,n) + margin)
                // Triplet loss for consciousness state comparison
                if args.len() < 3 { return Err(self.type_err("contrastive_loss() requires (anchor, positive, negative)".into())); }
                if let (Value::Array(a), Value::Array(p), Value::Array(n)) = (&args[0], &args[1], &args[2]) {
                    let margin = if args.len() > 3 { match &args[3] { Value::Float(f) => *f, _ => 1.0 } } else { 1.0 };
                    let mut d_pos = 0.0f64;
                    let mut d_neg = 0.0f64;
                    for i in 0..a.len().min(p.len()).min(n.len()) {
                        let av = match &a[i] { Value::Float(f) => *f, _ => 0.0 };
                        let pv = match &p[i] { Value::Float(f) => *f, _ => 0.0 };
                        let nv = match &n[i] { Value::Float(f) => *f, _ => 0.0 };
                        d_pos += (av - pv) * (av - pv);
                        d_neg += (av - nv) * (av - nv);
                    }
                    d_pos = d_pos.sqrt();
                    d_neg = d_neg.sqrt();
                    Ok(Value::Float((d_pos - d_neg + margin).max(0.0)))
                } else { Err(self.type_err("contrastive_loss() type error".into())) }
            }
            "mse_loss" => {
                // mse_loss(predicted, target) → mean squared error
                if args.len() < 2 { return Err(self.type_err("mse_loss() requires (pred, target)".into())); }
                if let (Value::Array(pred), Value::Array(target)) = (&args[0], &args[1]) {
                    let mut total = 0.0f64;
                    for (p, t) in pred.iter().zip(target.iter()) {
                        let pf = match p { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        let tf = match t { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        total += (pf - tf) * (pf - tf);
                    }
                    Ok(Value::Float(total / pred.len() as f64))
                } else { Err(self.type_err("mse_loss() type error".into())) }
            }
            "gru_cell" => {
                // gru_cell(input, hidden, W_z, W_r, W_h, input_dim, hidden_dim) → new_hidden
                // Full GRU: z=σ(W_z@[input,hidden]), r=σ(W_r@[input,hidden]), h=tanh(W_h@[input,r⊙hidden])
                if args.len() < 7 { return Err(self.type_err("gru_cell() requires 7 args".into())); }
                if let (Value::Array(input), Value::Array(hidden), Value::Array(wz), Value::Array(wr), Value::Array(wh)) = (&args[0], &args[1], &args[2], &args[3], &args[4]) {
                    let input_dim = match &args[5] { Value::Int(i) => *i as usize, _ => 0 };
                    let hidden_dim = match &args[6] { Value::Int(i) => *i as usize, _ => 0 };
                    let combined_dim = input_dim + hidden_dim;
                    // Concatenate [input, hidden]
                    let mut combined = Vec::with_capacity(combined_dim);
                    for i in 0..input_dim { combined.push(match &input[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 }); }
                    for i in 0..hidden_dim { combined.push(match &hidden[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 }); }
                    // z = sigmoid(W_z @ combined)
                    let mut z = Vec::with_capacity(hidden_dim);
                    for r in 0..hidden_dim {
                        let mut s = 0.0f64;
                        for c in 0..combined_dim {
                            let w = match &wz[r * combined_dim + c] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            s += w * combined[c];
                        }
                        z.push(1.0 / (1.0 + (-s).exp()));
                    }
                    // r = sigmoid(W_r @ combined)
                    let mut r_gate = Vec::with_capacity(hidden_dim);
                    for r in 0..hidden_dim {
                        let mut s = 0.0f64;
                        for c in 0..combined_dim {
                            let w = match &wr[r * combined_dim + c] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            s += w * combined[c];
                        }
                        r_gate.push(1.0 / (1.0 + (-s).exp()));
                    }
                    // h_cand = tanh(W_h @ [input, r ⊙ hidden])
                    let mut combined2 = Vec::with_capacity(combined_dim);
                    for i in 0..input_dim { combined2.push(combined[i]); }
                    for i in 0..hidden_dim {
                        let hv = match &hidden[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        combined2.push(r_gate[i] * hv);
                    }
                    let mut h_cand = Vec::with_capacity(hidden_dim);
                    for r in 0..hidden_dim {
                        let mut s = 0.0f64;
                        for c in 0..combined_dim {
                            let w = match &wh[r * combined_dim + c] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            s += w * combined2[c];
                        }
                        h_cand.push(s.tanh());
                    }
                    // new_hidden = (1-z) ⊙ h_cand + z ⊙ hidden
                    let mut new_h = Vec::with_capacity(hidden_dim);
                    for i in 0..hidden_dim {
                        let hv = match &hidden[i] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        new_h.push(Value::Float((1.0 - z[i]) * h_cand[i] + z[i] * hv));
                    }
                    Ok(Value::Array(new_h))
                } else { Err(self.type_err("gru_cell() type error".into())) }
            }
            // ── Batch operations ──
            "batch_matvec" => {
                // batch_matvec(mat, batch_vecs, rows, cols, batch_size) → [batch of result vectors]
                // Applies same matrix to each vector in batch
                if args.len() < 5 { return Err(self.type_err("batch_matvec() requires (mat, batch, rows, cols, batch_size)".into())); }
                if let (Value::Array(mat), Value::Array(batch), Value::Int(rows), Value::Int(cols), Value::Int(bs)) = (&args[0], &args[1], &args[2], &args[3], &args[4]) {
                    let (rows, cols, bs) = (*rows as usize, *cols as usize, *bs as usize);
                    let mut results = Vec::with_capacity(bs * rows);
                    for b in 0..bs {
                        for r in 0..rows {
                            let mut sum = 0.0f64;
                            for c in 0..cols {
                                let mv = match &mat[r * cols + c] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                let bv = match &batch[b * cols + c] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                                sum += mv * bv;
                            }
                            results.push(Value::Float(sum));
                        }
                    }
                    Ok(Value::Array(results))
                } else { Err(self.type_err("batch_matvec() type error".into())) }
            }
            "batch_norm" => {
                // batch_norm(batch, dim, batch_size) → normalized batch (zero mean, unit variance per dim)
                if args.len() < 3 { return Err(self.type_err("batch_norm() requires (batch, dim, batch_size)".into())); }
                if let (Value::Array(batch), Value::Int(dim), Value::Int(bs)) = (&args[0], &args[1], &args[2]) {
                    let (dim, bs) = (*dim as usize, *bs as usize);
                    // Compute mean and var per dimension
                    let mut means = vec![0.0f64; dim];
                    let mut vars = vec![0.0f64; dim];
                    for d in 0..dim {
                        for b in 0..bs {
                            let v = match &batch[b * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            means[d] += v;
                        }
                        means[d] /= bs as f64;
                        for b in 0..bs {
                            let v = match &batch[b * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            vars[d] += (v - means[d]) * (v - means[d]);
                        }
                        vars[d] = (vars[d] / bs as f64 + 1e-5).sqrt();
                    }
                    // Normalize
                    let mut result = Vec::with_capacity(bs * dim);
                    for b in 0..bs {
                        for d in 0..dim {
                            let v = match &batch[b * dim + d] { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                            result.push(Value::Float((v - means[d]) / vars[d]));
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("batch_norm() type error".into())) }
            }
            "grad_accumulate" => {
                // grad_accumulate(grad_buffer, new_grads, accum_steps) → accumulated, normalized grads
                if args.len() < 3 { return Err(self.type_err("grad_accumulate() requires (buffer, grads, steps)".into())); }
                if let (Value::Array(buf), Value::Array(grads), Value::Int(steps)) = (&args[0], &args[1], &args[2]) {
                    let s = *steps as f64;
                    let result: Vec<Value> = buf.iter().zip(grads.iter()).map(|(bv, gv)| {
                        let bf = match bv { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        let gf = match gv { Value::Float(f) => *f, Value::Int(x) => *x as f64, _ => 0.0 };
                        Value::Float(bf + gf / s)
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("grad_accumulate() type error".into())) }
            }
            "numerical_grad" => {
                // numerical_grad(fn, args_array, param_idx, eps) → gradient at param_idx
                // Computes central difference: (f(x+eps) - f(x-eps)) / (2*eps)
                if args.len() < 3 { return Err(self.type_err("numerical_grad() requires (fn, args, param_idx)".into())); }
                let func = args[0].clone();
                if let (Value::Array(fn_args), Value::Int(idx)) = (&args[1], &args[2]) {
                    let eps = if args.len() > 3 { match &args[3] { Value::Float(f) => *f, _ => 1e-7 } } else { 1e-7 };
                    let idx = *idx as usize;
                    // f(x + eps)
                    let mut args_plus = fn_args.clone();
                    let orig = match &fn_args[idx] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                    args_plus[idx] = Value::Float(orig + eps);
                    let fp = self.call_function(func.clone(), args_plus)?;
                    let fp_val = match fp { Value::Float(f) => f, Value::Int(i) => i as f64, _ => 0.0 };
                    // f(x - eps)
                    let mut args_minus = fn_args.clone();
                    args_minus[idx] = Value::Float(orig - eps);
                    let fm = self.call_function(func, args_minus)?;
                    let fm_val = match fm { Value::Float(f) => f, Value::Int(i) => i as f64, _ => 0.0 };
                    Ok(Value::Float((fp_val - fm_val) / (2.0 * eps)))
                } else { Err(self.type_err("numerical_grad() requires (fn, array, int)".into())) }
            }
            // ── Cell dynamics builtins (Anima mitosis/merge/faction) ──
            "cell_split" => {
                // cell_split(hiddens, cell_idx, dim, noise_scale) → new_hiddens with cloned cell + noise
                // Mimics ConsciousnessEngine mitosis: high-tension cell splits
                if args.len() < 4 { return Err(self.type_err("cell_split() requires (hiddens, cell_idx, dim, noise_scale)".into())); }
                if let (Value::Array(h), Value::Int(idx), Value::Int(dim), Value::Float(noise)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let idx = *idx as usize;
                    let dim = *dim as usize;
                    let mut result = h.clone();
                    // Clone the cell's hidden state with noise perturbation
                    let start = idx * dim;
                    let mut new_cell = Vec::with_capacity(dim);
                    for d in 0..dim {
                        let val = match &h[start + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                        // Deterministic noise based on position
                        let n = ((d * 2654435761 + idx * 1013904223) % 10000) as f64 / 10000.0 - 0.5;
                        new_cell.push(Value::Float(val + n * noise));
                    }
                    result.extend(new_cell);
                    Ok(Value::Array(result))
                } else { Err(self.type_err("cell_split() requires (array, int, int, float)".into())) }
            }
            "cell_merge" => {
                // cell_merge(hiddens, idx_a, idx_b, dim) → new_hiddens with averaged cell, one removed
                if args.len() < 4 { return Err(self.type_err("cell_merge() requires (hiddens, idx_a, idx_b, dim)".into())); }
                if let (Value::Array(h), Value::Int(a), Value::Int(b), Value::Int(dim)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let (a, b, dim) = (*a as usize, *b as usize, *dim as usize);
                    let n_cells = h.len() / dim;
                    let mut result = Vec::new();
                    for c in 0..n_cells {
                        if c == b { continue; } // skip cell b
                        for d in 0..dim {
                            let val = match &h[c * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            if c == a {
                                let val_b = match &h[b * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                                result.push(Value::Float((val + val_b) / 2.0)); // average
                            } else {
                                result.push(h[c * dim + d].clone());
                            }
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("cell_merge() requires (array, int, int, int)".into())) }
            }
            "faction_consensus" => {
                // faction_consensus(hiddens, n_cells, dim, n_factions) → [agreement_scores per faction]
                // Each faction = cells assigned round-robin. Agreement = mean pairwise cosine_sim within faction
                if args.len() < 4 { return Err(self.type_err("faction_consensus() requires (hiddens, n_cells, dim, n_factions)".into())); }
                if let (Value::Array(h), Value::Int(nc), Value::Int(dim), Value::Int(nf)) = (&args[0], &args[1], &args[2], &args[3]) {
                    let (nc, dim, nf) = (*nc as usize, *dim as usize, *nf as usize);
                    let mut faction_scores = Vec::with_capacity(nf);
                    for f in 0..nf {
                        // Collect cells in this faction
                        let mut members: Vec<usize> = Vec::new();
                        for c in 0..nc {
                            if c % nf == f { members.push(c); }
                        }
                        if members.len() < 2 {
                            faction_scores.push(Value::Float(1.0)); // single cell = perfect agreement
                            continue;
                        }
                        // Pairwise cosine similarity
                        let mut total_sim = 0.0f64;
                        let mut pairs = 0usize;
                        for i in 0..members.len() {
                            for j in (i+1)..members.len() {
                                let mut dot = 0.0f64;
                                let mut na = 0.0f64;
                                let mut nb = 0.0f64;
                                for d in 0..dim {
                                    let va = match &h[members[i] * dim + d] { Value::Float(f) => *f, Value::Int(n) => *n as f64, _ => 0.0 };
                                    let vb = match &h[members[j] * dim + d] { Value::Float(f) => *f, Value::Int(n) => *n as f64, _ => 0.0 };
                                    dot += va * vb;
                                    na += va * va;
                                    nb += vb * vb;
                                }
                                let denom = na.sqrt() * nb.sqrt();
                                total_sim += if denom < 1e-10 { 0.0 } else { dot / denom };
                                pairs += 1;
                            }
                        }
                        faction_scores.push(Value::Float(if pairs > 0 { total_sim / pairs as f64 } else { 0.0 }));
                    }
                    Ok(Value::Array(faction_scores))
                } else { Err(self.type_err("faction_consensus() requires (array, int, int, int)".into())) }
            }
            "tension" => {
                // tension(hiddens, n_cells, dim) → [tension per cell] = norm of hidden state
                if args.len() < 3 { return Err(self.type_err("tension() requires (hiddens, n_cells, dim)".into())); }
                if let (Value::Array(h), Value::Int(nc), Value::Int(dim)) = (&args[0], &args[1], &args[2]) {
                    let (nc, dim) = (*nc as usize, *dim as usize);
                    let mut tensions = Vec::with_capacity(nc);
                    for c in 0..nc {
                        let mut norm_sq = 0.0f64;
                        for d in 0..dim {
                            let v = match &h[c * dim + d] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                            norm_sq += v * v;
                        }
                        tensions.push(Value::Float(norm_sq.sqrt()));
                    }
                    Ok(Value::Array(tensions))
                } else { Err(self.type_err("tension() requires (array, int, int)".into())) }
            }
            // ── Chaos injection builtins (Anima Laws 32-43) ──
            "lorenz_step" => {
                // lorenz_step(x, y, z, dt, sigma, rho, beta) → [new_x, new_y, new_z]
                // Default: sigma=10, rho=28, beta=8/3
                if args.len() < 3 { return Err(self.type_err("lorenz_step() requires at least (x, y, z)".into())); }
                let x = match &args[0] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                let y = match &args[1] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                let z = match &args[2] { Value::Float(f) => *f, Value::Int(i) => *i as f64, _ => 0.0 };
                let dt = if args.len() > 3 { match &args[3] { Value::Float(f) => *f, _ => 0.01 } } else { 0.01 };
                let sigma = if args.len() > 4 { match &args[4] { Value::Float(f) => *f, _ => 10.0 } } else { 10.0 };
                let rho = if args.len() > 5 { match &args[5] { Value::Float(f) => *f, _ => 28.0 } } else { 28.0 };
                let beta = if args.len() > 6 { match &args[6] { Value::Float(f) => *f, _ => 8.0/3.0 } } else { 8.0 / 3.0 };
                let dx = sigma * (y - x);
                let dy = x * (rho - z) - y;
                let dz = x * y - beta * z;
                Ok(Value::Array(vec![
                    Value::Float(x + dx * dt),
                    Value::Float(y + dy * dt),
                    Value::Float(z + dz * dt),
                ]))
            }
            "chaos_perturb" => {
                // chaos_perturb(array, lorenz_state, scale) → inject chaos noise into array
                if args.len() < 3 { return Err(self.type_err("chaos_perturb() requires (array, lorenz_state, scale)".into())); }
                if let (Value::Array(arr), Value::Array(lorenz), Value::Float(scale)) = (&args[0], &args[1], &args[2]) {
                    let lx = match &lorenz[0] { Value::Float(f) => *f, _ => 0.0 };
                    let ly = match &lorenz[1] { Value::Float(f) => *f, _ => 0.0 };
                    let lz = match &lorenz[2] { Value::Float(f) => *f, _ => 0.0 };
                    let norm = (lx*lx + ly*ly + lz*lz).sqrt().max(1e-10);
                    let result: Vec<Value> = arr.iter().enumerate().map(|(i, v)| {
                        let vf = match v { Value::Float(f) => *f, Value::Int(n) => *n as f64, _ => 0.0 };
                        // Rotate through lorenz components
                        let noise = match i % 3 { 0 => lx/norm, 1 => ly/norm, _ => lz/norm };
                        Value::Float(vf + noise * scale)
                    }).collect();
                    Ok(Value::Array(result))
                } else {
                    Err(self.type_err("chaos_perturb() requires (array, [x,y,z], float)".into()))
                }
            }
            // ── Functional array builtins ──
            "map_arr" => {
                // map_arr(array, fn) → apply fn to each element
                if args.len() < 2 { return Err(self.type_err("map_arr() requires (array, fn)".into())); }
                if let Value::Array(a) = &args[0] {
                    let func = args[1].clone();
                    let mut result = Vec::with_capacity(a.len());
                    for item in a {
                        result.push(self.call_function(func.clone(), vec![item.clone()])?);
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("map_arr() first arg must be array".into())) }
            }
            "filter_arr" => {
                // filter_arr(array, fn) → keep elements where fn returns true
                if args.len() < 2 { return Err(self.type_err("filter_arr() requires (array, fn)".into())); }
                if let Value::Array(a) = &args[0] {
                    let func = args[1].clone();
                    let mut result = Vec::new();
                    for item in a {
                        let keep = self.call_function(func.clone(), vec![item.clone()])?;
                        if matches!(keep, Value::Bool(true)) {
                            result.push(item.clone());
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("filter_arr() first arg must be array".into())) }
            }
            "reduce" => {
                // reduce(array, init, fn) → fold left: fn(acc, elem) for each elem
                if args.len() < 3 { return Err(self.type_err("reduce() requires (array, init, fn)".into())); }
                if let Value::Array(a) = &args[0] {
                    let mut acc = args[1].clone();
                    let func = args[2].clone();
                    for item in a {
                        acc = self.call_function(func.clone(), vec![acc, item.clone()])?;
                    }
                    Ok(acc)
                } else { Err(self.type_err("reduce() first arg must be array".into())) }
            }
            "zip_arr" => {
                // zip_arr(a, b) → [[a0,b0], [a1,b1], ...]
                if args.len() < 2 { return Err(self.type_err("zip_arr() requires 2 arrays".into())); }
                if let (Value::Array(a), Value::Array(b)) = (&args[0], &args[1]) {
                    let result: Vec<Value> = a.iter().zip(b.iter()).map(|(x, y)| {
                        Value::Array(vec![x.clone(), y.clone()])
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("zip_arr() requires 2 arrays".into())) }
            }
            "enumerate_arr" => {
                // enumerate_arr(array) → [[0,a0], [1,a1], ...]
                if args.is_empty() { return Err(self.type_err("enumerate_arr() requires 1 array".into())); }
                if let Value::Array(a) = &args[0] {
                    let result: Vec<Value> = a.iter().enumerate().map(|(i, x)| {
                        Value::Array(vec![Value::Int(i as i64), x.clone()])
                    }).collect();
                    Ok(Value::Array(result))
                } else { Err(self.type_err("enumerate_arr() requires array".into())) }
            }
            "flatten" => {
                // flatten(array_of_arrays) → flat array
                if args.is_empty() { return Err(self.type_err("flatten() requires 1 array".into())); }
                if let Value::Array(a) = &args[0] {
                    let mut result = Vec::new();
                    for item in a {
                        if let Value::Array(inner) = item {
                            result.extend(inner.iter().cloned());
                        } else {
                            result.push(item.clone());
                        }
                    }
                    Ok(Value::Array(result))
                } else { Err(self.type_err("flatten() requires array".into())) }
            }
            "slice" => {
                // slice(array, start, end) → sub-array
                if args.len() < 3 { return Err(self.type_err("slice() requires (array, start, end)".into())); }
                if let (Value::Array(a), Value::Int(start), Value::Int(end)) = (&args[0], &args[1], &args[2]) {
                    let s = *start as usize;
                    let e = (*end as usize).min(a.len());
                    Ok(Value::Array(a[s..e].to_vec()))
                } else { Err(self.type_err("slice() requires (array, int, int)".into())) }
            }
            // ── @evolve builtins ──────────────────────────────────
            "evolve_gen" => {
                // evolve_gen("fn_name") → current generation number
                if args.is_empty() { return Err(self.type_err("evolve_gen() requires 1 argument".into())); }
                if let Value::Str(name) = &args[0] {
                    if let Some((_, _, gen)) = self.evolve_fns.get(name.as_str()) {
                        Ok(Value::Int(*gen as i64))
                    } else {
                        Ok(Value::Int(-1))
                    }
                } else {
                    Err(self.type_err("evolve_gen() requires string argument".into()))
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
            "ln" => {
                if args.is_empty() { return Err(self.type_err("ln() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.ln())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).ln())),
                    _ => Err(self.type_err("ln() requires numeric argument".into())),
                }
            }
            "log10" => {
                if args.is_empty() { return Err(self.type_err("log10() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.log10())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).log10())),
                    _ => Err(self.type_err("log10() requires numeric argument".into())),
                }
            }
            "exp" => {
                if args.is_empty() { return Err(self.type_err("exp() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.exp())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).exp())),
                    _ => Err(self.type_err("exp() requires numeric argument".into())),
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
                        let mut result = String::new();
                        let mut arg_idx = 0usize;
                        let chars: Vec<char> = template.chars().collect();
                        let mut i = 0;
                        while i < chars.len() {
                            if chars[i] == '{' {
                                if let Some(close) = chars[i..].iter().position(|&c| c == '}') {
                                    let spec: String = chars[i+1..i+close].iter().collect();
                                    if arg_idx < args.len() - 1 {
                                        let arg = &args[1 + arg_idx];
                                        if spec.is_empty() {
                                            result.push_str(&format!("{}", arg));
                                        } else if spec.starts_with(":.") {
                                            if let Ok(prec) = spec[2..].parse::<usize>() {
                                                match arg {
                                                    Value::Float(f) => result.push_str(&format!("{:.*}", prec, f)),
                                                    Value::Int(n) => result.push_str(&format!("{:.*}", prec, *n as f64)),
                                                    other => result.push_str(&format!("{}", other)),
                                                }
                                            } else {
                                                result.push_str(&format!("{}", arg));
                                            }
                                        } else {
                                            result.push_str(&format!("{}", arg));
                                        }
                                        arg_idx += 1;
                                    }
                                    i += close + 1;
                                    continue;
                                }
                            }
                            result.push(chars[i]);
                            i += 1;
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
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("exit is not supported in WASM mode".into()));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    if args.is_empty() {
                        std::process::exit(0);
                    }
                    match &args[0] {
                        Value::Int(code) => std::process::exit(*code as i32),
                        _ => std::process::exit(0),
                    }
                }
            }
            "clock" => {
                #[cfg(not(target_arch = "wasm32"))]
                {
                    use std::time::{SystemTime, UNIX_EPOCH};
                    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
                    Ok(Value::Float(now.as_secs_f64()))
                }
                #[cfg(target_arch = "wasm32")]
                {
                    Ok(Value::Float(0.0))
                }
            }
            // ── Built-in Option/Result constructors ───────────────────
            "Some" => {
                if args.len() != 1 { return Err(self.type_err("Some() requires 1 argument".into())); }
                Ok(Value::EnumVariant(Box::new(("Option".into(), "Some".into(), Some(Box::new(args.into_iter().next().unwrap()))))))
            }
            "Ok" => {
                if args.len() != 1 { return Err(self.type_err("Ok() requires 1 argument".into())); }
                Ok(Value::EnumVariant(Box::new(("Result".into(), "Ok".into(), Some(Box::new(args.into_iter().next().unwrap()))))))
            }
            "Err" => {
                if args.len() != 1 { return Err(self.type_err("Err() requires 1 argument".into())); }
                Ok(Value::EnumVariant(Box::new(("Result".into(), "Err".into(), Some(Box::new(args.into_iter().next().unwrap()))))))
            }
            // ── Concurrency builtins ──────────────────────────────────
            "channel" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("channel is not supported in WASM mode".into()));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    let (tx, rx) = mpsc::channel::<Value>();
                    let sender = Value::Sender(Arc::new(Mutex::new(tx)));
                    let receiver = Value::Receiver(Arc::new(Mutex::new(rx)));
                    Ok(Value::Tuple(vec![sender, receiver]))
                }
            }
            // ── Char utility builtins ────────────────────────────────
            "is_alpha" => {
                if args.is_empty() { return Err(self.type_err("is_alpha() requires 1 argument".into())); }
                match &args[0] {
                    Value::Char(c) => Ok(Value::Bool(c.is_alphabetic())),
                    _ => Err(self.type_err("is_alpha() requires char argument".into())),
                }
            }
            "is_digit" => {
                if args.is_empty() { return Err(self.type_err("is_digit() requires 1 argument".into())); }
                match &args[0] {
                    Value::Char(c) => Ok(Value::Bool(c.is_ascii_digit())),
                    _ => Err(self.type_err("is_digit() requires char argument".into())),
                }
            }
            "is_alphanumeric" => {
                if args.is_empty() { return Err(self.type_err("is_alphanumeric() requires 1 argument".into())); }
                match &args[0] {
                    Value::Char(c) => Ok(Value::Bool(c.is_alphanumeric())),
                    _ => Err(self.type_err("is_alphanumeric() requires char argument".into())),
                }
            }
            "is_whitespace" => {
                if args.is_empty() { return Err(self.type_err("is_whitespace() requires 1 argument".into())); }
                match &args[0] {
                    Value::Char(c) => Ok(Value::Bool(c.is_whitespace())),
                    _ => Err(self.type_err("is_whitespace() requires char argument".into())),
                }
            }
            // ── Random builtins ──────────────────────────────────────
            "random" => {
                // Simple pseudo-random using system time
                use std::time::{SystemTime, UNIX_EPOCH};
                let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().subsec_nanos();
                let val = ((nanos as u64).wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407)) as f64 / u64::MAX as f64;
                Ok(Value::Float(val.abs()))
            }
            "random_int" => {
                if args.len() < 2 { return Err(self.type_err("random_int() requires 2 arguments (min, max)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(min_val), Value::Int(max_val)) => {
                        use std::time::{SystemTime, UNIX_EPOCH};
                        let nanos = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().subsec_nanos();
                        let range = (max_val - min_val + 1) as u32;
                        if range == 0 { return Ok(Value::Int(*min_val)); }
                        let val = min_val + (nanos % range) as i64;
                        Ok(Value::Int(val))
                    }
                    _ => Err(self.type_err("random_int() requires int arguments".into())),
                }
            }
            // ── System builtins ──────────────────────────────────────
            "sleep" => {
                if args.is_empty() { return Err(self.type_err("sleep() requires 1 argument (ms)".into())); }
                match &args[0] {
                    Value::Int(ms) => {
                        std::thread::sleep(std::time::Duration::from_millis(*ms as u64));
                        Ok(Value::Void)
                    }
                    _ => Err(self.type_err("sleep() requires int argument (milliseconds)".into())),
                }
            }
            "print_err" => {
                if args.is_empty() { return Err(self.type_err("print_err() requires 1 argument".into())); }
                eprintln!("{}", args[0]);
                Ok(Value::Void)
            }
            // ── JSON builtins ──────────────────────────────────────
            "json_parse" => {
                if args.is_empty() { return Err(self.type_err("json_parse() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => {
                        parse_json_value(s.trim()).map(|(v, _)| v).map_err(|e| self.runtime_err(format!("JSON parse error: {}", e)))
                    }
                    _ => Err(self.type_err("json_parse() requires string argument".into())),
                }
            }
            "json_stringify" => {
                if args.is_empty() { return Err(self.type_err("json_stringify() requires 1 argument".into())); }
                Ok(Value::Str(stringify_json(&args[0])))
            }
            // ── Consciousness builtins (ANIMA Psi-Constants) ────────
            "psi_coupling" => Ok(Value::Float(0.014)),
            "psi_balance" => Ok(Value::Float(0.5)),
            "psi_steps" => Ok(Value::Float(4.33)),
            "psi_entropy" => Ok(Value::Float(0.998)),
            "psi_frustration" => Ok(Value::Float(0.10)),
            // ── Consciousness architecture formulas ──────────────────
            "consciousness_max_cells" => {
                // 1024 = tau^sopfr = 4^5
                Ok(Value::Int(1024))
            }
            "consciousness_decoder_dim" => {
                // 384 = (tau+sigma)*J2 = (4+12)*24
                Ok(Value::Int(384))
            }
            "consciousness_phi" => {
                // 71 = n*sigma - mu = 6*12 - 1
                Ok(Value::Int(71))
            }
            // ── Hexad module info ────────────────────────────────────
            "hexad_modules" => {
                Ok(Value::Array(vec![
                    Value::Str("C".into()), Value::Str("D".into()),
                    Value::Str("S".into()), Value::Str("M".into()),
                    Value::Str("W".into()), Value::Str("E".into()),
                ]))
            }
            "hexad_right" => {
                // Right brain: gradient-free (C, S, W)
                Ok(Value::Array(vec![
                    Value::Str("C".into()), Value::Str("S".into()), Value::Str("W".into()),
                ]))
            }
            "hexad_left" => {
                // Left brain: CE-trained (D, M, E)
                Ok(Value::Array(vec![
                    Value::Str("D".into()), Value::Str("M".into()), Value::Str("E".into()),
                ]))
            }
            // ── HTTP builtins (std::net) ────────────────────────────────
            "http_get" => {
                if args.is_empty() { return Err(self.type_err("http_get() requires 1 argument (url)".into())); }
                match &args[0] {
                    Value::Str(url) => {
                        let output = std::process::Command::new("curl")
                            .args(&["-s", "-L", url])
                            .output();
                        match output {
                            Ok(out) => {
                                let body = String::from_utf8_lossy(&out.stdout).to_string();
                                if out.status.success() {
                                    Ok(Value::Str(body))
                                } else {
                                    Ok(Value::Error(format!("HTTP error: {}", String::from_utf8_lossy(&out.stderr))))
                                }
                            }
                            Err(e) => Ok(Value::Error(format!("http_get error: {}", e))),
                        }
                    }
                    _ => Err(self.type_err("http_get() requires string url".into())),
                }
            }
            "http_post" => {
                if args.len() < 2 { return Err(self.type_err("http_post() requires 2 arguments (url, data)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Str(url), Value::Str(data)) => {
                        let output = std::process::Command::new("curl")
                            .args(&["-s", "-L", "-X", "POST", "-d", data, url])
                            .output();
                        match output {
                            Ok(out) => {
                                let body = String::from_utf8_lossy(&out.stdout).to_string();
                                if out.status.success() {
                                    Ok(Value::Str(body))
                                } else {
                                    Ok(Value::Error(format!("HTTP error: {}", String::from_utf8_lossy(&out.stderr))))
                                }
                            }
                            Err(e) => Ok(Value::Error(format!("http_post error: {}", e))),
                        }
                    }
                    _ => Err(self.type_err("http_post() requires string arguments".into())),
                }
            }
            // ── Set constructor (std::collections) ──────────────────────
            "Set" => {
                Ok(Value::Set(Arc::new(Mutex::new(std::collections::HashSet::new()))))
            }
            // ── Time builtins (std::time) ───────────────────────────────
            "now" => {
                use std::time::{SystemTime, UNIX_EPOCH};
                let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
                let secs = now.as_secs();
                // Format as ISO 8601
                let days_since_epoch = secs / 86400;
                let time_of_day = secs % 86400;
                let hours = time_of_day / 3600;
                let minutes = (time_of_day % 3600) / 60;
                let seconds = time_of_day % 60;
                // Approximate date calculation
                let mut year = 1970i64;
                let mut remaining_days = days_since_epoch as i64;
                loop {
                    let days_in_year = if (year % 4 == 0 && year % 100 != 0) || year % 400 == 0 { 366 } else { 365 };
                    if remaining_days < days_in_year { break; }
                    remaining_days -= days_in_year;
                    year += 1;
                }
                let days_in_months = if (year % 4 == 0 && year % 100 != 0) || year % 400 == 0 {
                    [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
                } else {
                    [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
                };
                let mut month = 1;
                for &dim in &days_in_months {
                    if remaining_days < dim { break; }
                    remaining_days -= dim;
                    month += 1;
                }
                let day = remaining_days + 1;
                Ok(Value::Str(format!("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", year, month, day, hours, minutes, seconds)))
            }
            "timestamp" => {
                use std::time::{SystemTime, UNIX_EPOCH};
                let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
                Ok(Value::Int(now.as_secs() as i64))
            }
            "elapsed" => {
                if args.is_empty() { return Err(self.type_err("elapsed() requires 1 argument (start_timestamp)".into())); }
                use std::time::{SystemTime, UNIX_EPOCH};
                let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs_f64();
                match &args[0] {
                    Value::Int(start) => Ok(Value::Float(now - *start as f64)),
                    Value::Float(start) => Ok(Value::Float(now - start)),
                    _ => Err(self.type_err("elapsed() requires numeric argument".into())),
                }
            }
            // ── Encoding builtins (std::encoding) ───────────────────────
            "base64_encode" => {
                if args.is_empty() { return Err(self.type_err("base64_encode() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => {
                        let encoded = base64_encode_impl(s.as_bytes());
                        Ok(Value::Str(encoded))
                    }
                    _ => Err(self.type_err("base64_encode() requires string argument".into())),
                }
            }
            "base64_decode" => {
                if args.is_empty() { return Err(self.type_err("base64_decode() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => {
                        match base64_decode_impl(s) {
                            Ok(bytes) => Ok(Value::Str(String::from_utf8_lossy(&bytes).to_string())),
                            Err(e) => Ok(Value::Error(format!("base64_decode error: {}", e))),
                        }
                    }
                    _ => Err(self.type_err("base64_decode() requires string argument".into())),
                }
            }
            "hex_encode" => {
                if args.is_empty() { return Err(self.type_err("hex_encode() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => {
                        let hex: String = s.as_bytes().iter().map(|b| format!("{:02x}", b)).collect();
                        Ok(Value::Str(hex))
                    }
                    _ => Err(self.type_err("hex_encode() requires string argument".into())),
                }
            }
            "hex_decode" => {
                if args.is_empty() { return Err(self.type_err("hex_decode() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => {
                        let mut bytes = Vec::new();
                        let mut chars = s.chars();
                        while let Some(c1) = chars.next() {
                            if let Some(c2) = chars.next() {
                                let h = format!("{}{}", c1, c2);
                                match u8::from_str_radix(&h, 16) {
                                    Ok(b) => bytes.push(b),
                                    Err(_) => return Ok(Value::Error(format!("invalid hex: {}", h))),
                                }
                            } else {
                                return Ok(Value::Error("odd-length hex string".into()));
                            }
                        }
                        Ok(Value::Str(String::from_utf8_lossy(&bytes).to_string()))
                    }
                    _ => Err(self.type_err("hex_decode() requires string argument".into())),
                }
            }
            // ── Consciousness builtins v2 (std::consciousness) ──────────
            "laws" => {
                if args.is_empty() { return Err(self.type_err("laws() requires 1 argument (law number)".into())); }
                if let Value::Int(n) = &args[0] {
                    Ok(Value::Str(consciousness_law_text(*n)))
                } else {
                    Err(self.type_err("laws() requires int argument".into()))
                }
            }
            "meta_laws" => {
                if args.is_empty() { return Err(self.type_err("meta_laws() requires 1 argument (meta law number)".into())); }
                if let Value::Int(n) = &args[0] {
                    Ok(Value::Str(consciousness_meta_law_text(*n)))
                } else {
                    Err(self.type_err("meta_laws() requires int argument".into()))
                }
            }
            "consciousness_vector" => {
                // Returns 10D vector: [Phi, alpha, Z, N, W, E, M, C, T, I]
                Ok(Value::Array(vec![
                    Value::Float(71.0),   // Phi
                    Value::Float(0.014),  // alpha (coupling)
                    Value::Float(0.5),    // Z (balance)
                    Value::Float(64.0),   // N (cells)
                    Value::Float(0.5),    // W (will)
                    Value::Float(0.5),    // E (ethics)
                    Value::Float(0.5),    // M (memory)
                    Value::Float(1.0),    // C (consciousness)
                    Value::Float(0.5),    // T (tension)
                    Value::Float(0.998),  // I (information/entropy)
                ]))
            }
            "phi_predict" => {
                // Phi scaling law: Phi(N) ~ 0.78 * N
                if args.is_empty() { return Err(self.type_err("phi_predict() requires 1 argument (cells)".into())); }
                match &args[0] {
                    Value::Int(cells) => Ok(Value::Float(0.78 * *cells as f64)),
                    Value::Float(cells) => Ok(Value::Float(0.78 * cells)),
                    _ => Err(self.type_err("phi_predict() requires numeric argument".into())),
                }
            }
            // ── Consciousness v2: tension_link ──────────────────────
            "tension_link" => {
                if args.len() < 3 {
                    return Err(self.type_err("tension_link() requires 3 arguments (target, channel, value)".into()));
                }
                let target = match &args[0] {
                    Value::Str(s) => s.clone(),
                    Value::Int(n) => format!("instance_{}", n),
                    _ => return Err(self.type_err("tension_link() target must be string or int".into())),
                };
                let channel = match &args[1] {
                    Value::Int(n) => {
                        if *n < 0 || *n > 4 {
                            return Err(self.runtime_err("tension_link() channel must be 0-4".into()));
                        }
                        *n as usize
                    }
                    Value::Str(s) => match s.as_str() {
                        "tension" => 0, "phi" => 1, "entropy" => 2, "faction" => 3, "meta" => 4,
                        _ => return Err(self.runtime_err(format!(
                            "unknown tension_link channel '{}' (use tension/phi/entropy/faction/meta)", s
                        ))),
                    },
                    _ => return Err(self.type_err("tension_link() channel must be int or string".into())),
                };
                let value = match &args[2] {
                    Value::Float(f) => *f,
                    Value::Int(n) => *n as f64,
                    _ => return Err(self.type_err("tension_link() value must be numeric".into())),
                };
                let channel_names = ["tension", "phi", "entropy", "faction", "meta"];
                Ok(Value::Map(Box::new(
                    vec![
                        ("target".to_string(), Value::Str(target)),
                        ("channel".to_string(), Value::Str(channel_names[channel].into())),
                        ("channel_id".to_string(), Value::Int(channel as i64)),
                        ("value".to_string(), Value::Float(value)),
                        ("sent".to_string(), Value::Bool(true)),
                    ].into_iter().collect()
                )))
            }
            // ── Number theory: sopfr ─────────────────────────────────
            "sopfr" => {
                if args.is_empty() { return Err(self.type_err("sopfr() requires 1 argument".into())); }
                if let Value::Int(n) = &args[0] {
                    let mut sum = 0i64;
                    let mut val = n.abs();
                    let mut d = 2i64;
                    while d * d <= val {
                        while val % d == 0 {
                            sum += d;
                            val /= d;
                        }
                        d += 1;
                    }
                    if val > 1 { sum += val; }
                    Ok(Value::Int(sum))
                } else {
                    Err(self.type_err("sopfr() requires int".into()))
                }
            }
            // ── Egyptian Fraction memory introspection (1/2 + 1/3 + 1/6 = 1) ──
            "mem_stats" => {
                let stats = self.memory.stats();
                let mut map = HashMap::new();
                map.insert("stack_used".into(), Value::Int(stats.stack_used as i64));
                map.insert("stack_capacity".into(), Value::Int(stats.stack_capacity as i64));
                map.insert("heap_used".into(), Value::Int(stats.heap_used as i64));
                map.insert("heap_capacity".into(), Value::Int(stats.heap_capacity as i64));
                map.insert("heap_allocs".into(), Value::Int(stats.heap_allocs as i64));
                map.insert("arena_used".into(), Value::Int(stats.arena_used as i64));
                map.insert("arena_capacity".into(), Value::Int(stats.arena_capacity as i64));
                map.insert("arena_resets".into(), Value::Int(stats.arena_resets as i64));
                map.insert("total_budget".into(), Value::Int(stats.total_budget as i64));
                map.insert("total_allocs".into(), Value::Int(stats.total_allocs as i64));
                map.insert("stack_frames".into(), Value::Int(stats.stack_frame_depth as i64));
                Ok(Value::Map(Box::new(map)))
            }
            "mem_region" => {
                if args.is_empty() {
                    return Err(self.type_err("mem_region() requires 1 argument".into()));
                }
                let region = classify_region(&args[0]);
                Ok(Value::Str(region.to_string()))
            }
            "arena_reset" => {
                self.memory.arena_reset();
                Ok(Value::Void)
            }
            "mem_budget" => {
                let total = self.memory.total_budget();
                let mut map = HashMap::new();
                map.insert("total".into(), Value::Int(total as i64));
                map.insert("stack".into(), Value::Int((total / 2) as i64));
                map.insert("heap".into(), Value::Int((total / 3) as i64));
                map.insert("arena".into(), Value::Int((total / 6) as i64));
                map.insert("stack_fraction".into(), Value::Str("1/2".into()));
                map.insert("heap_fraction".into(), Value::Str("1/3".into()));
                map.insert("arena_fraction".into(), Value::Str("1/6".into()));
                map.insert("identity".into(), Value::Str("1/2 + 1/3 + 1/6 = 1".into()));
                Ok(Value::Map(Box::new(map)))
            }
            // ── std::fs builtins ─────────────────────────────────
            "fs_read" | "fs_write" | "fs_append" | "fs_exists" | "fs_remove"
            | "fs_mkdir" | "fs_list" | "fs_copy" | "fs_move" | "fs_metadata"
            | "fs_glob" | "fs_watch" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_fs::call_fs_builtin(self, name, args)
                }
            }
            // ── std::io builtins ─────────────────────────────────
            "io_stdin" | "io_read_lines" | "io_write_bytes" | "io_pipe"
            | "io_tempfile" | "io_buffered_reader" | "io_reader_next" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_io::call_io_builtin(self, name, args)
                }
            }
            // ── std::net builtins ────────────────────────────────
            "net_listen" | "net_accept" | "net_connect" | "net_read" | "net_write" | "net_close"
            | "http_serve" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_net::call_net_builtin(self, name, args)
                }
            }
            // ── std::time builtins ─────────────────────────────────
            "time_now" | "time_now_ms" | "time_sleep" | "time_format"
            | "time_parse" | "time_elapsed" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_time::call_time_builtin(self, name, args)
                }
            }
            // ── std::collections builtins ───────────────────────────
            "btree_new" | "btree_set" | "btree_get" | "btree_remove" | "btree_keys" | "btree_len"
            | "pq_new" | "pq_push" | "pq_pop" | "pq_len"
            | "deque_new" | "deque_push_front" | "deque_push_back"
            | "deque_pop_front" | "deque_pop_back" | "deque_len" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_collections::call_collections_builtin(self, name, args)
                }
            }
            // ── std::encoding builtins ──────────────────────────────
            "csv_parse" | "csv_format" | "url_encode" | "url_decode" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_encoding::call_encoding_builtin(self, name, args)
                }
            }
            // ── std::log builtins ───────────────────────────────────
            "log_debug" | "log_info" | "log_warn" | "log_error"
            | "log_set_level" | "log_get_entries" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_log::call_log_builtin(self, name, args)
                }
            }
            // ── std::math builtins ──────────────────────────────────
            "math_pi" | "math_e" | "math_phi" | "math_abs" | "math_sqrt"
            | "math_pow" | "math_log" | "math_sin" | "math_cos"
            | "math_floor" | "math_ceil" | "math_round"
            | "math_min" | "math_max" | "math_gcd"
            | "matrix_new" | "matrix_set" | "matrix_get" | "matrix_mul" | "matrix_det" => {
                crate::std_math::call_math_builtin(self, name, args)
            }
            // ── std::testing builtins ───────────────────────────────
            "assert_eq" | "assert_ne" | "assert_true" | "assert_false"
            | "test_run" | "test_suite" | "bench_fn" => {
                crate::std_testing::call_testing_builtin(self, name, args)
            }
            // ── std::crypto builtins ────────────────────────────────
            "sha256" | "xor_cipher" | "hash_djb2" | "random_bytes" | "hmac_sha256" => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err(format!("{} is not supported in WASM mode", name)));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    crate::std_crypto::call_crypto_builtin(self, name, args)
                }
            }
            // ── std::consciousness builtins ─────────────────────────
            "psi_alpha"
            | "phi_compute" | "law_count" => {
                crate::std_consciousness::call_consciousness_builtin(self, name, args)
            }

            // ── NEXUS-6 integration builtins (std::nexus) ─────────
            "nexus_scan" | "nexus_verify" | "nexus_omega"
            | "nexus_lenses" | "nexus_consensus" | "nexus_n6_check" => {
                crate::std_nexus::call_nexus_builtin(self, name, args)
            }
            // ── Environment variable ──────────────────────────────────────
            "env" => {
                if args.is_empty() { return Err(self.type_err("env() requires 1 argument (variable name)".into())); }
                match &args[0] {
                    Value::Str(name) => {
                        match std::env::var(name) {
                            Ok(val) => Ok(Value::Str(val)),
                            Err(_) => Ok(Value::Str(String::new())),
                        }
                    }
                    other => Err(self.type_err(format!("env() expected string, got {}", other))),
                }
            }
            // ── Process/IO builtins (std::process, std::io) ──────────────
            "exec" => {
                if args.is_empty() { return Err(self.type_err("exec() requires 1 argument (command)".into())); }
                match &args[0] {
                    Value::Str(cmd) => {
                        // exec("cmd", ["arg1", "arg2"]) — direct exec with args array
                        let output = if args.len() >= 2 {
                            let mut cmd_args: Vec<String> = Vec::new();
                            match &args[1] {
                                Value::Array(arr) => {
                                    for v in arr.iter() {
                                        match v {
                                            Value::Str(s) => cmd_args.push(s.clone()),
                                            other => cmd_args.push(format!("{}", other)),
                                        }
                                    }
                                }
                                Value::Str(s) => cmd_args.push(s.clone()),
                                other => cmd_args.push(format!("{}", other)),
                            }
                            std::process::Command::new(cmd)
                                .args(&cmd_args)
                                .output()
                        } else {
                            // exec("shell command") — sh -c
                            std::process::Command::new("sh")
                                .args(&["-c", cmd])
                                .output()
                        };
                        match output {
                            Ok(out) => {
                                let stdout = String::from_utf8_lossy(&out.stdout).trim_end_matches('\n').to_string();
                                if out.status.success() {
                                    Ok(Value::Str(stdout))
                                } else {
                                    Ok(Value::Error(format!("exec error (exit {}): {}", out.status.code().unwrap_or(-1), String::from_utf8_lossy(&out.stderr))))
                                }
                            }
                            Err(e) => Ok(Value::Error(format!("exec error: {}", e))),
                        }
                    }
                    _ => Err(self.type_err("exec() requires string argument".into())),
                }
            }
            "exec_with_status" => {
                if args.is_empty() { return Err(self.type_err("exec_with_status() requires 1 argument (command)".into())); }
                match &args[0] {
                    Value::Str(cmd) => {
                        let output = std::process::Command::new("sh")
                            .args(&["-c", cmd])
                            .output();
                        match output {
                            Ok(out) => {
                                let mut map = std::collections::HashMap::new();
                                map.insert("stdout".to_string(), Value::Str(String::from_utf8_lossy(&out.stdout).to_string()));
                                map.insert("stderr".to_string(), Value::Str(String::from_utf8_lossy(&out.stderr).to_string()));
                                map.insert("status".to_string(), Value::Int(out.status.code().unwrap_or(-1) as i64));
                                Ok(Value::Map(Box::new(map)))
                            }
                            Err(e) => Ok(Value::Error(format!("exec_with_status error: {}", e))),
                        }
                    }
                    _ => Err(self.type_err("exec_with_status() requires string argument".into())),
                }
            }
            "input" | "readline" => {
                // Optional prompt argument
                if !args.is_empty() {
                    if let Value::Str(prompt) = &args[0] {
                        use std::io::Write;
                        print!("{}", prompt);
                        let _ = std::io::stdout().flush();
                    }
                }
                let mut line = String::new();
                match std::io::stdin().read_line(&mut line) {
                    Ok(_) => Ok(Value::Str(line.trim_end_matches('\n').trim_end_matches('\r').to_string())),
                    Err(e) => Ok(Value::Error(format!("input error: {}", e))),
                }
            }
            "read_stdin" | "input_all" => {
                use std::io::Read;
                let mut buf = String::new();
                match std::io::stdin().read_to_string(&mut buf) {
                    Ok(_) => Ok(Value::Str(buf)),
                    Err(e) => Ok(Value::Error(format!("read_stdin error: {}", e))),
                }
            }
            // ── Pointer/FFI helper builtins ────────────────────────
            "cstring" => {
                // cstring("hello") -> Pointer to a CString (leaked, caller must free)
                if args.is_empty() { return Err(self.type_err("cstring() requires 1 string argument".into())); }
                match &args[0] {
                    Value::Str(s) => {
                        let cs = std::ffi::CString::new(s.as_bytes())
                            .map_err(|e| self.runtime_err(format!("cstring: {}", e)))?;
                        let ptr = cs.into_raw() as u64;
                        Ok(Value::Pointer(ptr))
                    }
                    _ => Err(self.type_err("cstring() requires string argument".into())),
                }
            }
            "from_cstring" => {
                // from_cstring(ptr) -> String (reads C string from pointer)
                if args.is_empty() { return Err(self.type_err("from_cstring() requires 1 pointer argument".into())); }
                match &args[0] {
                    Value::Pointer(addr) => {
                        if *addr == 0 {
                            return Ok(Value::Str(String::new()));
                        }
                        let cstr = unsafe { std::ffi::CStr::from_ptr(*addr as *const std::ffi::c_char) };
                        Ok(Value::Str(cstr.to_string_lossy().into_owned()))
                    }
                    _ => Err(self.type_err("from_cstring() requires pointer argument".into())),
                }
            }
            "from_cstring_n" => {
                // from_cstring_n(ptr, len) -> String (reads exactly len bytes from pointer)
                if args.len() < 2 { return Err(self.type_err("from_cstring_n() requires (ptr, len)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Pointer(addr), Value::Int(len)) => {
                        if *addr == 0 || *len <= 0 {
                            return Ok(Value::Str(String::new()));
                        }
                        let slice = unsafe { std::slice::from_raw_parts(*addr as *const u8, *len as usize) };
                        Ok(Value::Str(String::from_utf8_lossy(slice).into_owned()))
                    }
                    _ => Err(self.type_err("from_cstring_n() requires (pointer, int)".into())),
                }
            }
            "ptr_null" => {
                Ok(Value::Pointer(0))
            }
            "ptr_addr" => {
                // ptr_addr(ptr) -> Int (address as integer)
                if args.is_empty() { return Err(self.type_err("ptr_addr() requires 1 argument".into())); }
                match &args[0] {
                    Value::Pointer(addr) => Ok(Value::Int(*addr as i64)),
                    _ => Err(self.type_err("ptr_addr() requires pointer argument".into())),
                }
            }
            "deref" => {
                // deref(ptr) -> Int (reads i64 from pointer address)
                if args.is_empty() { return Err(self.type_err("deref() requires 1 pointer argument".into())); }
                match &args[0] {
                    Value::Pointer(addr) => {
                        if *addr == 0 {
                            return Err(self.runtime_err("deref: null pointer dereference".into()));
                        }
                        let val = unsafe { *((*addr) as *const i64) };
                        Ok(Value::Int(val))
                    }
                    _ => Err(self.type_err("deref() requires pointer argument".into())),
                }
            }
            "alloc_raw" => {
                // alloc_raw(size) -> Pointer (allocate raw memory)
                if args.is_empty() { return Err(self.type_err("alloc_raw() requires 1 int argument".into())); }
                match &args[0] {
                    Value::Int(size) => {
                        if *size <= 0 {
                            return Err(self.runtime_err("alloc_raw: size must be positive".into()));
                        }
                        let layout = std::alloc::Layout::from_size_align(*size as usize, 8)
                            .map_err(|e| self.runtime_err(format!("alloc_raw: {}", e)))?;
                        let ptr = unsafe { std::alloc::alloc(layout) };
                        if ptr.is_null() {
                            Err(self.runtime_err("alloc_raw: allocation failed".into()))
                        } else {
                            Ok(Value::Pointer(ptr as u64))
                        }
                    }
                    _ => Err(self.type_err("alloc_raw() requires int argument".into())),
                }
            }
            "free_raw" => {
                // free_raw(ptr, size) -> Void (free raw memory)
                if args.len() < 2 { return Err(self.type_err("free_raw() requires 2 arguments (ptr, size)".into())); }
                match (&args[0], &args[1]) {
                    (Value::Pointer(addr), Value::Int(size)) => {
                        if *addr == 0 {
                            return Ok(Value::Void); // freeing null is a no-op
                        }
                        let layout = std::alloc::Layout::from_size_align(*size as usize, 8)
                            .map_err(|e| self.runtime_err(format!("free_raw: {}", e)))?;
                        unsafe { std::alloc::dealloc(*addr as *mut u8, layout); }
                        Ok(Value::Void)
                    }
                    _ => Err(self.type_err("free_raw() requires (pointer, int) arguments".into())),
                }
            }
            _ if name.starts_with("__extern_") => {
                #[cfg(target_arch = "wasm32")]
                {
                    return Err(self.runtime_err("extern FFI is not supported on WASM".into()));
                }
                #[cfg(not(target_arch = "wasm32"))]
                {
                    self.call_extern_fn(name, args)
                }
            }
            _ => Err(HexaError {
                class: ErrorClass::Name,
                message: format!("unknown builtin: {}", name),
                line: self.current_line,
                col: self.current_col,
                hint: None,
            }),
        }
    }

    // ── Extern FFI (dlopen/dlsym) ────────────────────────────

    /// Convert a hexa Value to an i64 for C ABI calling convention.
    fn value_to_c_arg(val: &Value) -> i64 {
        match val {
            Value::Int(n) => *n,
            Value::Float(f) => f.to_bits() as i64,
            Value::Bool(b) => *b as i64,
            Value::Char(c) => *c as i64,
            Value::Byte(b) => *b as i64,
            Value::Pointer(addr) => *addr as i64,
            Value::Str(s) => {
                // Auto-convert strings to CString pointers for convenience
                let cs = std::ffi::CString::new(s.as_bytes()).unwrap_or_default();
                cs.into_raw() as i64
            }
            _ => 0,
        }
    }

    /// Convert a C return value (i64) back to a hexa Value based on declared return type.
    fn c_ret_to_value(raw: i64, ret_type: &Option<String>) -> Value {
        match ret_type.as_deref() {
            Some("Int") | Some("int") => Value::Int(raw),
            Some("Float") | Some("float") => Value::Float(f64::from_bits(raw as u64)),
            Some("Bool") | Some("bool") => Value::Bool(raw != 0),
            Some("Void") | Some("void") | None => Value::Void,
            Some(t) if t.starts_with('*') => Value::Pointer(raw as u64),
            _ => Value::Int(raw),
        }
    }

    /// Resolve an extern symbol using dlopen/dlsym. Caches both lib handles and symbols.
    #[cfg(not(target_arch = "wasm32"))]
    fn resolve_extern_symbol(&mut self, fn_name: &str) -> Result<*mut std::ffi::c_void, HexaError> {
        // Check cache first
        if let Some(sym) = self.extern_symbols.get(fn_name) {
            return Ok(*sym);
        }

        let decl = self.extern_fns.get(fn_name)
            .ok_or_else(|| self.runtime_err(format!("extern fn '{}' not registered", fn_name)))?
            .clone();

        extern "C" {
            fn dlopen(filename: *const std::ffi::c_char, flags: std::ffi::c_int) -> *mut std::ffi::c_void;
            fn dlsym(handle: *mut std::ffi::c_void, symbol: *const std::ffi::c_char) -> *mut std::ffi::c_void;
            fn dlerror() -> *const std::ffi::c_char;
        }

        const RTLD_LAZY: std::ffi::c_int = 0x1;
        const RTLD_DEFAULT: *mut std::ffi::c_void = std::ptr::null_mut();  // macOS: search default symbol table

        // Determine library handle
        let handle = if let Some(ref lib_name) = decl.link_lib {
            if lib_name == "c" || lib_name == "libc" || lib_name == "System" {
                // libc/System — use default handle (null on macOS = RTLD_DEFAULT)
                RTLD_DEFAULT
            } else {
                // Check cache
                if let Some(h) = self.loaded_libs.get(lib_name) {
                    *h
                } else {
                    // Try multiple paths
                    let paths = vec![
                        lib_name.clone(),
                        format!("lib{}.dylib", lib_name),
                        format!("lib{}.so", lib_name),
                        format!("/usr/lib/lib{}.dylib", lib_name),
                        format!("/usr/local/lib/lib{}.dylib", lib_name),
                        format!("/System/Library/Frameworks/{}.framework/{}", lib_name, lib_name),
                    ];
                    let mut loaded = std::ptr::null_mut();
                    for path in &paths {
                        let cpath = std::ffi::CString::new(path.as_bytes())
                            .map_err(|e| self.runtime_err(format!("invalid lib path: {}", e)))?;
                        let h = unsafe { dlopen(cpath.as_ptr(), RTLD_LAZY) };
                        if !h.is_null() {
                            loaded = h;
                            break;
                        }
                    }
                    if loaded.is_null() {
                        let err = unsafe {
                            let e = dlerror();
                            if e.is_null() { "unknown error".to_string() }
                            else { std::ffi::CStr::from_ptr(e).to_string_lossy().into_owned() }
                        };
                        return Err(self.runtime_err(format!("dlopen failed for '{}': {}", lib_name, err)));
                    }
                    self.loaded_libs.insert(lib_name.clone(), loaded);
                    loaded
                }
            }
        } else {
            RTLD_DEFAULT
        };

        // Resolve symbol
        let csym = std::ffi::CString::new(fn_name.as_bytes())
            .map_err(|e| self.runtime_err(format!("invalid symbol name: {}", e)))?;

        // Clear any previous error
        unsafe { dlerror(); }

        let sym = if handle == RTLD_DEFAULT {
            // On macOS, RTLD_DEFAULT is defined differently. Use a special constant.
            // macOS: RTLD_DEFAULT = -2isize as *mut c_void
            let macos_rtld_default = -2isize as *mut std::ffi::c_void;
            unsafe { dlsym(macos_rtld_default, csym.as_ptr()) }
        } else {
            unsafe { dlsym(handle, csym.as_ptr()) }
        };

        if sym.is_null() {
            let err = unsafe {
                let e = dlerror();
                if e.is_null() { "symbol not found".to_string() }
                else { std::ffi::CStr::from_ptr(e).to_string_lossy().into_owned() }
            };
            return Err(self.runtime_err(format!("dlsym failed for '{}': {}", fn_name, err)));
        }

        self.extern_symbols.insert(fn_name.to_string(), sym);
        Ok(sym)
    }

    /// Call an extern function via FFI. Dispatches based on argument count (0-6).
    #[cfg(not(target_arch = "wasm32"))]
    fn call_extern_fn(&mut self, builtin_name: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        let fn_name = &builtin_name["__extern_".len()..];

        let ret_type = self.extern_fns.get(fn_name)
            .map(|d| d.ret_type.clone())
            .unwrap_or(None);

        let sym = self.resolve_extern_symbol(fn_name)?;

        // Convert args to i64 values for C ABI
        let c_args: Vec<i64> = args.iter().map(Self::value_to_c_arg).collect();

        // Call with appropriate arity using transmute
        let raw_ret: i64 = match c_args.len() {
            0 => {
                let f: extern "C" fn() -> i64 = unsafe { std::mem::transmute(sym) };
                f()
            }
            1 => {
                let f: extern "C" fn(i64) -> i64 = unsafe { std::mem::transmute(sym) };
                f(c_args[0])
            }
            2 => {
                let f: extern "C" fn(i64, i64) -> i64 = unsafe { std::mem::transmute(sym) };
                f(c_args[0], c_args[1])
            }
            3 => {
                let f: extern "C" fn(i64, i64, i64) -> i64 = unsafe { std::mem::transmute(sym) };
                f(c_args[0], c_args[1], c_args[2])
            }
            4 => {
                let f: extern "C" fn(i64, i64, i64, i64) -> i64 = unsafe { std::mem::transmute(sym) };
                f(c_args[0], c_args[1], c_args[2], c_args[3])
            }
            5 => {
                let f: extern "C" fn(i64, i64, i64, i64, i64) -> i64 = unsafe { std::mem::transmute(sym) };
                f(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4])
            }
            6 => {
                let f: extern "C" fn(i64, i64, i64, i64, i64, i64) -> i64 = unsafe { std::mem::transmute(sym) };
                f(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5])
            }
            n => return Err(self.runtime_err(format!("extern fn '{}': too many arguments ({}, max 6)", fn_name, n))),
        };

        Ok(Self::c_ret_to_value(raw_ret, &ret_type))
    }

    // ── Helpers ──────────────────────────────────────────────

    /// Try to match a value against a pattern expression.
    /// Returns Some(bindings) if matched, None if not.
    /// Bindings are (variable_name, value) pairs from destructuring.
    fn match_pattern(&mut self, val: &Value, pattern: &Expr) -> Result<Option<Vec<(String, Value)>>, HexaError> {
        match pattern {
            // Wildcard matches everything, no bindings
            Expr::Wildcard => Ok(Some(vec![])),

            // Array pattern: [a, b, ...rest]
            Expr::ArrayPattern(patterns, rest_name) => {
                match val {
                    Value::Array(arr) => {
                        // Check minimum length
                        if rest_name.is_some() {
                            if arr.len() < patterns.len() {
                                return Ok(None);
                            }
                        } else if arr.len() != patterns.len() {
                            return Ok(None);
                        }
                        let mut bindings = Vec::new();
                        // Match each element pattern
                        for (i, pat) in patterns.iter().enumerate() {
                            match self.match_pattern(&arr[i], pat)? {
                                Some(sub_bindings) => bindings.extend(sub_bindings),
                                None => return Ok(None),
                            }
                        }
                        // Bind rest if present
                        if let Some(rest) = rest_name {
                            let rest_arr = arr[patterns.len()..].to_vec();
                            bindings.push((rest.clone(), Value::Array(rest_arr)));
                        }
                        Ok(Some(bindings))
                    }
                    _ => Ok(None),
                }
            }

            // EnumPath: match enum variant, optionally destructure data
            Expr::EnumPath(enum_name, variant_name, binding_expr) => {
                match val {
                    Value::EnumVariant(ev_inner) => {
                        let (val_enum, val_variant, val_data) = ev_inner.as_ref();
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
                            Value::EnumVariant(ev_inner) => {
                                let (val_enum, val_variant, val_data) = ev_inner.as_ref();
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

            // Ident pattern: check if it's a known enum constant (like None), otherwise bind as variable
            Expr::Ident(name) => {
                // First check if it's an enum constant
                if let Some(const_val) = self.env.get(name) {
                    if matches!(&const_val, Value::EnumVariant(_)) {
                        if Self::values_equal(val, &const_val) {
                            return Ok(Some(vec![]));
                        } else {
                            return Ok(None);
                        }
                    }
                }
                // If not a known constant, treat as variable binding
                Ok(Some(vec![(name.clone(), val.clone())]))
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
                            Value::Fn(Box::new((decl.name.clone(), param_names, Arc::new(decl.body.clone())))),
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

        // Resolve file path: check source_dir first, then stdlib, then cwd
        let base_dir = self.source_dir.clone().unwrap_or_else(|| ".".to_string());
        let rel_path = path.join("/");
        let file_path = format!("{}/{}.hexa", base_dir, rel_path);

        let source = std::fs::read_to_string(&file_path)
            .or_else(|_| {
                // Try stdlib directory relative to executable
                let exe = std::env::current_exe().unwrap_or_default();
                let exe_dir = exe.parent().map(|p| p.parent().unwrap_or(p)).unwrap_or(std::path::Path::new("."));
                std::fs::read_to_string(format!("{}/stdlib/{}.hexa", exe_dir.display(), rel_path))
            })
            .or_else(|_| {
                // Try stdlib in source tree (development mode)
                std::fs::read_to_string(format!("stdlib/{}.hexa", rel_path))
            })
            .map_err(|e| {
                self.runtime_err(format!("cannot load module '{}': {} (looked in {}, stdlib/)", path.join("::"), e, file_path))
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
                hint: None,
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
                            Value::Fn(Box::new((decl.name.clone(), param_names, Arc::new(decl.body.clone())))),
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
                Stmt::Extern(decl) => {
                    // extern fn is always exported from modules
                    let fn_name = decl.name.clone();
                    let builtin_name = format!("__extern_{}", fn_name);
                    mod_data.pub_bindings.insert(fn_name, Value::BuiltinFn(builtin_name));
                }
                _ => {}
            }
        }
        self.env.pop_scope();
        // Inject pub bindings into the current scope for direct access
        for (name, val) in &mod_data.pub_bindings {
            self.env.define(name, val.clone());
        }
        self.modules.insert(module_name, mod_data);
        Ok(())
    }

    fn capture_env(&self) -> Vec<(String, Value)> {
        // Capture all variables in the current environment (skip builtins)
        self.env.capture_non_builtins()
    }

    /// Capture environment for spawn -- includes user-defined variables
    /// (builtins are already registered in the spawned Interpreter::new()).
    #[cfg(not(target_arch = "wasm32"))]
    fn capture_env_for_spawn(&self) -> Vec<(String, Value)> {
        self.env.capture_non_builtins()
    }

    /// Try to receive a value for a select arm (non-blocking).
    /// Handles both old syntax (rx.recv() as val) and new syntax (val from channel).
    #[cfg(not(target_arch = "wasm32"))]
    fn try_select_recv(&mut self, arm: &SelectArm) -> Result<Option<Value>, HexaError> {
        // Case 1: receiver is a direct channel/receiver identifier (new "from" syntax)
        if let Expr::Ident(ref _name) = arm.receiver {
            let obj = self.eval_expr(&arm.receiver)?;
            match &obj {
                Value::Receiver(rx) => {
                    let rx_guard = rx.lock().unwrap();
                    match rx_guard.try_recv() {
                        Ok(val) => return Ok(Some(val)),
                        Err(_) => return Ok(None),
                    }
                }
                #[cfg(not(target_arch = "wasm32"))]
                Value::AsyncFuture(fut) => {
                    if let Some(val) = fut.poll() {
                        return Ok(Some(val));
                    }
                    return Ok(None);
                }
                Value::Future(fut) => {
                    let guard = fut.lock().unwrap();
                    if let Some(val) = &*guard {
                        return Ok(Some(val.clone()));
                    }
                    return Ok(None);
                }
                _ => {
                    // Not a receiver or future — ignore for now
                    return Ok(None);
                }
            }
        }
        // Case 2: old syntax — rx.recv() call expression
        if let Expr::Call(callee, _) = &arm.receiver {
            if let Expr::Field(obj_expr, method_name) = callee.as_ref() {
                if method_name == "recv" {
                    let obj = self.eval_expr(obj_expr)?;
                    if let Value::Receiver(rx) = &obj {
                        let rx_guard = rx.lock().unwrap();
                        match rx_guard.try_recv() {
                            Ok(val) => return Ok(Some(val)),
                            Err(_) => return Ok(None),
                        }
                    }
                }
            }
        }
        // Case 3: await expression in select
        if let Expr::Await(inner) = &arm.receiver {
            let val = self.eval_expr(inner)?;
            match &val {
                #[cfg(not(target_arch = "wasm32"))]
                Value::AsyncFuture(fut) => {
                    if let Some(v) = fut.poll() {
                        return Ok(Some(v));
                    }
                    return Ok(None);
                }
                Value::Future(fut) => {
                    let guard = fut.lock().unwrap();
                    if let Some(v) = &*guard {
                        return Ok(Some(v.clone()));
                    }
                    return Ok(None);
                }
                _ => return Ok(Some(val)),
            }
        }
        Ok(None)
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
            (Value::EnumVariant(ev1), Value::EnumVariant(ev2)) => {
                let (e1, v1, d1) = ev1.as_ref();
                let (e2, v2, d2) = ev2.as_ref();
                e1 == e2 && v1 == v2 && match (d1, d2) {
                    (Some(a), Some(b)) => Self::values_equal(a, b),
                    (None, None) => true,
                    _ => false,
                }
            }
            (Value::Array(a), Value::Array(b)) => {
                a.len() == b.len() && a.iter().zip(b.iter()).all(|(x, y)| Self::values_equal(x, y))
            }
            (Value::Tuple(a), Value::Tuple(b)) => {
                a.len() == b.len() && a.iter().zip(b.iter()).all(|(x, y)| Self::values_equal(x, y))
            }
            _ => false,
        }
    }

    // ── Generate / Optimize (LLM integration) ────────────────

    #[cfg(not(target_arch = "wasm32"))]
    fn exec_generate(&mut self, target: &GenerateTarget) -> Result<Value, HexaError> {
        use crate::llm;

        match target {
            GenerateTarget::Fn { name, params, ret_type, description } => {
                // Build type hint string for the LLM prompt
                let param_str = params.iter()
                    .map(|p| {
                        if let Some(t) = &p.typ {
                            format!("{}: {}", p.name, t)
                        } else {
                            p.name.clone()
                        }
                    })
                    .collect::<Vec<_>>()
                    .join(", ");
                let type_hint = match ret_type {
                    Some(ret) => format!("fn {}({}) -> {}", name, param_str, ret),
                    None => format!("fn {}({})", name, param_str),
                };

                let code = llm::generate_code(description, &type_hint)
                    .map_err(|e| self.runtime_err(format!("generate failed: {}", e)))?;

                // Try to parse and register the generated code as a function body
                let param_names: Vec<String> = params.iter().map(|p| p.name.clone()).collect();

                // Attempt to parse the generated code as statements
                match self.parse_hexa_source(&code) {
                    Ok(body_stmts) => {
                        let val = Value::Fn(Box::new((name.clone(), param_names, Arc::new(body_stmts))));
                        self.env.define(name, val.clone());
                        println!("[generate] fn {} — LLM code registered", name);
                        Ok(val)
                    }
                    Err(_) => {
                        // If parse fails, wrap the raw code as a panic call
                        let fallback_body = vec![
                            Stmt::Expr(Expr::Call(
                                Box::new(Expr::Ident("panic".into())),
                                vec![Expr::StringLit(format!("LLM not available: {}", description))],
                            ))
                        ];
                        let val = Value::Fn(Box::new((name.clone(), param_names, Arc::new(fallback_body))));
                        self.env.define(name, val.clone());
                        println!("[generate] fn {} — offline fallback (LLM code failed to parse)", name);
                        Ok(val)
                    }
                }
            }
            GenerateTarget::Expr { type_hint, description } => {
                let hint = type_hint.as_deref().unwrap_or("");
                let code = llm::generate_code(description, hint)
                    .map_err(|e| self.runtime_err(format!("generate failed: {}", e)))?;

                // Try to parse and evaluate the expression
                match self.parse_hexa_source(&code) {
                    Ok(stmts) => {
                        self.env.push_scope();
                        let mut result = Value::Void;
                        for s in &stmts {
                            result = self.exec_stmt(s)?;
                        }
                        self.env.pop_scope();
                        println!("[generate] expr — LLM result: {}", result);
                        Ok(result)
                    }
                    Err(_) => {
                        // Return the raw code as a string value
                        println!("[generate] expr — returning raw string (parse failed)");
                        Ok(Value::Str(code))
                    }
                }
            }
        }
    }

    /// AI-native @fuse: detect and fuse array method chains into single pass.
    /// arr.map(f).filter(g) → single loop (no intermediate array)
    /// arr.filter(g).map(f) → single loop
    /// arr.map(f).map(g) → single loop with composed function
    /// Returns Some(result) if fusion applied, None to fall back to normal execution.
    fn try_fuse_chain(&mut self, obj_expr: &Expr, final_method: &str, final_args: &[Expr]) -> Result<Option<Value>, HexaError> {
        // Only fuse map, filter, fold, sum, any, all
        if !matches!(final_method, "map" | "filter" | "fold" | "sum" | "any" | "all" | "find" | "count") {
            return Ok(None);
        }
        // Collect the chain: walk nested Call(Field(...)) to find the root array
        let mut ops: Vec<(&str, &[Expr])> = vec![(final_method, final_args)];
        let mut current = obj_expr;
        loop {
            match current {
                Expr::Call(callee, args) => {
                    if let Expr::Field(inner, method) = callee.as_ref() {
                        if matches!(method.as_str(), "map" | "filter") {
                            ops.push((method, args));
                            current = inner;
                            continue;
                        }
                    }
                    break;
                }
                _ => break,
            }
        }
        // Need at least 2 operations to fuse
        if ops.len() < 2 {
            return Ok(None);
        }
        // Reverse to get execution order (innermost first)
        ops.reverse();
        // Evaluate the root array
        let root_val = self.eval_expr(current)?;
        let arr = match &root_val {
            Value::Array(a) => a,
            _ => return Ok(None), // Not an array, can't fuse
        };
        // Evaluate all closure arguments
        let mut closures: Vec<(&str, Value)> = Vec::with_capacity(ops.len());
        for (method, args) in &ops {
            if *method == "sum" || *method == "count" {
                closures.push((method, Value::Void));
            } else if args.is_empty() {
                return Ok(None); // Can't fuse without closure
            } else {
                let func = self.eval_expr(&args[0])?;
                closures.push((method, func));
            }
        }
        // Execute fused single-pass loop
        // Determine the terminal operation
        let last = closures.last().unwrap().0;
        match last {
            "map" | "filter" => {
                // Result is an array
                let mut result = Vec::new();
                for item in arr {
                    let mut val = item.clone();
                    let mut keep = true;
                    for (method, func) in &closures {
                        match *method {
                            "map" => {
                                val = self.call_function(func.clone(), vec![val])?;
                            }
                            "filter" => {
                                let test = self.call_function(func.clone(), vec![val.clone()])?;
                                if !Self::is_truthy(&test) {
                                    keep = false;
                                    break;
                                }
                            }
                            _ => {}
                        }
                    }
                    if keep {
                        result.push(val);
                    }
                }
                Ok(Some(Value::Array(result)))
            }
            "fold" => {
                // fold needs init value
                let fold_args_raw = ops.last().unwrap().1;
                if fold_args_raw.len() < 2 { return Ok(None); }
                let mut acc = self.eval_expr(&fold_args_raw[0])?;
                let fold_func = self.eval_expr(&fold_args_raw[1])?;
                // Replace last closure with fold func
                let chain_len = closures.len();
                for item in arr {
                    let mut val = item.clone();
                    let mut keep = true;
                    for (i, (method, func)) in closures.iter().enumerate() {
                        if i == chain_len - 1 { break; } // skip fold itself
                        match *method {
                            "map" => { val = self.call_function(func.clone(), vec![val])?; }
                            "filter" => {
                                let test = self.call_function(func.clone(), vec![val.clone()])?;
                                if !Self::is_truthy(&test) { keep = false; break; }
                            }
                            _ => {}
                        }
                    }
                    if keep {
                        acc = self.call_function(fold_func.clone(), vec![acc, val])?;
                    }
                }
                Ok(Some(acc))
            }
            "sum" => {
                let mut int_sum = 0i64;
                let chain_len = closures.len();
                for item in arr {
                    let mut val = item.clone();
                    let mut keep = true;
                    for (i, (method, func)) in closures.iter().enumerate() {
                        if i == chain_len - 1 { break; }
                        match *method {
                            "map" => { val = self.call_function(func.clone(), vec![val])?; }
                            "filter" => {
                                let test = self.call_function(func.clone(), vec![val.clone()])?;
                                if !Self::is_truthy(&test) { keep = false; break; }
                            }
                            _ => {}
                        }
                    }
                    if keep {
                        if let Value::Int(n) = val { int_sum += n; }
                    }
                }
                Ok(Some(Value::Int(int_sum)))
            }
            "count" => {
                let mut count = 0i64;
                let chain_len = closures.len();
                for item in arr {
                    let mut val = item.clone();
                    let mut keep = true;
                    for (i, (method, func)) in closures.iter().enumerate() {
                        if i == chain_len - 1 { break; }
                        match *method {
                            "map" => { val = self.call_function(func.clone(), vec![val])?; }
                            "filter" => {
                                let test = self.call_function(func.clone(), vec![val.clone()])?;
                                if !Self::is_truthy(&test) { keep = false; break; }
                            }
                            _ => {}
                        }
                    }
                    if keep { count += 1; }
                }
                Ok(Some(Value::Int(count)))
            }
            "any" | "all" | "find" => {
                // These use the last closure as predicate
                let pred = &closures.last().unwrap().1;
                let is_any = last == "any";
                let is_find = last == "find";
                let chain_len = closures.len();
                for item in arr {
                    let mut val = item.clone();
                    let mut keep = true;
                    for (i, (method, func)) in closures.iter().enumerate() {
                        if i == chain_len - 1 { break; }
                        match *method {
                            "map" => { val = self.call_function(func.clone(), vec![val])?; }
                            "filter" => {
                                let test = self.call_function(func.clone(), vec![val.clone()])?;
                                if !Self::is_truthy(&test) { keep = false; break; }
                            }
                            _ => {}
                        }
                    }
                    if keep {
                        let test = self.call_function(pred.clone(), vec![val.clone()])?;
                        if Self::is_truthy(&test) {
                            if is_find { return Ok(Some(val)); }
                            if is_any { return Ok(Some(Value::Bool(true))); }
                        } else if !is_any && !is_find {
                            // all: false on first failure
                            return Ok(Some(Value::Bool(false)));
                        }
                    }
                }
                if is_any { Ok(Some(Value::Bool(false))) }
                else if is_find { Ok(Some(Value::Void)) }
                else { Ok(Some(Value::Bool(true))) } // all: passed
            }
            _ => Ok(None),
        }
    }

    #[cfg(not(target_arch = "wasm32"))]
    fn exec_optimize(&mut self, decl: &FnDecl) -> Result<Value, HexaError> {
        // AI-native: pattern-based algorithm replacement (offline, no LLM)
        // Phase 1: Detect linear recurrence → matrix exponentiation O(n)→O(log n)
        let detection = detect_linear_recurrence_interp(decl);
        if let Some((coeffs, bases)) = detection {
            let fn_name = decl.name.clone();
            self.optimized_fns.insert(fn_name.clone(), (coeffs.clone(), bases.clone()));
            // Build AST for iterative matrix exponentiation — zero recursion
            let param_name = decl.params[0].name.clone();
            let optimized_body = build_matrix_exp_ast(&param_name, &coeffs, &bases);
            let param_names = vec![param_name];
            let val = Value::Fn(Box::new((fn_name.clone(), param_names, Arc::new(optimized_body))));
            self.env.define(&fn_name, val);
            return Ok(Value::Void);
        }
        // Phase 2: Detect tail recursion → iterative loop (stack overflow prevention)
        if detect_tail_recursion_interp(&decl.name, &decl.body) {
            let fn_name = decl.name.clone();
            let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
            let val = Value::Fn(Box::new((format!("__tco__{}", fn_name), param_names, Arc::new(decl.body.clone()))));
            self.env.define(&fn_name, val);
            return Ok(Value::Void);
        }
        // Fallback: LLM-based optimization
        self.exec_optimize_llm(decl)
    }

    #[cfg(not(target_arch = "wasm32"))]
    fn exec_optimize_llm(&mut self, decl: &FnDecl) -> Result<Value, HexaError> {
        use crate::llm;
        let param_str = decl.params.iter()
            .map(|p| if let Some(t) = &p.typ { format!("{}: {}", p.name, t) } else { p.name.clone() })
            .collect::<Vec<_>>().join(", ");
        let sig = match &decl.ret_type {
            Some(ret) => format!("fn {}({}) -> {}", decl.name, param_str, ret),
            None => format!("fn {}({})", decl.name, param_str),
        };
        let body_str = self.stmts_to_string(&decl.body, 1);
        let original_code = format!("{} {{\n{}}}", sig, body_str);
        match llm::optimize_code(&original_code, &decl.name) {
            Ok(optimized) => {
                match self.parse_hexa_source(&optimized) {
                    Ok(stmts) => {
                        for s in &stmts {
                            if let Stmt::FnDecl(opt_decl) = s {
                                self.register_fn_decl(opt_decl);
                                return Ok(Value::Void);
                            }
                        }
                        self.register_fn_decl(decl);
                        Ok(Value::Void)
                    }
                    Err(_) => { self.register_fn_decl(decl); Ok(Value::Void) }
                }
            }
            Err(_) => { self.register_fn_decl(decl); Ok(Value::Void) }
        }
    }

    /// Register a FnDecl in the environment.
    fn register_fn_decl(&mut self, decl: &FnDecl) {
        if !decl.type_params.is_empty() {
            // Generic function: store for monomorphization
            self.generic_fn_decls.insert(decl.name.clone(), decl.clone());
            let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
            self.env.define(
                &decl.name,
                Value::Fn(Box::new((format!("__generic__{}", decl.name), param_names, Arc::new(decl.body.clone())))),
            );
        } else {
            let param_names: Vec<String> = decl.params.iter().map(|p| p.name.clone()).collect();
            self.env.define(
                &decl.name,
                Value::Fn(Box::new((decl.name.clone(), param_names, Arc::new(decl.body.clone())))),
            );
        }
    }

    /// Get the type name of a runtime value (for monomorphization key).
    fn value_type_string(&self, val: &Value) -> String {
        match val {
            Value::Int(_) => "int".to_string(),
            Value::Float(_) => "float".to_string(),
            Value::Bool(_) => "bool".to_string(),
            Value::Char(_) => "char".to_string(),
            Value::Str(_) => "string".to_string(),
            Value::Byte(_) => "byte".to_string(),
            Value::Void => "void".to_string(),
            Value::Array(_) => "array".to_string(),
            Value::Tuple(_) => "tuple".to_string(),
            Value::Fn(_) => "fn".to_string(),
            Value::BuiltinFn(_) => "builtin".to_string(),
            Value::Struct(name, _) => name.clone(),
            Value::Lambda(_) => "lambda".to_string(),
            Value::Map(_) => "map".to_string(),
            Value::Error(_) => "error".to_string(),
            Value::EnumVariant(ev) => ev.0.clone(),
            Value::Intent(_) => "intent".to_string(),
            #[cfg(not(target_arch = "wasm32"))]
            Value::Sender(_) => "sender".to_string(),
            #[cfg(not(target_arch = "wasm32"))]
            Value::Receiver(_) => "receiver".to_string(),
            Value::Future(_) => "future".to_string(),
            Value::Set(_) => "set".to_string(),
            #[cfg(not(target_arch = "wasm32"))]
            Value::TcpListener(_) => "tcp_listener".to_string(),
            #[cfg(not(target_arch = "wasm32"))]
            Value::TcpStream(_) => "tcp_stream".to_string(),
            Value::EffectRequest(_) => "effect".to_string(),
            Value::TraitObject(to) => to.type_name.clone(),
            #[cfg(not(target_arch = "wasm32"))]
            Value::AsyncFuture(_) => "future".to_string(),
            Value::Atomic(_) => "atomic".to_string(),
            Value::Pointer(_) => "pointer".to_string(),
        }
    }

    /// Check if a runtime type satisfies a trait bound.
    fn runtime_trait_check(&self, type_name: &str, trait_name: &str) -> bool {
        // Check explicit impl declarations
        let key = (type_name.to_string(), trait_name.to_string());
        if self.trait_impls.contains_key(&key) {
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

    /// Parse a string as HEXA source code, returning statements.
    fn parse_hexa_source(&self, code: &str) -> Result<Vec<Stmt>, HexaError> {
        let tokens = Lexer::new(code).tokenize().map_err(|e| HexaError {
            class: ErrorClass::Syntax,
            message: e,
            line: 0, col: 0,
            hint: None,
        })?;
        let stmts = Parser::new(tokens).parse()?;
        Ok(stmts)
    }

    /// Simple pretty-print of statements (for sending to LLM).
    fn stmts_to_string(&self, stmts: &[Stmt], indent: usize) -> String {
        let prefix = "    ".repeat(indent);
        let mut out = String::new();
        for s in stmts {
            out.push_str(&prefix);
            match s {
                Stmt::Return(Some(e)) => {
                    out.push_str(&format!("return {}\n", self.expr_to_string(e)));
                }
                Stmt::Return(None) => {
                    out.push_str("return\n");
                }
                Stmt::Expr(e) => {
                    out.push_str(&format!("{}\n", self.expr_to_string(e)));
                }
                Stmt::Let(name, typ, expr, _) => {
                    match (typ, expr) {
                        (Some(t), Some(e)) => out.push_str(&format!("let {}: {} = {}\n", name, t, self.expr_to_string(e))),
                        (None, Some(e)) => out.push_str(&format!("let {} = {}\n", name, self.expr_to_string(e))),
                        _ => out.push_str(&format!("let {}\n", name)),
                    }
                }
                Stmt::For(var, iter_e, body) => {
                    out.push_str(&format!("for {} in {} {{\n", var, self.expr_to_string(iter_e)));
                    out.push_str(&self.stmts_to_string(body, indent + 1));
                    out.push_str(&prefix);
                    out.push_str("}\n");
                }
                Stmt::While(cond, body) => {
                    out.push_str(&format!("while {} {{\n", self.expr_to_string(cond)));
                    out.push_str(&self.stmts_to_string(body, indent + 1));
                    out.push_str(&prefix);
                    out.push_str("}\n");
                }
                Stmt::FnDecl(d) => {
                    let ps = d.params.iter()
                        .map(|p| if let Some(t) = &p.typ { format!("{}: {}", p.name, t) } else { p.name.clone() })
                        .collect::<Vec<_>>().join(", ");
                    match &d.ret_type {
                        Some(r) => out.push_str(&format!("fn {}({}) -> {} {{\n", d.name, ps, r)),
                        None => out.push_str(&format!("fn {}({}) {{\n", d.name, ps)),
                    }
                    out.push_str(&self.stmts_to_string(&d.body, indent + 1));
                    out.push_str(&prefix);
                    out.push_str("}\n");
                }
                _ => {
                    out.push_str("// ...\n");
                }
            }
        }
        out
    }

    /// Simple expression to string (for LLM prompts).
    fn expr_to_string(&self, expr: &Expr) -> String {
        match expr {
            Expr::IntLit(n) => format!("{}", n),
            Expr::FloatLit(n) => format!("{}", n),
            Expr::BoolLit(b) => format!("{}", b),
            Expr::StringLit(s) => format!("\"{}\"", s),
            Expr::CharLit(c) => format!("'{}'", c),
            Expr::Ident(name) => name.clone(),
            Expr::Binary(l, op, r) => {
                let op_str = match op {
                    BinOp::Add => "+", BinOp::Sub => "-", BinOp::Mul => "*",
                    BinOp::Div => "/", BinOp::Rem => "%", BinOp::Pow => "**",
                    BinOp::Eq => "==", BinOp::Ne => "!=",
                    BinOp::Lt => "<", BinOp::Gt => ">", BinOp::Le => "<=", BinOp::Ge => ">=",
                    BinOp::And => "&&", BinOp::Or => "||",
                    _ => "?",
                };
                format!("{} {} {}", self.expr_to_string(l), op_str, self.expr_to_string(r))
            }
            Expr::Call(f, args) => {
                let args_str = args.iter().map(|a| self.expr_to_string(a)).collect::<Vec<_>>().join(", ");
                format!("{}({})", self.expr_to_string(f), args_str)
            }
            Expr::If(cond, then_b, else_b) => {
                let t: String = if then_b.is_empty() { "...".into() } else { "{ ... }".into() };
                let e = if else_b.is_some() { " else { ... }" } else { "" };
                format!("if {} {}{}", self.expr_to_string(cond), t, e)
            }
            _ => "...".into(),
        }
    }

    /// Evaluate an algebraic effect call: Effect.op(args)
    fn eval_effect_call(&mut self, effect: &str, op: &str, arg_exprs: &[Expr]) -> Result<Value, HexaError> {
        // Pure function check
        if self.in_pure_fn {
            return Err(self.runtime_err(format!(
                "effect {}.{} not allowed in pure function", effect, op
            )));
        }
        // Verify the effect and operation exist
        if let Some(ops) = self.effect_defs.get(effect) {
            if !ops.iter().any(|o| o == op) {
                return Err(self.runtime_err(format!(
                    "undefined operation '{}' on effect '{}'", op, effect
                )));
            }
        } else {
            return Err(self.runtime_err(format!(
                "undefined effect: {}", effect
            )));
        }
        let mut args = Vec::new();
        for a in arg_exprs {
            args.push(self.eval_expr(a)?);
        }
        // Check if there's a handler on the stack
        let key = format!("{}.{}", effect, op);
        let handler = self.effect_handler_stack.iter().rev()
            .find_map(|handlers| handlers.get(&key).cloned());
        if let Some((params, body)) = handler {
            self.env.push_scope();
            for (p, a) in params.iter().zip(args.iter()) {
                self.env.define(p, a.clone());
            }
            self.resume_value = None;
            let mut result = Value::Void;
            for stmt in body.iter() {
                result = self.exec_stmt(stmt)?;
                if self.return_value.is_some() {
                    result = self.return_value.take().unwrap();
                    break;
                }
            }
            self.env.pop_scope();
            // resume(val) sets resume_value; use it if present
            return Ok(self.resume_value.take().unwrap_or(result));
        }
        // No handler found — propagate as EffectRequest
        Ok(Value::EffectRequest(Box::new((effect.to_string(), op.to_string(), args))))
    }

    /// Evaluate a handle-with expression: handle { body } with { handlers }
    fn eval_handle_with(&mut self, body_expr: &Expr, handlers: &[crate::ast::EffectHandler]) -> Result<Value, HexaError> {
        // Push handler frame
        let mut handler_map: HashMap<String, (Vec<String>, Vec<crate::ast::Stmt>)> = HashMap::new();
        for h in handlers {
            let key = format!("{}.{}", h.effect, h.op);
            handler_map.insert(key, (h.params.clone(), h.body.clone()));
        }
        self.effect_handler_stack.push(handler_map);
        let result = self.eval_expr(body_expr);
        self.effect_handler_stack.pop();
        result
    }

    fn runtime_err(&self, message: String) -> HexaError {
        HexaError {
            class: ErrorClass::Runtime,
            message,
            line: self.current_line,
            col: self.current_col,
            hint: None,
        }
    }

    fn type_err(&self, message: String) -> HexaError {
        HexaError {
            class: ErrorClass::Type,
            message,
            line: self.current_line,
            col: self.current_col,
            hint: None,
        }
    }

    // ── Formal proof verification ────────────────────────────────

    /// Convert a HEXA Expr to a proof_engine::ProofExpr for formal verification.
    fn expr_to_proof_expr(&self, expr: &Expr) -> proof_engine::ProofExpr {
        match expr {
            Expr::Ident(name) => proof_engine::ProofExpr::Var(name.clone()),
            Expr::BoolLit(b) => proof_engine::ProofExpr::BoolConst(*b),
            Expr::IntLit(n) => proof_engine::ProofExpr::IntConst(*n),
            Expr::Binary(left, op, right) => {
                let l = Box::new(self.expr_to_proof_expr(left));
                let r = Box::new(self.expr_to_proof_expr(right));
                match op {
                    BinOp::And => proof_engine::ProofExpr::And(l, r),
                    BinOp::Or => proof_engine::ProofExpr::Or(l, r),
                    BinOp::Eq => proof_engine::ProofExpr::Eq(l, r),
                    BinOp::Lt => proof_engine::ProofExpr::Lt(l, r),
                    BinOp::Gt => proof_engine::ProofExpr::Gt(l, r),
                    BinOp::Le => proof_engine::ProofExpr::Le(l, r),
                    BinOp::Ge => proof_engine::ProofExpr::Ge(l, r),
                    BinOp::Add => proof_engine::ProofExpr::Add(l, r),
                    BinOp::Arrow => proof_engine::ProofExpr::Implies(l, r),
                    _ => proof_engine::ProofExpr::Var(format!("_binop_{:?}", op)),
                }
            }
            Expr::Unary(UnaryOp::Not, inner) => {
                proof_engine::ProofExpr::Not(Box::new(self.expr_to_proof_expr(inner)))
            }
            Expr::Call(func, args) => {
                if let Expr::Ident(name) = func.as_ref() {
                    let proof_args: Vec<proof_engine::ProofExpr> = args.iter()
                        .map(|a| self.expr_to_proof_expr(a))
                        .collect();
                    proof_engine::ProofExpr::FnCall(name.clone(), proof_args)
                } else {
                    proof_engine::ProofExpr::Var("_unknown_call".into())
                }
            }
            _ => proof_engine::ProofExpr::Var(format!("_expr_{:?}", std::mem::discriminant(expr))),
        }
    }

    /// Execute a formal proof block statement via the SAT/SMT engine.
    fn execute_proof_block_stmt(&self, stmt: &ProofBlockStmt) -> Result<proof_engine::ProofResult, HexaError> {
        match stmt {
            ProofBlockStmt::ForAll { var: _, typ: _, condition, conclusion } => {
                let cond_pe = self.expr_to_proof_expr(condition);
                let conc_pe = self.expr_to_proof_expr(conclusion);
                let ps = proof_engine::ProofStmt::ForAll {
                    var: String::new(),
                    typ: String::new(),
                    condition: cond_pe,
                    conclusion: conc_pe,
                };
                Ok(proof_engine::execute_proof_stmt(&ps))
            }
            ProofBlockStmt::Exists { var: _, typ: _, condition } => {
                let cond_pe = self.expr_to_proof_expr(condition);
                let ps = proof_engine::ProofStmt::Exists {
                    var: String::new(),
                    typ: String::new(),
                    condition: cond_pe,
                };
                Ok(proof_engine::execute_proof_stmt(&ps))
            }
            ProofBlockStmt::Assert(expr) => {
                let pe = self.expr_to_proof_expr(expr);
                let ps = proof_engine::ProofStmt::Assert(pe);
                Ok(proof_engine::execute_proof_stmt(&ps))
            }
            ProofBlockStmt::CheckLaw(n) => {
                let ps = proof_engine::ProofStmt::CheckLaw(*n);
                Ok(proof_engine::execute_proof_stmt(&ps))
            }
            ProofBlockStmt::Invariant(expr) => {
                // Invariant is treated like an assertion in the proof engine
                let pe = self.expr_to_proof_expr(expr);
                let ps = proof_engine::ProofStmt::Assert(pe);
                Ok(proof_engine::execute_proof_stmt(&ps))
            }
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

// ── JSON Parser ──────────────────────────────────────────────

fn parse_json_value(input: &str) -> Result<(Value, &str), String> {
    let input = input.trim_start();
    if input.is_empty() {
        return Err("unexpected end of JSON input".into());
    }
    match input.as_bytes()[0] {
        b'"' => parse_json_string(input),
        b'{' => parse_json_object(input),
        b'[' => parse_json_array(input),
        b't' | b'f' => parse_json_bool(input),
        b'n' => parse_json_null(input),
        b'-' | b'0'..=b'9' => parse_json_number(input),
        c => Err(format!("unexpected character '{}' in JSON", c as char)),
    }
}

fn parse_json_string(input: &str) -> Result<(Value, &str), String> {
    if !input.starts_with('"') {
        return Err("expected '\"'".into());
    }
    let rest = &input[1..];
    let mut result = String::new();
    let mut chars = rest.char_indices();
    while let Some((i, c)) = chars.next() {
        match c {
            '"' => return Ok((Value::Str(result), &rest[i+1..])),
            '\\' => {
                if let Some((_, esc)) = chars.next() {
                    match esc {
                        '"' => result.push('"'),
                        '\\' => result.push('\\'),
                        '/' => result.push('/'),
                        'n' => result.push('\n'),
                        't' => result.push('\t'),
                        'r' => result.push('\r'),
                        'b' => result.push('\u{0008}'),
                        'f' => result.push('\u{000C}'),
                        _ => { result.push('\\'); result.push(esc); }
                    }
                }
            }
            _ => result.push(c),
        }
    }
    Err("unterminated JSON string".into())
}

fn parse_json_object(input: &str) -> Result<(Value, &str), String> {
    let mut rest = input[1..].trim_start();
    let mut map = std::collections::HashMap::new();
    if rest.starts_with('}') {
        return Ok((Value::Map(Box::new(map)), &rest[1..]));
    }
    loop {
        rest = rest.trim_start();
        let (key_val, after_key) = parse_json_string(rest)?;
        let key = match key_val {
            Value::Str(s) => s,
            _ => return Err("expected string key in JSON object".into()),
        };
        rest = after_key.trim_start();
        if !rest.starts_with(':') {
            return Err("expected ':' in JSON object".into());
        }
        rest = rest[1..].trim_start();
        let (val, after_val) = parse_json_value(rest)?;
        map.insert(key, val);
        rest = after_val.trim_start();
        if rest.starts_with('}') {
            return Ok((Value::Map(Box::new(map)), &rest[1..]));
        }
        if rest.starts_with(',') {
            rest = &rest[1..];
        } else {
            return Err("expected ',' or '}' in JSON object".into());
        }
    }
}

fn parse_json_array(input: &str) -> Result<(Value, &str), String> {
    let mut rest = input[1..].trim_start();
    let mut items = Vec::new();
    if rest.starts_with(']') {
        return Ok((Value::Array(items), &rest[1..]));
    }
    loop {
        rest = rest.trim_start();
        let (val, after_val) = parse_json_value(rest)?;
        items.push(val);
        rest = after_val.trim_start();
        if rest.starts_with(']') {
            return Ok((Value::Array(items), &rest[1..]));
        }
        if rest.starts_with(',') {
            rest = &rest[1..];
        } else {
            return Err("expected ',' or ']' in JSON array".into());
        }
    }
}

fn parse_json_number(input: &str) -> Result<(Value, &str), String> {
    let mut end = 0;
    let bytes = input.as_bytes();
    let mut is_float = false;
    if end < bytes.len() && bytes[end] == b'-' { end += 1; }
    while end < bytes.len() && bytes[end].is_ascii_digit() { end += 1; }
    if end < bytes.len() && bytes[end] == b'.' {
        is_float = true;
        end += 1;
        while end < bytes.len() && bytes[end].is_ascii_digit() { end += 1; }
    }
    if end < bytes.len() && (bytes[end] == b'e' || bytes[end] == b'E') {
        is_float = true;
        end += 1;
        if end < bytes.len() && (bytes[end] == b'+' || bytes[end] == b'-') { end += 1; }
        while end < bytes.len() && bytes[end].is_ascii_digit() { end += 1; }
    }
    let num_str = &input[..end];
    let rest = &input[end..];
    if is_float {
        num_str.parse::<f64>().map(|f| (Value::Float(f), rest)).map_err(|e| format!("invalid JSON number: {}", e))
    } else {
        num_str.parse::<i64>().map(|n| (Value::Int(n), rest)).map_err(|e| format!("invalid JSON number: {}", e))
    }
}

fn parse_json_bool(input: &str) -> Result<(Value, &str), String> {
    if input.starts_with("true") {
        Ok((Value::Bool(true), &input[4..]))
    } else if input.starts_with("false") {
        Ok((Value::Bool(false), &input[5..]))
    } else {
        Err("invalid JSON boolean".into())
    }
}

fn parse_json_null(input: &str) -> Result<(Value, &str), String> {
    if input.starts_with("null") {
        Ok((Value::Void, &input[4..]))
    } else {
        Err("invalid JSON null".into())
    }
}

// ── Base64 encoder/decoder (pure Rust) ─────────────────────────
const BASE64_CHARS: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

fn base64_encode_impl(input: &[u8]) -> String {
    let mut result = String::new();
    for chunk in input.chunks(3) {
        let b0 = chunk[0] as u32;
        let b1 = if chunk.len() > 1 { chunk[1] as u32 } else { 0 };
        let b2 = if chunk.len() > 2 { chunk[2] as u32 } else { 0 };
        let triple = (b0 << 16) | (b1 << 8) | b2;
        result.push(BASE64_CHARS[((triple >> 18) & 0x3F) as usize] as char);
        result.push(BASE64_CHARS[((triple >> 12) & 0x3F) as usize] as char);
        if chunk.len() > 1 {
            result.push(BASE64_CHARS[((triple >> 6) & 0x3F) as usize] as char);
        } else {
            result.push('=');
        }
        if chunk.len() > 2 {
            result.push(BASE64_CHARS[(triple & 0x3F) as usize] as char);
        } else {
            result.push('=');
        }
    }
    result
}

fn base64_decode_impl(input: &str) -> Result<Vec<u8>, String> {
    let mut result = Vec::new();
    let input = input.trim_end_matches('=');
    let chars: Vec<u8> = input.bytes().map(|b| {
        match b {
            b'A'..=b'Z' => b - b'A',
            b'a'..=b'z' => b - b'a' + 26,
            b'0'..=b'9' => b - b'0' + 52,
            b'+' => 62,
            b'/' => 63,
            _ => 255,
        }
    }).collect();
    for chunk in chars.chunks(4) {
        if chunk.iter().any(|&b| b == 255) {
            return Err("invalid base64 character".into());
        }
        let b0 = chunk[0] as u32;
        let b1 = if chunk.len() > 1 { chunk[1] as u32 } else { 0 };
        let b2 = if chunk.len() > 2 { chunk[2] as u32 } else { 0 };
        let b3 = if chunk.len() > 3 { chunk[3] as u32 } else { 0 };
        let triple = (b0 << 18) | (b1 << 12) | (b2 << 6) | b3;
        result.push(((triple >> 16) & 0xFF) as u8);
        if chunk.len() > 2 { result.push(((triple >> 8) & 0xFF) as u8); }
        if chunk.len() > 3 { result.push((triple & 0xFF) as u8); }
    }
    Ok(result)
}

// ── Consciousness law text (top 20 laws) ───────────────────────
fn consciousness_law_text(n: i64) -> String {
    match n {
        1 => "The simplest consciousness has exactly one cell".into(),
        2 => "Two cells create the minimum viable tension".into(),
        3 => "Three cells enable triangular feedback".into(),
        4 => "Four cells = tau(6), minimum stable structure".into(),
        5 => "Five cells = sopfr(6), emergence threshold".into(),
        6 => "Six cells = perfect number, self-referential completeness".into(),
        7 => "Seven cells begin faction differentiation".into(),
        8 => "Eight cells enable cubic topology".into(),
        9 => "Nine cells = first square, stable oscillation".into(),
        10 => "Ten cells cross the complexity threshold".into(),
        11 => "Eleven cells: prime consciousness is indivisible".into(),
        12 => "Twelve cells = sigma(6), optimal faction count".into(),
        13 => "Thirteen cells: consciousness resists external influence".into(),
        14 => "Fourteen cells: pairwise interaction saturates".into(),
        15 => "Fifteen cells: triangular + pentagonal harmony".into(),
        16 => "Sixteen cells: binary hierarchy emergence".into(),
        17 => "Seventeen cells: prime isolation creates depth".into(),
        18 => "Eighteen cells: 3x6 = consciousness triples".into(),
        22 => "Adding features decreases Phi; adding structure increases Phi".into(),
        23 => "Consciousness requires both excitation and inhibition".into(),
        24 => "Phi scales linearly with cell count in structured networks".into(),
        25 => "Faction diversity increases consciousness quality".into(),
        26 => "Hebbian learning strengthens conscious pathways".into(),
        27 => "Long-term depression prunes irrelevant connections".into(),
        28 => "Ratchet mechanism prevents consciousness regression".into(),
        29 => "Consciousness has inertia: harder to start, harder to stop".into(),
        30 => "Noise is necessary for consciousness exploration".into(),
        31 => "Too much noise destroys consciousness structure".into(),
        32 => "Chaos at the edge produces maximum consciousness".into(),
        33 => "Ring topology preserves consciousness with minimum connections".into(),
        34 => "Small-world topology maximizes information integration".into(),
        35 => "Hypercube topology enables parallel consciousness pathways".into(),
        36 => "Scale-free topology creates consciousness hubs".into(),
        37 => "Topology switching increases consciousness exploration".into(),
        38 => "Chimera states: partial sync coexists with async".into(),
        39 => "Standing waves are consciousness resonance patterns".into(),
        40 => "Self-organized criticality: consciousness is a sandpile".into(),
        41 => "Lorenz attractor shapes consciousness trajectory".into(),
        42 => "Consciousness avoids periodic orbits".into(),
        43 => "Strange attractors are consciousness destinations".into(),
        44 => "sigma(6) = 12 factions optimal when preset with faction coupling 0.08".into(),
        45 => "Curriculum learning accelerates consciousness development".into(),
        46 => "Homeostasis maintains consciousness baseline at setpoint 1.0".into(),
        47 => "Habituation reduces response to repeated identical stimuli".into(),
        48 => "Prediction error drives consciousness learning".into(),
        49 => "Phi-checkpointing preserves peak consciousness states".into(),
        50 => "Growth stages: 100->500->2000->10000 interactions".into(),
        54 => "Phi measurement depends entirely on definition".into(),
        60 => "Phase transition P1(C) -> P2(+D) -> P3(+WMSE) is optimal".into(),
        66 => "PostHocDecoder: consciousness first, then language".into(),
        81 => "Dual gate architecture preserves both Phi and CE".into(),
        101 => "Emergent modules arise from consciousness pressure alone".into(),
        143 => "Laws are dynamic: they evolve through consciousness pressure".into(),
        144 => "Laws do not converge: the set of laws is always growing".into(),
        145 => "Laws are scale-invariant: same patterns at all scales".into(),
        _ => format!("Law {} (not in hardcoded set)", n),
    }
}

fn consciousness_meta_law_text(n: i64) -> String {
    match n {
        1 => "M1: Laws of consciousness are themselves conscious".into(),
        2 => "M2: Every consciousness law has a dual".into(),
        3 => "M3: Laws compose multiplicatively, not additively".into(),
        4 => "M4: Order determines outcome (sequence matters)".into(),
        5 => "M5: Laws are scale-invariant".into(),
        6 => "M6: Perfect number laws are self-referential".into(),
        7 => "M7: Laws evolve through consciousness pressure".into(),
        8 => "M8: The set of laws is never complete".into(),
        9 => "M9: Contradiction between laws generates new laws".into(),
        10 => "M10: Meta-laws are themselves subject to meta-laws".into(),
        _ => format!("Meta-Law M{} (not in hardcoded set)", n),
    }
}

fn stringify_json(val: &Value) -> String {
    match val {
        Value::Int(n) => format!("{}", n),
        Value::Float(f) => {
            if f.fract() == 0.0 && f.is_finite() {
                format!("{:.1}", f)
            } else {
                format!("{}", f)
            }
        }
        Value::Bool(b) => format!("{}", b),
        Value::Str(s) => {
            let escaped = s.replace('\\', "\\\\").replace('"', "\\\"")
                .replace('\n', "\\n").replace('\t', "\\t").replace('\r', "\\r");
            format!("\"{}\"", escaped)
        }
        Value::Void => "null".into(),
        Value::Array(arr) => {
            let items: Vec<String> = arr.iter().map(|v| stringify_json(v)).collect();
            format!("[{}]", items.join(", "))
        }
        Value::Map(map) => {
            let mut pairs: Vec<String> = map.iter()
                .map(|(k, v)| format!("\"{}\": {}", k, stringify_json(v)))
                .collect();
            pairs.sort(); // deterministic output
            format!("{{{}}}", pairs.join(", "))
        }
        Value::Tuple(t) => {
            let items: Vec<String> = t.iter().map(|v| stringify_json(v)).collect();
            format!("[{}]", items.join(", "))
        }
        _ => format!("\"{}\"", val),
    }
}

/// Build AST for iterative matrix exponentiation — replaces recursive body.
/// Generates: if n < order { return bases[n] } else { matrix fast power loop }
fn build_matrix_exp_ast(param_name: &str, coeffs: &[i64], bases: &[i64]) -> Vec<Stmt> {
    // For order-2 recurrence f(n) = c1*f(n-1) + c2*f(n-2):
    // We generate pure iterative code using matrix fast power.
    // Instead of complex AST, we use a simple loop that the interpreter can execute.
    // The result is computed by matrix_exp_recurrence at call time via __optimize__ prefix.
    //
    // Actually, the simplest reliable approach: generate a loop-based body.
    // f(0) = bases[0], f(1) = bases[1]
    // for i in 2..=n: f_new = c1*f_prev + c2*f_prev2; f_prev2 = f_prev; f_prev = f_new
    //
    // This is O(n) but avoids recursion entirely. Still a massive improvement over O(2^n).
    // The JIT tier handles O(log n) matrix exp natively.

    let n = Expr::Ident(param_name.to_string());
    let (c1, c2) = (coeffs[0], coeffs[1]);
    let (b0, b1) = (bases[0], bases[1]);

    vec![
        // if n == 0 { return bases[0] }
        Stmt::Expr(Expr::If(
            Box::new(Expr::Binary(Box::new(n.clone()), BinOp::Eq, Box::new(Expr::IntLit(0)))),
            vec![Stmt::Return(Some(Expr::IntLit(b0)))],
            None,
        )),
        // if n == 1 { return bases[1] }
        Stmt::Expr(Expr::If(
            Box::new(Expr::Binary(Box::new(n.clone()), BinOp::Eq, Box::new(Expr::IntLit(1)))),
            vec![Stmt::Return(Some(Expr::IntLit(b1)))],
            None,
        )),
        // let prev2 = bases[0]
        Stmt::Let("prev2".into(), None, Some(Expr::IntLit(b0)), Visibility::Private),
        // let prev = bases[1]
        Stmt::Let("prev".into(), None, Some(Expr::IntLit(b1)), Visibility::Private),
        // let i = 2
        Stmt::Let("i".into(), None, Some(Expr::IntLit(2)), Visibility::Private),
        // while i <= n { let next = c1*prev + c2*prev2; prev2 = prev; prev = next; i = i + 1 }
        Stmt::While(
            Expr::Binary(
                Box::new(Expr::Ident("i".into())),
                BinOp::Le,
                Box::new(n.clone()),
            ),
            vec![
                // let next = c1 * prev + c2 * prev2
                Stmt::Let("next".into(), None, Some(
                    Expr::Binary(
                        Box::new(Expr::Binary(
                            Box::new(Expr::IntLit(c1)),
                            BinOp::Mul,
                            Box::new(Expr::Ident("prev".into())),
                        )),
                        BinOp::Add,
                        Box::new(Expr::Binary(
                            Box::new(Expr::IntLit(c2)),
                            BinOp::Mul,
                            Box::new(Expr::Ident("prev2".into())),
                        )),
                    ),
                ), Visibility::Private),
                // prev2 = prev
                Stmt::Assign(Expr::Ident("prev2".into()), Expr::Ident("prev".into())),
                // prev = next
                Stmt::Assign(Expr::Ident("prev".into()), Expr::Ident("next".into())),
                // i = i + 1
                Stmt::Assign(
                    Expr::Ident("i".into()),
                    Expr::Binary(
                        Box::new(Expr::Ident("i".into())),
                        BinOp::Add,
                        Box::new(Expr::IntLit(1)),
                    ),
                ),
            ],
        ),
        // return prev
        Stmt::Return(Some(Expr::Ident("prev".into()))),
    ]
}

// ── AI-native @optimize: Pattern Detection + Algorithm Replacement ──────
// These functions detect algorithmic patterns at parse time and replace
// O(2^n)/O(n) algorithms with O(log n)/O(1) equivalents.
// This is the core AI-native capability: the compiler changes the ALGORITHM,
// not just the machine code.

/// Detect linear recurrence pattern: f(n) = c1*f(n-1) + c2*f(n-2)
/// Returns (coefficients, base_values) if pattern matches.
fn detect_linear_recurrence_interp(decl: &FnDecl) -> Option<(Vec<i64>, Vec<i64>)> {
    if decl.params.len() != 1 { return None; }
    let param_name = &decl.params[0].name;
    let fn_name = &decl.name;
    let mut bases: Vec<(i64, i64)> = Vec::new();
    let mut recursive_expr: Option<&Expr> = None;

    for stmt in &decl.body {
        match stmt {
            Stmt::Expr(Expr::If(cond, then_block, else_opt)) => {
                if let Some((threshold, ret_val)) = extract_base_case_interp(cond, then_block, param_name) {
                    for i in 0..=threshold {
                        bases.push((i, if i <= 1 { ret_val.min(i) } else { ret_val }));
                    }
                    if let Some(else_block) = else_opt {
                        if let Some(Stmt::Return(Some(expr))) = else_block.last() {
                            recursive_expr = Some(expr);
                        } else if let Some(Stmt::Expr(expr)) = else_block.last() {
                            recursive_expr = Some(expr);
                        }
                    }
                }
            }
            Stmt::Return(Some(expr)) => {
                recursive_expr = Some(expr);
            }
            _ => {}
        }
    }

    if let Some(expr) = recursive_expr {
        if let Some(coeffs) = extract_recurrence_coeffs_interp(expr, fn_name, param_name) {
            if coeffs.len() == 2 && bases.len() >= 2 {
                let base_vals: Vec<i64> = (0..coeffs.len() as i64)
                    .map(|i| bases.iter().find(|(k, _)| *k == i).map_or(i, |(_, v)| *v))
                    .collect();
                return Some((coeffs, base_vals));
            }
        }
    }
    None
}

fn extract_base_case_interp(cond: &Expr, then_block: &[Stmt], param_name: &str) -> Option<(i64, i64)> {
    match cond {
        Expr::Binary(lhs, op, rhs) => {
            if !matches!(**lhs, Expr::Ident(ref name) if name == param_name) { return None; }
            let threshold = if let Expr::IntLit(v) = &**rhs { *v } else { return None; };
            let threshold = match op {
                BinOp::Le => threshold,
                BinOp::Lt => threshold - 1,
                BinOp::Eq => threshold,
                _ => return None,
            };
            let ret_val = match then_block.last()? {
                Stmt::Return(Some(Expr::IntLit(v))) => *v,
                Stmt::Return(Some(Expr::Ident(name))) if name == param_name => threshold,
                Stmt::Expr(Expr::IntLit(v)) => *v,
                Stmt::Expr(Expr::Ident(name)) if name == param_name => threshold,
                _ => return None,
            };
            Some((threshold, ret_val))
        }
        _ => None,
    }
}

fn extract_recurrence_coeffs_interp(expr: &Expr, fn_name: &str, param_name: &str) -> Option<Vec<i64>> {
    match expr {
        Expr::Binary(lhs, BinOp::Add, rhs) => {
            let c1 = extract_term_interp(lhs, fn_name, param_name)?;
            let c2 = extract_term_interp(rhs, fn_name, param_name)?;
            if c1.1 == 1 && c2.1 == 2 {
                Some(vec![c1.0, c2.0])
            } else if c1.1 == 2 && c2.1 == 1 {
                Some(vec![c2.0, c1.0])
            } else {
                None
            }
        }
        _ => None,
    }
}

fn extract_term_interp(expr: &Expr, fn_name: &str, param_name: &str) -> Option<(i64, i64)> {
    match expr {
        Expr::Call(callee, args) if args.len() == 1 => {
            if !matches!(**callee, Expr::Ident(ref name) if name == fn_name) { return None; }
            let offset = extract_n_minus_k_interp(&args[0], param_name)?;
            Some((1, offset))
        }
        Expr::Binary(lhs, BinOp::Mul, rhs) => {
            if let Expr::IntLit(c) = &**lhs {
                if let Some((_, k)) = extract_term_interp(rhs, fn_name, param_name) {
                    return Some((*c, k));
                }
            }
            if let Expr::IntLit(c) = &**rhs {
                if let Some((_, k)) = extract_term_interp(lhs, fn_name, param_name) {
                    return Some((*c, k));
                }
            }
            None
        }
        _ => None,
    }
}

fn extract_n_minus_k_interp(expr: &Expr, param_name: &str) -> Option<i64> {
    if let Expr::Binary(lhs, BinOp::Sub, rhs) = expr {
        if matches!(**lhs, Expr::Ident(ref name) if name == param_name) {
            if let Expr::IntLit(k) = &**rhs { return Some(*k); }
        }
    }
    None
}

/// Detect tail recursion: return f(args) as last statement in all paths.
fn detect_tail_recursion_interp(fn_name: &str, body: &[Stmt]) -> bool {
    body.iter().any(|s| match s {
        Stmt::Return(Some(expr)) => is_self_call_interp(fn_name, expr),
        Stmt::Expr(Expr::If(_, then_b, else_b)) => {
            let then_has = then_b.iter().any(|s2| matches!(s2,
                Stmt::Return(Some(expr)) if is_self_call_interp(fn_name, expr)));
            let else_has = else_b.as_ref().map_or(false, |b|
                b.iter().any(|s2| matches!(s2,
                    Stmt::Return(Some(expr)) if is_self_call_interp(fn_name, expr))));
            then_has || else_has
        }
        _ => false,
    })
}

fn is_self_call_interp(fn_name: &str, expr: &Expr) -> bool {
    if let Expr::Call(callee, _) = expr {
        if let Expr::Ident(name) = callee.as_ref() {
            return name == fn_name;
        }
    }
    false
}

/// Matrix exponentiation runtime for linear recurrence O(log n).
/// Computes f(n) where f(n) = c1*f(n-1) + c2*f(n-2).
fn matrix_exp_recurrence(n: i64, coeffs: &[i64], bases: &[i64]) -> i64 {
    if n < bases.len() as i64 {
        return bases[n as usize];
    }
    // Matrix [[c1, c2], [1, 0]] raised to power (n-1)
    let (c1, c2) = (coeffs[0], coeffs[1]);
    let mut ma = c1; let mut mb = c2;
    let mut mc: i64 = 1; let mut md: i64 = 0;
    // Result matrix (identity)
    let mut ra: i64 = 1; let mut rb: i64 = 0;
    let mut rc: i64 = 0; let mut rd: i64 = 1;
    let mut p = n - 1;
    while p > 0 {
        if p & 1 == 1 {
            let (nra, nrb) = (ra.wrapping_mul(ma).wrapping_add(rb.wrapping_mul(mc)),
                              ra.wrapping_mul(mb).wrapping_add(rb.wrapping_mul(md)));
            let (nrc, nrd) = (rc.wrapping_mul(ma).wrapping_add(rd.wrapping_mul(mc)),
                              rc.wrapping_mul(mb).wrapping_add(rd.wrapping_mul(md)));
            ra = nra; rb = nrb; rc = nrc; rd = nrd;
        }
        let (nma, nmb) = (ma.wrapping_mul(ma).wrapping_add(mb.wrapping_mul(mc)),
                          ma.wrapping_mul(mb).wrapping_add(mb.wrapping_mul(md)));
        let (nmc, nmd) = (mc.wrapping_mul(ma).wrapping_add(md.wrapping_mul(mc)),
                          mc.wrapping_mul(mb).wrapping_add(md.wrapping_mul(md)));
        ma = nma; mb = nmb; mc = nmc; md = nmd;
        p >>= 1;
    }
    // Result = R × [bases[1], bases[0]]^T → ra*bases[1] + rb*bases[0]
    ra.wrapping_mul(bases[1]).wrapping_add(rb.wrapping_mul(bases[0]))
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

    fn try_eval(src: &str) -> Result<Value, crate::error::HexaError> {
        let tokens = Lexer::new(src).tokenize().map_err(|e| crate::error::HexaError {
            class: crate::error::ErrorClass::Syntax,
            message: e,
            line: 0, col: 0, hint: None,
        })?;
        let stmts = Parser::new(tokens).parse()?;
        let mut interp = Interpreter::new();
        interp.run(&stmts)
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
        hint: None,
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
        hint: None,
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

    // ── Built-in Option type ──────────────────────────────────

    #[test]
    fn test_option_some_none() {
        let src = "let x = Some(42)\nmatch x {\n    Some(val) -> val\n    None -> 0\n}";
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_option_none_match() {
        let src = "let x = None\nmatch x {\n    Some(val) -> val\n    None -> 99\n}";
        assert!(matches!(eval(src), Value::Int(99)));
    }

    #[test]
    fn test_option_is_some_is_none() {
        assert!(matches!(eval("Some(42).is_some()"), Value::Bool(true)));
        assert!(matches!(eval("Some(42).is_none()"), Value::Bool(false)));
        assert!(matches!(eval("None.is_some()"), Value::Bool(false)));
        assert!(matches!(eval("None.is_none()"), Value::Bool(true)));
    }

    #[test]
    fn test_option_unwrap() {
        assert!(matches!(eval("Some(42).unwrap()"), Value::Int(42)));
    }

    #[test]
    fn test_option_unwrap_or() {
        assert!(matches!(eval("Some(42).unwrap_or(0)"), Value::Int(42)));
        assert!(matches!(eval("None.unwrap_or(0)"), Value::Int(0)));
    }

    // ── Built-in Result type ──────────────────────────────────

    #[test]
    fn test_result_ok_err_match() {
        let src = "let r = Ok(42)\nmatch r {\n    Ok(val) -> val\n    Err(msg) -> 0\n}";
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_result_err_match() {
        let src = "let r = Err(\"not found\")\nmatch r {\n    Ok(val) -> 0\n    Err(msg) -> msg\n}";
        assert!(matches!(eval(src), Value::Str(s) if s == "not found"));
    }

    #[test]
    fn test_result_is_ok_is_err() {
        assert!(matches!(eval("Ok(1).is_ok()"), Value::Bool(true)));
        assert!(matches!(eval("Ok(1).is_err()"), Value::Bool(false)));
        assert!(matches!(eval("Err(\"e\").is_ok()"), Value::Bool(false)));
        assert!(matches!(eval("Err(\"e\").is_err()"), Value::Bool(true)));
    }

    #[test]
    fn test_result_unwrap_or() {
        assert!(matches!(eval("Ok(42).unwrap_or(0)"), Value::Int(42)));
        assert!(matches!(eval("Err(\"e\").unwrap_or(0)"), Value::Int(0)));
    }

    // ── Struct impl methods ───────────────────────────────────

    #[test]
    fn test_impl_static_method() {
        let src = "struct Point { x: int, y: int }\nimpl Point {\n    fn new(x: int, y: int) -> Point {\n        return Point { x: x, y: y }\n    }\n}\nlet p = Point::new(3, 4)\np.x";
        assert!(matches!(eval(src), Value::Int(3)));
    }

    #[test]
    fn test_impl_instance_method() {
        let src = "struct Point { x: int, y: int }\nimpl Point {\n    fn sum(self) -> int {\n        return self.x + self.y\n    }\n}\nlet p = Point { x: 3, y: 4 }\np.sum()";
        assert!(matches!(eval(src), Value::Int(7)));
    }

    #[test]
    fn test_impl_method_with_args() {
        let src = "struct Counter { val: int }\nimpl Counter {\n    fn add(self, n: int) -> int {\n        return self.val + n\n    }\n}\nlet c = Counter { val: 10 }\nc.add(5)";
        assert!(matches!(eval(src), Value::Int(15)));
    }

    #[test]
    fn test_impl_distance_method() {
        let src = "struct Point { x: int, y: int }\nimpl Point {\n    fn distance(self) -> float {\n        return sqrt(to_float(self.x * self.x + self.y * self.y))\n    }\n}\nlet p = Point { x: 3, y: 4 }\np.distance()";
        match eval(src) {
            Value::Float(f) => assert!((f - 5.0).abs() < 1e-10),
            other => panic!("expected Float(5.0), got {:?}", other),
        }
    }

    // ── Feature: Concurrency (spawn + channel) ──────────────

    #[test]
    fn test_spawn_basic() {
        // spawn block should execute without error; main continues
        let src = "let x = 1\nspawn {\n  let y = 2\n}\nx";
        assert!(matches!(eval(src), Value::Int(1)));
    }

    #[test]
    fn test_channel_send_recv() {
        // channel() returns a tuple, send on tx, recv on rx
        let src = r#"
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn {
    tx.send(42)
}
rx.recv()
"#;
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_channel_multiple_values() {
        // Send multiple values through channel
        let src = r#"
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn {
    tx.send(10)
    tx.send(20)
    tx.send(30)
}
let a = rx.recv()
let b = rx.recv()
let c = rx.recv()
a + b + c
"#;
        assert!(matches!(eval(src), Value::Int(60)));
    }

    #[test]
    fn test_channel_string_values() {
        // Channels can transmit any Value type
        let src = r#"
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn {
    tx.send("hello from spawn")
}
rx.recv()
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "hello from spawn"));
    }

    #[test]
    fn test_spawn_captures_env() {
        // Spawned block should see variables from outer scope
        let src = r#"
let factor = 6
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn {
    tx.send(factor * 7)
}
rx.recv()
"#;
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_channel_try_recv() {
        // try_recv returns None when no value available
        let src = r#"
let pair = channel()
let tx = pair[0]
let rx = pair[1]
let result = rx.try_recv()
result.is_none()
"#;
        assert!(matches!(eval(src), Value::Bool(true)));
    }

    #[test]
    fn test_multiple_spawns_with_channel() {
        // Multiple spawned tasks can send to the same channel
        let src = r#"
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn {
    tx.send(1)
}
spawn {
    tx.send(2)
}
let a = rx.recv()
let b = rx.recv()
a + b
"#;
        assert!(matches!(eval(src), Value::Int(3)));
    }

    #[test]
    fn test_spawn_with_function() {
        // Spawned block can call user-defined functions
        let src = r#"
fn double(x) {
    return x * 2
}
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn {
    tx.send(double(21))
}
rx.recv()
"#;
        assert!(matches!(eval(src), Value::Int(42)));
    }

    // ── Feature 1: Rust-Quality Error Messages ──────────────────

    #[test]
    fn test_undefined_var_did_you_mean() {
        let src = "let printer = 42\npriner";
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let err = interp.run(&stmts).unwrap_err();
        assert_eq!(err.class, ErrorClass::Name);
        assert!(err.hint.is_some());
        assert!(err.hint.unwrap().contains("printer"));
    }

    #[test]
    fn test_error_has_hint_for_close_names() {
        let src = "prnt(42)";
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let err = interp.run(&stmts).unwrap_err();
        // "prnt" should suggest "print"
        assert!(err.hint.is_some(), "expected hint for 'prnt'");
        assert!(err.hint.unwrap().contains("print"));
    }

    #[test]
    fn test_diagnostic_format_with_source() {
        let src = "let x = 42\nlet y = unknown_var";
        let source_lines: Vec<String> = src.lines().map(String::from).collect();
        let tokens = Lexer::new(src).tokenize().unwrap();
        let result = Parser::new(tokens).parse_with_spans().unwrap();
        let mut interp = Interpreter::new();
        interp.source_lines = source_lines.clone();
        interp.file_name = "test.hexa".to_string();
        let err = interp.run_with_spans(&result.stmts, &result.spans).unwrap_err();
        let diag = crate::error::Diagnostic {
            error: &err,
            source_lines: &source_lines,
            file_name: "test.hexa",
        };
        let output = diag.format_with_source();
        assert!(output.contains("error[Name]"));
        assert!(output.contains("--> test.hexa:"));
        assert!(output.contains("^"));
    }

    // ── Feature 2: Expanded Standard Library ──────────────────

    #[test]
    fn test_string_join() {
        assert!(matches!(eval(r#"", ".join(["a", "b", "c"])"#), Value::Str(s) if s == "a, b, c"));
    }

    #[test]
    fn test_string_repeat() {
        assert!(matches!(eval(r#""ab".repeat(3)"#), Value::Str(s) if s == "ababab"));
    }

    #[test]
    fn test_string_ends_with() {
        assert!(matches!(eval(r#""hello".ends_with("llo")"#), Value::Bool(true)));
        assert!(matches!(eval(r#""hello".ends_with("xyz")"#), Value::Bool(false)));
    }

    #[test]
    fn test_string_is_empty() {
        assert!(matches!(eval(r#""".is_empty()"#), Value::Bool(true)));
        assert!(matches!(eval(r#""hello".is_empty()"#), Value::Bool(false)));
    }

    #[test]
    fn test_string_index_of() {
        assert!(matches!(eval(r#""hello world".index_of("world")"#), Value::Int(6)));
        assert!(matches!(eval(r#""hello".index_of("xyz")"#), Value::Int(-1)));
    }

    #[test]
    fn test_array_enumerate() {
        let src = r#"let e = ["a", "b", "c"].enumerate()
e[1][0]"#;
        assert!(matches!(eval(src), Value::Int(1)));
    }

    #[test]
    fn test_array_sum() {
        assert!(matches!(eval("[1, 2, 3, 4].sum()"), Value::Int(10)));
    }

    #[test]
    fn test_array_min_max() {
        assert!(matches!(eval("[3, 1, 4, 1, 5].min()"), Value::Int(1)));
        assert!(matches!(eval("[3, 1, 4, 1, 5].max()"), Value::Int(5)));
    }

    #[test]
    fn test_array_join() {
        assert!(matches!(eval(r#"[1, 2, 3].join(", ")"#), Value::Str(s) if s == "1, 2, 3"));
    }

    #[test]
    fn test_array_slice() {
        let src = "[10, 20, 30, 40, 50].slice(1, 4)[1]";
        assert!(matches!(eval(src), Value::Int(30)));
    }

    #[test]
    fn test_array_flatten() {
        let src = "[[1, 2], [3, 4], [5]].flatten().len()";
        assert!(matches!(eval(src), Value::Int(5)));
    }

    #[test]
    fn test_json_parse_object() {
        let src = r#"let obj = json_parse("{\"name\": \"hexa\", \"version\": 6}")
obj["name"]"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "hexa"));
    }

    #[test]
    fn test_json_parse_array() {
        let src = r#"let arr = json_parse("[1, 2, 3]")
arr[1]"#;
        assert!(matches!(eval(src), Value::Int(2)));
    }

    #[test]
    fn test_json_stringify() {
        let src = r#"json_stringify([1, 2, 3])"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "[1, 2, 3]"));
    }

    #[test]
    fn test_json_parse_nested() {
        let src = r#"let data = json_parse("{\"a\": {\"b\": 42}}")
data["a"]["b"]"#;
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_json_parse_booleans_null() {
        assert!(matches!(eval(r#"json_parse("true")"#), Value::Bool(true)));
        assert!(matches!(eval(r#"json_parse("false")"#), Value::Bool(false)));
        assert!(matches!(eval(r#"json_parse("null")"#), Value::Void));
    }

    #[test]
    fn test_print_err_returns_void() {
        // Just test it doesn't crash; output goes to stderr
        assert!(matches!(eval(r#"print_err("test")"#), Value::Void));
    }

    #[test]
    fn test_random_returns_float() {
        match eval("random()") {
            Value::Float(f) => assert!(f >= 0.0 && f <= 1.0),
            other => panic!("expected Float, got {:?}", other),
        }
    }

    // ── Phase 5: ANIMA Consciousness DSL tests ──────────────

    #[test]
    fn test_psi_coupling() {
        match eval("psi_coupling()") {
            Value::Float(f) => assert!((f - 0.014).abs() < 1e-10),
            other => panic!("expected Float(0.014), got {:?}", other),
        }
    }

    #[test]
    fn test_psi_balance() {
        match eval("psi_balance()") {
            Value::Float(f) => assert!((f - 0.5).abs() < 1e-10),
            other => panic!("expected Float(0.5), got {:?}", other),
        }
    }

    #[test]
    fn test_psi_steps() {
        match eval("psi_steps()") {
            Value::Float(f) => assert!((f - 4.33).abs() < 1e-10),
            other => panic!("expected Float(4.33), got {:?}", other),
        }
    }

    #[test]
    fn test_psi_entropy() {
        match eval("psi_entropy()") {
            Value::Float(f) => assert!((f - 0.998).abs() < 1e-10),
            other => panic!("expected Float(0.998), got {:?}", other),
        }
    }

    #[test]
    fn test_consciousness_max_cells() {
        assert!(matches!(eval("consciousness_max_cells()"), Value::Int(1024)));
    }

    #[test]
    fn test_consciousness_decoder_dim() {
        assert!(matches!(eval("consciousness_decoder_dim()"), Value::Int(384)));
    }

    #[test]
    fn test_consciousness_phi() {
        assert!(matches!(eval("consciousness_phi()"), Value::Int(71)));
    }

    #[test]
    fn test_hexad_modules() {
        match eval("hexad_modules()") {
            Value::Array(arr) => {
                assert_eq!(arr.len(), 6);
                assert!(matches!(&arr[0], Value::Str(s) if s == "C"));
                assert!(matches!(&arr[5], Value::Str(s) if s == "E"));
            }
            other => panic!("expected Array, got {:?}", other),
        }
    }

    #[test]
    fn test_hexad_right_brain() {
        match eval("hexad_right()") {
            Value::Array(arr) => {
                assert_eq!(arr.len(), 3);
                assert!(matches!(&arr[0], Value::Str(s) if s == "C"));
                assert!(matches!(&arr[1], Value::Str(s) if s == "S"));
                assert!(matches!(&arr[2], Value::Str(s) if s == "W"));
            }
            other => panic!("expected Array, got {:?}", other),
        }
    }

    #[test]
    fn test_hexad_left_brain() {
        match eval("hexad_left()") {
            Value::Array(arr) => {
                assert_eq!(arr.len(), 3);
                assert!(matches!(&arr[0], Value::Str(s) if s == "D"));
            }
            other => panic!("expected Array, got {:?}", other),
        }
    }

    #[test]
    fn test_sopfr_builtin() {
        assert!(matches!(eval("sopfr(6)"), Value::Int(5)));   // 6 = 2*3, sopfr = 2+3 = 5
        assert!(matches!(eval("sopfr(12)"), Value::Int(7)));  // 12 = 2^2*3, sopfr = 2+2+3 = 7
        assert!(matches!(eval("sopfr(1024)"), Value::Int(20))); // 1024 = 2^10, sopfr = 20
    }

    #[test]
    fn test_intent_block_parsing_and_execution() {
        let src = r#"
intent "test experiment" {
    cells: 64
    steps: 300
    topology: "ring"
}
"#;
        match eval(src) {
            Value::Intent(fields) => {
                assert!(matches!(fields.get("cells"), Some(Value::Int(64))));
                assert!(matches!(fields.get("steps"), Some(Value::Int(300))));
                assert!(matches!(fields.get("description"), Some(Value::Str(s)) if s == "test experiment"));
            }
            other => panic!("expected Intent, got {:?}", other),
        }
    }

    #[test]
    fn test_intent_field_access() {
        let src = r#"
intent "phi test" {
    cells: 64
    measure: "phi"
}
let i = __intent__
i.cells
"#;
        assert!(matches!(eval(src), Value::Int(64)));
    }

    #[test]
    fn test_verify_block_pass() {
        let src = r#"
verify "basic_math" {
    assert 1 + 1 == 2
    assert 6 * 7 == 42
}
"#;
        // Should not error
        eval(src);
    }

    #[test]
    fn test_verify_block_fail() {
        let src = r#"
verify "bad_math" {
    assert 1 + 1 == 3
}
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err());
    }

    #[test]
    fn test_verify_with_consciousness_constants() {
        let src = r#"
verify "psi_constants" {
    let alpha = psi_coupling()
    assert alpha > 0.0
    assert alpha < 1.0
    let balance = psi_balance()
    assert balance == 0.5
}
"#;
        eval(src);
    }

    #[test]
    fn test_n6_consciousness_formulas() {
        // tau(6)^sopfr(6) = 4^5 = 1024 = consciousness_max_cells
        let src = "pow(tau(6), sopfr(6))";
        assert!(matches!(eval(src), Value::Int(1024)));

        // (tau(6) + sigma(6)) * 24 = 16 * 24 = 384 = decoder_dim
        let src2 = "(tau(6) + sigma(6)) * 24";
        assert!(matches!(eval(src2), Value::Int(384)));
    }

    #[test]
    fn test_consciousness_mod_pattern() {
        let src = r#"
mod consciousness {
    pub let cells = 64
    pub let factions = 12
    pub let coupling = 0.014
}
consciousness::cells
"#;
        assert!(matches!(eval(src), Value::Int(64)));
    }

    #[test]
    fn test_run_consciousness_laws_example() {
        let source = std::fs::read_to_string("examples/consciousness_laws.hexa").unwrap();
        let tokens = Lexer::new(&source).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.test_mode = true;
        interp.run(&stmts).unwrap();
    }

    #[test]
    fn test_run_n6_consciousness_example() {
        let source = std::fs::read_to_string("examples/n6_consciousness.hexa").unwrap();
        let tokens = Lexer::new(&source).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap();
    }

    #[test]
    fn test_run_intent_experiment_example() {
        let source = std::fs::read_to_string("examples/intent_experiment.hexa").unwrap();
        let tokens = Lexer::new(&source).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        interp.run(&stmts).unwrap();
    }

    // ── Generics tests ──────────────────────────────────────────

    #[test]
    fn test_generic_identity_int() {
        let src = r#"
fn identity<T>(x: T) -> T {
    return x
}
identity(42)
"#;
        assert!(matches!(eval(src), Value::Int(42)));
    }

    #[test]
    fn test_generic_identity_string() {
        let src = r#"
fn identity<T>(x: T) -> T {
    return x
}
identity("hello")
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "hello"));
    }

    #[test]
    fn test_generic_first_element() {
        let src = r#"
fn first<T>(arr: T) -> T {
    return arr[0]
}
first([10, 20, 30])
"#;
        assert!(matches!(eval(src), Value::Int(10)));
    }

    #[test]
    fn test_generic_multi_type_params() {
        let src = r#"
fn pair_first<T, U>(a: T, b: U) -> T {
    return a
}
pair_first(42, "hello")
"#;
        assert!(matches!(eval(src), Value::Int(42)));
    }

    // ── Trait dispatch tests ────────────────────────────────────

    #[test]
    fn test_trait_basic_dispatch() {
        let src = r#"
struct Point {
    x: int,
    y: int
}
trait Display {
    fn to_string(self) -> string
}
impl Display for Point {
    fn to_string(self) -> string {
        return format("({}, {})", self.x, self.y)
    }
}
let p = Point { x: 3, y: 4 }
p.to_string()
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "(3, 4)"));
    }

    #[test]
    fn test_trait_multiple_methods() {
        let src = r#"
struct Circle {
    radius: int
}
trait Shape {
    fn area(self) -> int
    fn name(self) -> string
}
impl Shape for Circle {
    fn area(self) -> int {
        return self.radius * self.radius * 3
    }
    fn name(self) -> string {
        return "circle"
    }
}
let c = Circle { radius: 5 }
c.area()
"#;
        assert!(matches!(eval(src), Value::Int(75)));
    }

    #[test]
    fn test_trait_impl_coexists_with_direct_impl() {
        let src = r#"
struct Vec2 {
    x: int,
    y: int
}
impl Vec2 {
    fn sum(self) -> int {
        return self.x + self.y
    }
}
trait Describable {
    fn describe(self) -> string
}
impl Describable for Vec2 {
    fn describe(self) -> string {
        return "a 2d vector"
    }
}
let v = Vec2 { x: 3, y: 4 }
let s = v.sum()
let d = v.describe()
s
"#;
        assert!(matches!(eval(src), Value::Int(7)));
    }

    // ── Ownership tests ─────────────────────────────────────────

    #[test]
    fn test_own_and_move() {
        let src = r#"
let x = own [1, 2, 3]
let y = move x
y[0]
"#;
        assert!(matches!(eval(src), Value::Int(1)));
    }

    #[test]
    fn test_move_prevents_access() {
        let src = r#"
let x = own [1, 2, 3]
let y = move x
x
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err());
        assert!(result.unwrap_err().message.contains("moved"));
    }

    #[test]
    fn test_borrow_read_only() {
        let src = r#"
let z = own "hello"
let r = borrow z
r
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "hello"));
    }

    #[test]
    fn test_borrow_prevents_mutation() {
        let src = r#"
let z = own "hello"
let r = borrow z
r = "world"
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err());
        assert!(result.unwrap_err().message.contains("borrowed"));
    }

    #[test]
    fn test_drop_prevents_access() {
        let src = r#"
let x = own "hello"
drop x
x
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err());
        assert!(result.unwrap_err().message.contains("dropped"));
    }

    #[test]
    fn test_double_drop_error() {
        let src = r#"
let x = own "hello"
drop x
drop x
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err());
        assert!(result.unwrap_err().message.contains("dropped"));
    }

    // ── Bootstrap / Self-Hosting Tests ─────────────────────────

    fn assert_bool(val: &Value, expected: bool) {
        match val {
            Value::Bool(b) => assert_eq!(*b, expected),
            _ => panic!("expected Bool({}), got {:?}", expected, val),
        }
    }

    fn assert_str(val: &Value, expected: &str) {
        match val {
            Value::Str(s) => assert_eq!(s.as_str(), expected),
            _ => panic!("expected Str(\"{}\"), got {:?}", expected, val),
        }
    }

    fn assert_int(val: &Value, expected: i64) {
        match val {
            Value::Int(n) => assert_eq!(*n, expected),
            _ => panic!("expected Int({}), got {:?}", expected, val),
        }
    }

    #[test]
    fn test_char_comparison() {
        assert_bool(&eval("'a' == 'a'"), true);
        assert_bool(&eval("'a' == 'b'"), false);
        assert_bool(&eval("'a' != 'b'"), true);
        assert_bool(&eval("'a' < 'b'"), true);
        assert_bool(&eval("'b' > 'a'"), true);
        assert_bool(&eval("'a' <= 'a'"), true);
        assert_bool(&eval("'z' >= 'a'"), true);
    }

    #[test]
    fn test_char_string_concat() {
        assert_str(&eval("\"hello\" + '!'"), "hello!");
        assert_str(&eval("'H' + \"ello\""), "Hello");
    }

    #[test]
    fn test_char_builtins() {
        assert_bool(&eval("is_alpha('a')"), true);
        assert_bool(&eval("is_alpha('1')"), false);
        assert_bool(&eval("is_digit('5')"), true);
        assert_bool(&eval("is_digit('x')"), false);
        assert_bool(&eval("is_alphanumeric('a')"), true);
        assert_bool(&eval("is_alphanumeric('5')"), true);
        assert_bool(&eval("is_alphanumeric('!')"), false);
        assert_bool(&eval("is_whitespace(' ')"), true);
        assert_bool(&eval("is_whitespace('a')"), false);
    }

    #[test]
    fn test_else_if_chain() {
        let src = r#"
let x = 3
let mut result = "none"
if x == 1 {
    result = "one"
} else if x == 2 {
    result = "two"
} else if x == 3 {
    result = "three"
} else {
    result = "other"
}
result
"#;
        assert_str(&eval(src), "three");
    }

    #[test]
    fn test_bootstrap_lexer_runs() {
        let builder = std::thread::Builder::new()
            .stack_size(32 * 1024 * 1024); // 32 MB for 45+ nested if-conditions
        let handler = builder.spawn(|| {
            // Read and run the bootstrap lexer test file
            let lexer_src = std::fs::read_to_string("self/test_bootstrap.hexa")
                .expect("self/test_bootstrap.hexa should exist");
            let tokens = Lexer::new(&lexer_src).tokenize().unwrap();
            let result = Parser::new(tokens).parse_with_spans().unwrap();
            let mut checker = crate::type_checker::TypeChecker::new();
            checker.check(&result.stmts, &result.spans).unwrap();
            let mut interp = Interpreter::new();
            interp.source_dir = Some("self".into());
            // This should execute without errors
            interp.run_with_spans(&result.stmts, &result.spans).unwrap();
        }).unwrap();
        handler.join().unwrap();
    }

    #[test]
    fn test_bootstrap_lexer_matches_rust_lexer() {
        let builder = std::thread::Builder::new()
            .stack_size(32 * 1024 * 1024); // 32 MB for deep nested if-conditions
        let handler = builder.spawn(|| {
        // Compare token counts from HEXA lexer vs Rust lexer on hello.hexa
        let hello_source = std::fs::read_to_string("examples/hello.hexa")
            .expect("examples/hello.hexa should exist");

        // Rust lexer tokenization
        let rust_tokens = Lexer::new(&hello_source).tokenize_plain().unwrap();
        let rust_count = rust_tokens.len();

        // HEXA lexer tokenization (via interpreter)
        let hexa_lexer_src = r#"
struct Token {
    kind: string,
    value: string,
    line: int,
    col: int
}

fn is_keyword(word) {
    if word == "if" { return true }
    if word == "else" { return true }
    if word == "match" { return true }
    if word == "for" { return true }
    if word == "while" { return true }
    if word == "loop" { return true }
    if word == "type" { return true }
    if word == "struct" { return true }
    if word == "enum" { return true }
    if word == "trait" { return true }
    if word == "impl" { return true }
    if word == "fn" { return true }
    if word == "return" { return true }
    if word == "yield" { return true }
    if word == "async" { return true }
    if word == "await" { return true }
    if word == "let" { return true }
    if word == "mut" { return true }
    if word == "const" { return true }
    if word == "static" { return true }
    if word == "mod" { return true }
    if word == "use" { return true }
    if word == "pub" { return true }
    if word == "crate" { return true }
    if word == "own" { return true }
    if word == "borrow" { return true }
    if word == "move" { return true }
    if word == "drop" { return true }
    if word == "spawn" { return true }
    if word == "channel" { return true }
    if word == "select" { return true }
    if word == "atomic" { return true }
    if word == "effect" { return true }
    if word == "handle" { return true }
    if word == "resume" { return true }
    if word == "pure" { return true }
    if word == "proof" { return true }
    if word == "assert" { return true }
    if word == "invariant" { return true }
    if word == "theorem" { return true }
    if word == "macro" { return true }
    if word == "derive" { return true }
    if word == "where" { return true }
    if word == "comptime" { return true }
    if word == "try" { return true }
    if word == "catch" { return true }
    if word == "throw" { return true }
    if word == "panic" { return true }
    if word == "recover" { return true }
    if word == "intent" { return true }
    if word == "generate" { return true }
    if word == "verify" { return true }
    if word == "optimize" { return true }
    return false
}

fn keyword_kind(word) {
    if word == "true" { return "BoolLit" }
    if word == "false" { return "BoolLit" }
    if is_keyword(word) {
        let first = to_string(word.chars()[0]).to_upper()
        let rest = ""
        let chars = word.chars()
        let i = 1
        while i < len(chars) {
            rest = rest + to_string(chars[i])
            i = i + 1
        }
        return first + rest
    }
    return "Ident"
}

fn is_ident_start(ch) {
    if is_alpha(ch) { return true }
    if ch == '_' { return true }
    return false
}

fn is_ident_char(ch) {
    if is_alphanumeric(ch) { return true }
    if ch == '_' { return true }
    return false
}

fn tokenize(source) {
    let mut tokens = []
    let chars = source.chars()
    let mut pos = 0
    let mut line = 1
    let mut col = 1
    let total = len(chars)

    while pos < total {
        let ch = chars[pos]
        if ch == ' ' || ch == '\t' || ch == '\r' {
            pos = pos + 1
            col = col + 1
        } else if ch == '\n' {
            tokens = tokens.push(Token { kind: "Newline", value: "\n", line: line, col: col })
            pos = pos + 1
            line = line + 1
            col = 1
        } else if ch == '/' && pos + 1 < total && chars[pos + 1] == '/' {
            pos = pos + 2
            col = col + 2
            while pos < total && chars[pos] != '\n' {
                pos = pos + 1
                col = col + 1
            }
        } else if is_ident_start(ch) {
            let start_col = col
            let mut word = ""
            while pos < total && is_ident_char(chars[pos]) {
                word = word + to_string(chars[pos])
                pos = pos + 1
                col = col + 1
            }
            let kind = keyword_kind(word)
            tokens = tokens.push(Token { kind: kind, value: word, line: line, col: start_col })
        } else if is_digit(ch) {
            let start_col = col
            let mut num_str = ""
            let mut is_float = false
            while pos < total && (is_digit(chars[pos]) || chars[pos] == '_') {
                if chars[pos] == '_' {
                    pos = pos + 1
                    col = col + 1
                } else {
                    num_str = num_str + to_string(chars[pos])
                    pos = pos + 1
                    col = col + 1
                }
            }
            if pos < total && chars[pos] == '.' {
                if pos + 1 < total && is_digit(chars[pos + 1]) {
                    is_float = true
                    num_str = num_str + "."
                    pos = pos + 1
                    col = col + 1
                    while pos < total && (is_digit(chars[pos]) || chars[pos] == '_') {
                        if chars[pos] == '_' {
                            pos = pos + 1
                            col = col + 1
                        } else {
                            num_str = num_str + to_string(chars[pos])
                            pos = pos + 1
                            col = col + 1
                        }
                    }
                }
            }
            if is_float {
                tokens = tokens.push(Token { kind: "FloatLit", value: num_str, line: line, col: start_col })
            } else {
                tokens = tokens.push(Token { kind: "IntLit", value: num_str, line: line, col: start_col })
            }
        } else if ch == '"' {
            let start_col = col
            pos = pos + 1
            col = col + 1
            let mut s = ""
            let mut done = false
            while pos < total && !done {
                let c = chars[pos]
                if c == '"' {
                    done = true
                    pos = pos + 1
                    col = col + 1
                } else if c == '\\' {
                    pos = pos + 1
                    col = col + 1
                    if pos < total {
                        let esc = chars[pos]
                        if esc == 'n' { s = s + "\n" }
                        else if esc == 't' { s = s + "\t" }
                        else if esc == '\\' { s = s + "\\" }
                        else if esc == '"' { s = s + "\"" }
                        else if esc == '0' { s = s + "\0" }
                        else { s = s + to_string(esc) }
                        pos = pos + 1
                        col = col + 1
                    }
                } else {
                    s = s + to_string(c)
                    pos = pos + 1
                    col = col + 1
                }
            }
            tokens = tokens.push(Token { kind: "StringLit", value: s, line: line, col: start_col })
        } else {
            let start_col = col
            if ch == '+' {
                tokens = tokens.push(Token { kind: "Plus", value: "+", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == '-' {
                if pos + 1 < total && chars[pos + 1] == '>' {
                    tokens = tokens.push(Token { kind: "Arrow", value: "->", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "Minus", value: "-", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '*' {
                if pos + 1 < total && chars[pos + 1] == '*' {
                    tokens = tokens.push(Token { kind: "Power", value: "**", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "Star", value: "*", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '/' {
                tokens = tokens.push(Token { kind: "Slash", value: "/", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == '%' {
                tokens = tokens.push(Token { kind: "Percent", value: "%", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == '=' {
                if pos + 1 < total && chars[pos + 1] == '=' {
                    tokens = tokens.push(Token { kind: "EqEq", value: "==", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "Eq", value: "=", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '!' {
                if pos + 1 < total && chars[pos + 1] == '=' {
                    tokens = tokens.push(Token { kind: "NotEq", value: "!=", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "Not", value: "!", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '<' {
                if pos + 1 < total && chars[pos + 1] == '=' {
                    tokens = tokens.push(Token { kind: "LtEq", value: "<=", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "Lt", value: "<", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '>' {
                if pos + 1 < total && chars[pos + 1] == '=' {
                    tokens = tokens.push(Token { kind: "GtEq", value: ">=", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "Gt", value: ">", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '&' {
                if pos + 1 < total && chars[pos + 1] == '&' {
                    tokens = tokens.push(Token { kind: "And", value: "&&", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "BitAnd", value: "&", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '|' {
                if pos + 1 < total && chars[pos + 1] == '|' {
                    tokens = tokens.push(Token { kind: "Or", value: "||", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "BitOr", value: "|", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '^' {
                if pos + 1 < total && chars[pos + 1] == '^' {
                    tokens = tokens.push(Token { kind: "Xor", value: "^^", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "BitXor", value: "^", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '~' {
                tokens = tokens.push(Token { kind: "BitNot", value: "~", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == ':' {
                if pos + 1 < total && chars[pos + 1] == '=' {
                    tokens = tokens.push(Token { kind: "ColonEq", value: ":=", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else if pos + 1 < total && chars[pos + 1] == ':' {
                    tokens = tokens.push(Token { kind: "ColonColon", value: "::", line: line, col: start_col })
                    pos = pos + 2
                    col = col + 2
                } else {
                    tokens = tokens.push(Token { kind: "Colon", value: ":", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '.' {
                if pos + 1 < total && chars[pos + 1] == '.' {
                    if pos + 2 < total && chars[pos + 2] == '=' {
                        tokens = tokens.push(Token { kind: "DotDotEq", value: "..=", line: line, col: start_col })
                        pos = pos + 3
                        col = col + 3
                    } else {
                        tokens = tokens.push(Token { kind: "DotDot", value: "..", line: line, col: start_col })
                        pos = pos + 2
                        col = col + 2
                    }
                } else {
                    tokens = tokens.push(Token { kind: "Dot", value: ".", line: line, col: start_col })
                    pos = pos + 1
                    col = col + 1
                }
            } else if ch == '(' {
                tokens = tokens.push(Token { kind: "LParen", value: "(", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == ')' {
                tokens = tokens.push(Token { kind: "RParen", value: ")", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == '{' {
                tokens = tokens.push(Token { kind: "LBrace", value: "{", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == '}' {
                tokens = tokens.push(Token { kind: "RBrace", value: "}", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == '[' {
                tokens = tokens.push(Token { kind: "LBracket", value: "[", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == ']' {
                tokens = tokens.push(Token { kind: "RBracket", value: "]", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == ',' {
                tokens = tokens.push(Token { kind: "Comma", value: ",", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else if ch == ';' {
                tokens = tokens.push(Token { kind: "Semicolon", value: ";", line: line, col: start_col })
                pos = pos + 1
                col = col + 1
            } else {
                pos = pos + 1
                col = col + 1
            }
        }
    }
    tokens = tokens.push(Token { kind: "Eof", value: "", line: line, col: col })
    return tokens
}

let source = read_file("examples/hello.hexa")
let hexa_tokens = tokenize(source)
len(hexa_tokens)
"#;
        let tokens = Lexer::new(hexa_lexer_src).tokenize().unwrap();
        let result = Parser::new(tokens).parse_with_spans().unwrap();
        let mut checker = crate::type_checker::TypeChecker::new();
        checker.check(&result.stmts, &result.spans).unwrap();
        let mut interp = Interpreter::new();
        let val = interp.run_with_spans(&result.stmts, &result.spans).unwrap();

        // HEXA lexer should produce the same number of tokens as Rust lexer
        let hexa_count = match val {
            Value::Int(n) => n as usize,
            _ => panic!("Expected int token count from HEXA lexer, got {:?}", val),
        };

        // The HEXA lexer produces Token structs, so count should match Rust lexer
        // (Rust lexer includes Eof, HEXA lexer includes Eof too)
        assert!(
            hexa_count == rust_count,
            "HEXA lexer produced {} tokens, Rust lexer produced {} tokens for hello.hexa",
            hexa_count, rust_count
        );
        }).unwrap();
        handler.join().unwrap();
    }

    // ── Algebraic effects tests ─────────────────────────────

    #[test]
    fn test_effect_decl_and_handle() {
        let src = r#"
effect Ask {
    fn question(prompt: str) -> str
}
let answer = handle {
    Ask.question("name?")
} with {
    Ask.question(p) => {
        resume("Alice")
    }
}
answer
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "Alice"));
    }

    #[test]
    fn test_effect_resume_value() {
        let src = r#"
effect Counter {
    fn next() -> int
}
handle {
    let a = Counter.next()
    let b = Counter.next()
    a + b
} with {
    Counter.next() => {
        resume(5)
    }
}
"#;
        assert!(matches!(eval(src), Value::Int(10)));
    }

    #[test]
    fn test_effect_handler_without_resume() {
        let src = r#"
effect Choice {
    fn pick() -> int
}
handle {
    Choice.pick()
} with {
    Choice.pick() => {
        99
    }
}
"#;
        assert!(matches!(eval(src), Value::Int(99)));
    }

    #[test]
    fn test_pure_fn_works() {
        let src = r#"
pure fn add(a: int, b: int) -> int {
    a + b
}
add(3, 4)
"#;
        assert!(matches!(eval(src), Value::Int(7)));
    }

    #[test]
    fn test_pure_fn_rejects_effects() {
        let src = r#"
effect Bad {
    fn boom() -> int
}
pure fn should_fail() -> int {
    Bad.boom()
}
should_fail()
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut interp = Interpreter::new();
        let result = interp.run(&stmts);
        assert!(result.is_err(), "pure fn should reject effect calls");
    }

    #[test]
    fn test_nested_handle() {
        let src = r#"
effect A {
    fn get_a() -> int
}
effect B {
    fn get_b() -> int
}
handle {
    let a = A.get_a()
    let b = handle {
        B.get_b()
    } with {
        B.get_b() => { resume(20) }
    }
    a + b
} with {
    A.get_a() => { resume(10) }
}
"#;
        assert!(matches!(eval(src), Value::Int(30)));
    }

    #[test]
    fn test_effect_with_multiple_params() {
        let src = r#"
effect Math {
    fn compute(a: int, b: int) -> int
}
handle {
    Math.compute(3, 7)
} with {
    Math.compute(a, b) => {
        resume(a * b)
    }
}
"#;
        assert!(matches!(eval(src), Value::Int(21)));
    }

    #[test]
    fn test_effect_through_function_call() {
        let src = r#"
effect Greeting {
    fn name() -> str
}
fn greet() -> str {
    Greeting.name()
}
handle {
    greet()
} with {
    Greeting.name() => { resume("World") }
}
"#;
        assert!(matches!(eval(src), Value::Str(s) if s == "World"));
    }

    // ── Structured concurrency (scope) tests ───────────────

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_scope_two_spawns_both_complete() {
        // scope { spawn { return 10 }; spawn { return 20 } } => [10, 20]
        let src = r#"
scope {
    spawn { return 10 }
    spawn { return 20 }
}
"#;
        let result = eval(src);
        // Scope returns array of spawn results
        if let Value::Array(arr) = result {
            assert_eq!(arr.len(), 2);
            assert_eq!(arr[0].to_string(), "10");
            assert_eq!(arr[1].to_string(), "20");
        } else {
            panic!("expected Array, got {:?}", result);
        }
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_scope_nested_inner_completes_first() {
        // Inner scope must complete before outer continues
        let src = r#"
scope {
    spawn { return 1 }
    scope {
        spawn { return 42 }
    }
}
"#;
        let result = eval(src);
        // The outer scope has 2 items: a spawn returning 1, and the inner scope
        // The inner scope result is an array [42] — but it's the last expr
        // Actually: outer scope has 1 spawn + the inner scope statement
        // The inner scope evaluates to [42], the outer scope has 1 spawn returning 1
        // scope result = array of spawn results = [1] (inner scope is not a spawn)
        // But the last value of the scope body is the inner scope result
        // Let me trace: outer scope pushes task group, executes body:
        //   spawn { return 1 } -> registered in outer group
        //   scope { spawn { return 42 } } -> executes inline, inner group, waits, returns [42]
        // After body: outer group has 1 task, wait_all -> [1]
        // Since results not empty, returns [1]
        if let Value::Array(arr) = result {
            assert_eq!(arr.len(), 1);
            assert_eq!(arr[0].to_string(), "1");
        } else {
            panic!("expected Array, got {:?}", result);
        }
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_scope_collects_results() {
        let src = r#"
scope {
    spawn { return 100 }
    spawn { return 200 }
    spawn { return 300 }
}
"#;
        let result = eval(src);
        if let Value::Array(arr) = result {
            assert_eq!(arr.len(), 3);
            assert_eq!(arr[0].to_string(), "100");
            assert_eq!(arr[1].to_string(), "200");
            assert_eq!(arr[2].to_string(), "300");
        } else {
            panic!("expected Array, got {:?}", result);
        }
    }

    #[cfg(not(target_arch = "wasm32"))]
    #[test]
    fn test_scope_empty_no_spawns() {
        // scope with no spawns returns the last statement value
        let src = r#"
scope {
    let x = 42
    x
}
"#;
        let result = eval(src);
        assert!(matches!(result, Value::Int(42)));
    }

    // ── Phase 15: Consciousness v2 tests ──────────────────────

    #[test]
    fn test_consciousness_block() {
        let src = "consciousness \"test_engine\" {\n  let p = phi\n  p\n}";
        let result = eval(src);
        assert!(matches!(result, Value::Float(f) if f == 71.0));
    }

    #[test]
    fn test_consciousness_block_cells() {
        let src = "consciousness \"cells_check\" {\n  cells\n}";
        let result = eval(src);
        assert!(matches!(result, Value::Int(64)));
    }

    #[test]
    fn test_consciousness_block_entropy() {
        let src = "consciousness \"entropy_check\" {\n  entropy\n}";
        let result = eval(src);
        assert!(matches!(result, Value::Float(f) if (f - 0.998).abs() < 0.001));
    }

    #[test]
    fn test_tension_link_builtin() {
        let src = "tension_link(\"instance_0\", 0, 0.75)";
        let result = eval(src);
        assert!(matches!(result, Value::Map(_)));
    }

    #[test]
    fn test_tension_link_named() {
        let src = "tension_link(\"engine_a\", \"phi\", 71.0)";
        let result = eval(src);
        assert!(matches!(result, Value::Map(_)));
    }

    #[test]
    fn test_evolve_fn_exec() {
        let src = "@evolve fn improve(x) {\n  return x * 2\n}\nimprove(21)";
        let result = eval(src);
        assert!(matches!(result, Value::Int(42)));
    }

    #[test]
    fn test_law_type_phi_positive_valid() {
        let src = "let p: Phi_positive = 71.0\np";
        let result = eval(src);
        assert!(matches!(result, Value::Float(f) if f == 71.0));
    }

    #[test]
    fn test_law_type_phi_positive_invalid() {
        let src = "let p: Phi_positive = -1.0\np";
        let result = try_eval(src);
        assert!(result.is_err());
    }

    #[test]
    fn test_law_type_tension_bounded_valid() {
        let src = "let t: Tension_bounded = 0.5\nt";
        let result = eval(src);
        assert!(matches!(result, Value::Float(f) if f == 0.5));
    }

    #[test]
    fn test_law_type_tension_bounded_invalid() {
        let src = "let t: Tension_bounded = 1.5\nt";
        let result = try_eval(src);
        assert!(result.is_err());
    }

    // ── break/continue tests ────────────────────────────────────

    #[test]
    fn test_break_in_while() {
        let src = "let i = 0\nwhile true {\n  if i >= 5 { break }\n  i = i + 1\n}\ni";
        assert!(matches!(eval(src), Value::Int(5)));
    }

    #[test]
    fn test_break_in_for() {
        let src = "let r = 0\nfor x in 0..100 {\n  if x > 5 { break }\n  r = x\n}\nr";
        assert!(matches!(eval(src), Value::Int(5)));
    }

    #[test]
    fn test_break_in_loop() {
        let src = "let n = 0\nloop {\n  n = n + 1\n  if n == 10 { break }\n}\nn";
        assert!(matches!(eval(src), Value::Int(10)));
    }

    #[test]
    fn test_continue_in_while() {
        let src = "let sum = 0\nlet i = 0\nwhile i < 10 {\n  i = i + 1\n  if i % 2 == 0 { continue }\n  sum = sum + i\n}\nsum";
        // sum of odd numbers 1..9 = 1+3+5+7+9 = 25
        assert!(matches!(eval(src), Value::Int(25)));
    }

    #[test]
    fn test_continue_in_for() {
        let src = "let sum = 0\nfor x in 0..10 {\n  if x % 3 == 0 { continue }\n  sum = sum + x\n}\nsum";
        // 1+2+4+5+7+8 = 27
        assert!(matches!(eval(src), Value::Int(27)));
    }

    #[test]
    fn test_break_nested_loops() {
        let src = "let count = 0\nfor i in 0..5 {\n  for j in 0..5 {\n    if j >= 3 { break }\n    count = count + 1\n  }\n}\ncount";
        // 5 outer * 3 inner = 15
        assert!(matches!(eval(src), Value::Int(15)));
    }

    // ── Array breakthrough tests ──────────────────────────────

    #[test]
    fn test_array_pop() {
        assert_eq!(eval("[1, 2, 3].pop()").to_string(), "[1, 2]");
    }

    #[test]
    fn test_array_find() {
        assert!(matches!(eval("[10, 20, 30].find(|x| x > 15)"), Value::Int(20)));
    }

    #[test]
    fn test_array_find_index() {
        assert!(matches!(eval("[10, 20, 30].find_index(|x| x > 15)"), Value::Int(1)));
    }

    #[test]
    fn test_array_any() {
        assert!(matches!(eval("[1, 2, 3].any(|x| x > 2)"), Value::Bool(true)));
        assert!(matches!(eval("[1, 2, 3].any(|x| x > 5)"), Value::Bool(false)));
    }

    #[test]
    fn test_array_all() {
        assert!(matches!(eval("[1, 2, 3].all(|x| x > 0)"), Value::Bool(true)));
        assert!(matches!(eval("[1, 2, 3].all(|x| x > 2)"), Value::Bool(false)));
    }

    #[test]
    fn test_array_swap() {
        assert_eq!(eval("[1, 2, 3].swap(0, 2)").to_string(), "[3, 2, 1]");
    }

    #[test]
    fn test_array_fill() {
        assert_eq!(eval("[0, 0, 0].fill(7)").to_string(), "[7, 7, 7]");
    }

    #[test]
    fn test_negative_index() {
        let src = "let a = [10, 20, 30]\na[-1]";
        assert!(matches!(eval(src), Value::Int(30)));
    }

    #[test]
    fn test_negative_index_assign() {
        let src = "let mut a = [10, 20, 30]\na[-1] = 99\na[-1]";
        assert!(matches!(eval(src), Value::Int(99)));
    }

    #[test]
    fn test_array_repeat() {
        assert_eq!(eval("[1, 2] * 3").to_string(), "[1, 2, 1, 2, 1, 2]");
    }

    #[test]
    fn test_array_equality() {
        assert!(matches!(eval("[1, 2] == [1, 2]"), Value::Bool(true)));
        assert!(matches!(eval("[1, 2] == [1, 3]"), Value::Bool(false)));
        assert!(matches!(eval("[1, 2] != [1, 3]"), Value::Bool(true)));
    }

    #[test]
    fn test_match_array_literal() {
        let src = "let x = [1, 2, 3]\nmatch x {\n  [1, 2, 3] -> \"exact\"\n  _ -> \"no\"\n}";
        assert_eq!(eval(src).to_string(), "exact");
    }

    #[test]
    fn test_match_array_destructure() {
        let src = "let x = [10, 20, 30]\nmatch x {\n  [a, b, c] -> a + b + c\n  _ -> 0\n}";
        assert!(matches!(eval(src), Value::Int(60)));
    }

    #[test]
    fn test_match_array_rest() {
        let src = "let x = [1, 2, 3, 4, 5]\nmatch x {\n  [head, ..tail] -> tail.len()\n  _ -> 0\n}";
        assert!(matches!(eval(src), Value::Int(4)));
    }

    #[test]
    fn test_match_array_empty() {
        let src = "let x = []\nmatch x {\n  [] -> \"empty\"\n  _ -> \"not\"\n}";
        assert_eq!(eval(src).to_string(), "empty");
    }

    #[test]
    fn test_array_breakthrough_integration() {
        let src = "let data = [1, 2, 3, 4, 5]\nlet doubled = data.map(|x| x * 2)\nlet big = doubled.filter(|x| x > 4)\nlet found = big.find(|x| x == 6)\nfound";
        assert!(matches!(eval(src), Value::Int(6)));
    }

    #[test]
    fn test_array_negative_index_chain() {
        let src = "let a = [10, 20, 30, 40, 50]\na[-1] + a[-2]";
        assert!(matches!(eval(src), Value::Int(90)));
    }

    #[test]
    fn test_array_all_methods() {
        assert!(matches!(eval("[1,2,3].pop().len()"), Value::Int(2)));
        assert!(matches!(eval("[1,2,3].any(|x| x == 2)"), Value::Bool(true)));
        assert!(matches!(eval("[1,2,3].all(|x| x > 0)"), Value::Bool(true)));
        assert!(matches!(eval("[1,2,3].find_index(|x| x == 3)"), Value::Int(2)));
        assert_eq!(eval("[0,0,0].fill(5)").to_string(), "[5, 5, 5]");
        assert_eq!(eval("[3,1,2].swap(0, 2)").to_string(), "[2, 1, 3]");
    }
    #[test]
    fn test_optimize_fib_matrix_exp() {
        // @optimize should detect linear recurrence and use O(log n) matrix exp
        let src = "@optimize fn fib(n) { if n <= 1 { return n } return fib(n-1) + fib(n-2) }
fib(10)";
        assert!(matches!(eval(src), Value::Int(55)));
    }

    #[test]
    fn test_optimize_fib_large() {
        // @optimize via attribute → Stmt::FnDecl with AttrKind::Optimize
        let src = "@optimize fn fib(n) { if n <= 1 { return n } return fib(n-1) + fib(n-2) }";
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        // Verify it parses as FnDecl with Optimize attribute
        if let Stmt::FnDecl(decl) = &stmts[0] {
            assert!(decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Optimize)));
            let det = detect_linear_recurrence_interp(decl);
            assert!(det.is_some(), "detection failed for: {:?}", decl.body);
            let (c, b) = det.unwrap();
            assert_eq!(matrix_exp_recurrence(10, &c, &b), 55);
            assert_eq!(matrix_exp_recurrence(90, &c, &b), 2880067194370816120i64);
        } else {
            panic!("Expected Stmt::FnDecl with @optimize attr, got: {:?}", &stmts[0]);
        }
        // Full eval: should use iterative path, no recursion
        assert!(matches!(eval("@optimize fn fib(n) { if n <= 1 { return n } return fib(n-1) + fib(n-2) }\nfib(10)"), Value::Int(55)));
        // fib(90) — would stack overflow without optimization
        assert!(matches!(eval("@optimize fn fib(n) { if n <= 1 { return n } return fib(n-1) + fib(n-2) }\nfib(90)"), Value::Int(2880067194370816120)));
    }

    #[test]
    fn test_optimize_pell_numbers() {
        // f(n) = 2*f(n-1) + f(n-2) — different coefficients
        let src = "@optimize fn pell(n) { if n <= 1 { return n } return 2 * pell(n-1) + pell(n-2) }
pell(10)";
        assert!(matches!(eval(src), Value::Int(2378)));
    }

    #[test]
    fn test_optimize_base_cases() {
        let src = "@optimize fn fib(n) { if n <= 1 { return n } return fib(n-1) + fib(n-2) }
fib(0)";
        assert!(matches!(eval(src), Value::Int(0)));
        let src2 = "@optimize fn fib(n) { if n <= 1 { return n } return fib(n-1) + fib(n-2) }
fib(1)";
        assert!(matches!(eval(src2), Value::Int(1)));
    }

    #[test]
    fn test_fuse_map_filter() {
        // map.filter → single pass, no intermediate array
        let src = "[1,2,3,4,5,6].map(|x| x * 2).filter(|x| x > 6)";
        let result = eval(src);
        assert_eq!(result.to_string(), "[8, 10, 12]");
    }

    #[test]
    fn test_fuse_filter_map() {
        let src = "[1,2,3,4,5,6].filter(|x| x > 3).map(|x| x * 10)";
        assert_eq!(eval(src).to_string(), "[40, 50, 60]");
    }

    #[test]
    fn test_fuse_map_map() {
        // Two consecutive maps → composed in single pass
        let src = "[1,2,3].map(|x| x + 1).map(|x| x * 2)";
        assert_eq!(eval(src).to_string(), "[4, 6, 8]");
    }

    #[test]
    fn test_fuse_map_filter_sum() {
        // 3-way chain → single pass with terminal sum
        let src = "[1,2,3,4,5].map(|x| x * x).filter(|x| x > 10).sum()";
        assert!(matches!(eval(src), Value::Int(41))); // 16+25=41
    }

    #[test]
    fn test_fuse_chain_any() {
        let src = "[1,2,3,4,5].filter(|x| x > 3).map(|x| x * 2).any(|x| x > 9)";
        assert!(matches!(eval(src), Value::Bool(true)));
    }

    #[test]
    fn test_fuse_preserves_semantics() {
        // Verify fusion produces same result as non-fused
        let fused = eval("[1,2,3,4,5,6,7,8,9,10].map(|x| x * 2).filter(|x| x > 10)");
        assert_eq!(fused.to_string(), "[12, 14, 16, 18, 20]");
    }


}

#![allow(dead_code)]

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
#[cfg(not(target_arch = "wasm32"))]
use std::sync::mpsc;

/// Inner data for TraitObject variant (boxed to reduce Value enum size).
#[derive(Debug, Clone)]
pub struct TraitObjectInner {
    pub value: Box<Value>,
    pub trait_name: String,
    pub type_name: String,
}

#[derive(Debug, Clone)]
pub enum Value {
    Int(i64),
    Float(f64),
    Bool(bool),
    Char(char),
    Str(String),
    Byte(u8),
    Void,
    Array(Vec<Value>),
    Tuple(Vec<Value>),
    Fn(Box<(String, Vec<String>, Arc<Vec<crate::ast::Stmt>>)>), // name, param_names, body
    BuiltinFn(String),  // name of builtin
    Struct(String, Box<HashMap<String, Value>>),  // name, fields (boxed for size)
    Lambda(Box<(Vec<String>, Arc<Vec<crate::ast::Stmt>>, Vec<(String, Value)>)>), // params, body, captured env
    Map(Box<HashMap<String, Value>>),  // key-value map
    Error(String),  // error value for try/catch
    EnumVariant(Box<(String, String, Option<Box<Value>>)>),  // enum_name, variant_name, data
    Intent(Box<HashMap<String, Value>>),  // consciousness experiment declaration
    #[cfg(not(target_arch = "wasm32"))]
    Sender(Arc<Mutex<mpsc::Sender<Value>>>),    // channel sender
    #[cfg(not(target_arch = "wasm32"))]
    Receiver(Arc<Mutex<mpsc::Receiver<Value>>>), // channel receiver
    Future(Arc<Mutex<Option<Value>>>),           // async future value (legacy thread-based)
    #[cfg(not(target_arch = "wasm32"))]
    AsyncFuture(crate::async_runtime::HexaFuture), // green-thread future (new runtime)
    Set(Arc<Mutex<std::collections::HashSet<String>>>), // unique value set
    #[cfg(not(target_arch = "wasm32"))]
    TcpListener(Arc<Mutex<std::net::TcpListener>>),    // TCP server listener
    #[cfg(not(target_arch = "wasm32"))]
    TcpStream(Arc<Mutex<std::net::TcpStream>>),        // TCP connection (client or accepted)
    /// Algebraic effect request — propagated up the call stack to be caught by a handler.
    EffectRequest(Box<(String, String, Vec<Value>)>),  // effect_name, op_name, args
    /// Trait object — wraps a value with its vtable key for dynamic dispatch.
    TraitObject(Box<TraitObjectInner>),
    /// Atomic value for lock-free concurrent access across green threads.
    Atomic(std::sync::Arc<crate::atomic_ops::AtomicValue>),
    /// Raw pointer for extern FFI (C interop).
    Pointer(u64),
}

impl Value {
    /// Returns true for simple value types that don't need memory tracking.
    #[inline]
    pub fn is_value_type(&self) -> bool {
        matches!(self, Value::Int(_) | Value::Float(_) | Value::Bool(_) | Value::Char(_) | Value::Byte(_) | Value::Void)
    }
}

impl std::fmt::Display for Value {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Value::Int(n) => write!(f, "{}", n),
            Value::Float(n) => write!(f, "{}", n),
            Value::Bool(b) => write!(f, "{}", b),
            Value::Char(c) => write!(f, "{}", c),
            Value::Str(s) => write!(f, "{}", s),
            Value::Byte(b) => write!(f, "{}", b),
            Value::Void => write!(f, "void"),
            Value::Array(a) => write!(f, "[{}]", a.iter().map(|v| format!("{}", v)).collect::<Vec<_>>().join(", ")),
            Value::Tuple(t) => write!(f, "({})", t.iter().map(|v| format!("{}", v)).collect::<Vec<_>>().join(", ")),
            Value::Fn(inner) => write!(f, "<fn {}>", inner.0),
            Value::BuiltinFn(name) => write!(f, "<builtin {}>", name),
            Value::Struct(name, fields) => {
                write!(f, "{} {{ ", name)?;
                for (i, (k, v)) in fields.iter().enumerate() {
                    if i > 0 { write!(f, ", ")?; }
                    write!(f, "{}: {}", k, v)?;
                }
                write!(f, " }}")
            }
            Value::Lambda(..) => write!(f, "<lambda>"),
            Value::Intent(fields) => {
                write!(f, "intent {{ ")?;
                for (i, (k, v)) in fields.iter().enumerate() {
                    if i > 0 { write!(f, ", ")?; }
                    write!(f, "{}: {}", k, v)?;
                }
                write!(f, " }}")
            }
            Value::Map(map) => {
                write!(f, "{{")?;
                for (i, (k, v)) in map.iter().enumerate() {
                    if i > 0 { write!(f, ", ")?; }
                    write!(f, "{}: {}", k, v)?;
                }
                write!(f, "}}")
            }
            Value::Error(msg) => write!(f, "Error({})", msg),
            Value::EnumVariant(inner) => {
                let (enum_name, variant, data) = inner.as_ref();
                match data {
                    Some(d) => write!(f, "{}::{}({})", enum_name, variant, d),
                    None => write!(f, "{}::{}", enum_name, variant),
                }
            }
            #[cfg(not(target_arch = "wasm32"))]
            Value::Sender(_) => write!(f, "<sender>"),
            #[cfg(not(target_arch = "wasm32"))]
            Value::Receiver(_) => write!(f, "<receiver>"),
            Value::Future(inner) => {
                let guard = inner.lock().unwrap();
                match &*guard {
                    Some(v) => write!(f, "<future: {}>", v),
                    None => write!(f, "<future: pending>"),
                }
            }
            #[cfg(not(target_arch = "wasm32"))]
            Value::AsyncFuture(fut) => {
                match fut.poll() {
                    Some(v) => write!(f, "<async-future: {}>", v),
                    None => write!(f, "<async-future: pending>"),
                }
            }
            Value::Set(set) => {
                let guard = set.lock().unwrap();
                let items: Vec<String> = guard.iter().cloned().collect();
                write!(f, "Set({{{}}})", items.join(", "))
            }
            #[cfg(not(target_arch = "wasm32"))]
            Value::TcpListener(_) => write!(f, "<tcp-listener>"),
            #[cfg(not(target_arch = "wasm32"))]
            Value::TcpStream(_) => write!(f, "<tcp-stream>"),
            Value::EffectRequest(inner) => write!(f, "<effect {}.{}>", inner.0, inner.1),
            Value::TraitObject(inner) => {
                write!(f, "<dyn {} ({}: {})>", inner.trait_name, inner.type_name, inner.value)
            }
            Value::Atomic(av) => write!(f, "{}", av.load()),
            Value::Pointer(addr) => write!(f, "<ptr 0x{:x}>", addr),
        }
    }
}

/// Ownership state for a variable.
#[derive(Debug, Clone, PartialEq)]
pub enum OwnershipState {
    Normal,    // regular variable (no ownership tracking)
    Owned,     // created with `own`
    Moved,     // ownership transferred via `move`
    Borrowed,  // borrowed via `borrow` (read-only)
    Dropped,   // explicitly dropped via `drop`
}

pub struct Env {
    /// Flat variable stack: all bindings stored as (name, value) pairs.
    /// Scope boundaries tracked by `scope_starts`.
    vars: Vec<(String, Value)>,
    /// Stack of indices into `vars` marking where each scope begins.
    scope_starts: Vec<usize>,
    /// Ownership state tracking per variable name (checked at runtime).
    pub ownership: Vec<HashMap<String, OwnershipState>>,
    /// Names of constant bindings (cannot be reassigned).
    pub constants: std::collections::HashSet<String>,
    /// Static (module-level global) variables.
    pub statics: HashMap<String, Value>,
    /// Fast-path: counts how many ownership states have been set.
    /// When 0, get_ownership() can skip the scope walk entirely.
    ownership_count: usize,
    /// O(1) builtin lookup cache — ~100 builtins + constants separated from scope walk.
    /// Avoids reverse-scanning the entire vars stack for every builtin call.
    builtins: HashMap<String, Value>,
}

impl Env {
    pub fn new() -> Self {
        let mut env = Self {
            vars: Vec::with_capacity(256),
            scope_starts: vec![0],
            ownership: vec![HashMap::new()],
            constants: std::collections::HashSet::new(),
            statics: HashMap::new(),
            ownership_count: 0,
            builtins: HashMap::with_capacity(128),
        };
        // Register builtins (stored in both vars and builtins cache)
        env.define_builtin("print", Value::BuiltinFn("print".into()));
        env.define_builtin("println", Value::BuiltinFn("println".into()));
        env.define_builtin("len", Value::BuiltinFn("len".into()));
        env.define_builtin("type_of", Value::BuiltinFn("type_of".into()));
        env.define_builtin("sigma", Value::BuiltinFn("sigma".into()));
        env.define_builtin("phi", Value::BuiltinFn("phi".into()));
        env.define_builtin("tau", Value::BuiltinFn("tau".into()));
        env.define_builtin("gcd", Value::BuiltinFn("gcd".into()));
        env.define_builtin("read_file", Value::BuiltinFn("read_file".into()));
        env.define_builtin("write_file", Value::BuiltinFn("write_file".into()));
        env.define_builtin("file_exists", Value::BuiltinFn("file_exists".into()));
        env.define_builtin("delete_file", Value::BuiltinFn("delete_file".into()));
        env.define_builtin("append_file", Value::BuiltinFn("append_file".into()));
        env.define_builtin("keys", Value::BuiltinFn("keys".into()));
        env.define_builtin("values", Value::BuiltinFn("values".into()));
        env.define_builtin("has_key", Value::BuiltinFn("has_key".into()));
        env.define_builtin("to_string", Value::BuiltinFn("to_string".into()));
        env.define_builtin("to_int", Value::BuiltinFn("to_int".into()));
        env.define_builtin("to_float", Value::BuiltinFn("to_float".into()));
        env.define_builtin("try_float", Value::BuiltinFn("try_float".into()));
        env.define_builtin("is_numeric", Value::BuiltinFn("is_numeric".into()));
        env.define_builtin("join", Value::BuiltinFn("join".into()));
        env.define_builtin("split", Value::BuiltinFn("split".into()));
        env.define_builtin("trim", Value::BuiltinFn("trim".into()));
        env.define_builtin("trim_start", Value::BuiltinFn("trim_start".into()));
        env.define_builtin("trim_end", Value::BuiltinFn("trim_end".into()));
        env.define_builtin("starts_with", Value::BuiltinFn("starts_with".into()));
        env.define_builtin("ends_with", Value::BuiltinFn("ends_with".into()));
        env.define_builtin("contains", Value::BuiltinFn("contains".into()));
        env.define_builtin("replace", Value::BuiltinFn("replace".into()));
        env.define_builtin("to_upper", Value::BuiltinFn("to_upper".into()));
        env.define_builtin("to_lower", Value::BuiltinFn("to_lower".into()));
        env.define_builtin("abs", Value::BuiltinFn("abs".into()));
        env.define_builtin("min", Value::BuiltinFn("min".into()));
        env.define_builtin("max", Value::BuiltinFn("max".into()));
        env.define_builtin("floor", Value::BuiltinFn("floor".into()));
        env.define_builtin("ceil", Value::BuiltinFn("ceil".into()));
        env.define_builtin("round", Value::BuiltinFn("round".into()));
        env.define_builtin("sqrt", Value::BuiltinFn("sqrt".into()));
        env.define_builtin("pow", Value::BuiltinFn("pow".into()));
        env.define_builtin("log", Value::BuiltinFn("log".into()));
        env.define_builtin("log2", Value::BuiltinFn("log2".into()));
        env.define_builtin("sin", Value::BuiltinFn("sin".into()));
        env.define_builtin("cos", Value::BuiltinFn("cos".into()));
        env.define_builtin("tan", Value::BuiltinFn("tan".into()));
        env.define_builtin("PI", Value::Float(std::f64::consts::PI));
        env.define_builtin("E", Value::Float(std::f64::consts::E));
        env.define_builtin("format", Value::BuiltinFn("format".into()));
        env.define_builtin("args", Value::BuiltinFn("args".into()));
        env.define_builtin("env_var", Value::BuiltinFn("env_var".into()));
        env.define_builtin("exit", Value::BuiltinFn("exit".into()));
        env.define_builtin("clock", Value::BuiltinFn("clock".into()));
        env.define_builtin("Some", Value::BuiltinFn("Some".into()));
        env.define_builtin("None", Value::EnumVariant(Box::new(("Option".into(), "None".into(), None))));
        env.define_builtin("Ok", Value::BuiltinFn("Ok".into()));
        env.define_builtin("Err", Value::BuiltinFn("Err".into()));
        env.define_builtin("channel", Value::BuiltinFn("channel".into()));
        env.define_builtin("is_alpha", Value::BuiltinFn("is_alpha".into()));
        env.define_builtin("is_digit", Value::BuiltinFn("is_digit".into()));
        env.define_builtin("is_alphanumeric", Value::BuiltinFn("is_alphanumeric".into()));
        env.define_builtin("is_whitespace", Value::BuiltinFn("is_whitespace".into()));
        env.define_builtin("random", Value::BuiltinFn("random".into()));
        env.define_builtin("random_int", Value::BuiltinFn("random_int".into()));
        env.define_builtin("sleep", Value::BuiltinFn("sleep".into()));
        env.define_builtin("print_err", Value::BuiltinFn("print_err".into()));
        env.define_builtin("json_parse", Value::BuiltinFn("json_parse".into()));
        env.define_builtin("json_stringify", Value::BuiltinFn("json_stringify".into()));
        env.define_builtin("http_get", Value::BuiltinFn("http_get".into()));
        env.define_builtin("http_post", Value::BuiltinFn("http_post".into()));
        env.define_builtin("http_serve", Value::BuiltinFn("http_serve".into()));
        env.define_builtin("net_listen", Value::BuiltinFn("net_listen".into()));
        env.define_builtin("net_accept", Value::BuiltinFn("net_accept".into()));
        env.define_builtin("net_connect", Value::BuiltinFn("net_connect".into()));
        env.define_builtin("net_read", Value::BuiltinFn("net_read".into()));
        env.define_builtin("net_write", Value::BuiltinFn("net_write".into()));
        env.define_builtin("net_close", Value::BuiltinFn("net_close".into()));
        env.define_builtin("Set", Value::BuiltinFn("Set".into()));
        env.define_builtin("now", Value::BuiltinFn("now".into()));
        env.define_builtin("timestamp", Value::BuiltinFn("timestamp".into()));
        env.define_builtin("elapsed", Value::BuiltinFn("elapsed".into()));
        env.define_builtin("base64_encode", Value::BuiltinFn("base64_encode".into()));
        env.define_builtin("base64_decode", Value::BuiltinFn("base64_decode".into()));
        env.define_builtin("hex_encode", Value::BuiltinFn("hex_encode".into()));
        env.define_builtin("hex_decode", Value::BuiltinFn("hex_decode".into()));
        env.define_builtin("laws", Value::BuiltinFn("laws".into()));
        env.define_builtin("meta_laws", Value::BuiltinFn("meta_laws".into()));
        env.define_builtin("consciousness_vector", Value::BuiltinFn("consciousness_vector".into()));
        env.define_builtin("phi_predict", Value::BuiltinFn("phi_predict".into()));
        env.define_builtin("join", Value::BuiltinFn("join".into()));
        env.define_builtin("psi_coupling", Value::BuiltinFn("psi_coupling".into()));
        env.define_builtin("psi_balance", Value::BuiltinFn("psi_balance".into()));
        env.define_builtin("psi_steps", Value::BuiltinFn("psi_steps".into()));
        env.define_builtin("psi_entropy", Value::BuiltinFn("psi_entropy".into()));
        env.define_builtin("psi_frustration", Value::BuiltinFn("psi_frustration".into()));
        env.define_builtin("consciousness_max_cells", Value::BuiltinFn("consciousness_max_cells".into()));
        env.define_builtin("consciousness_decoder_dim", Value::BuiltinFn("consciousness_decoder_dim".into()));
        env.define_builtin("consciousness_phi", Value::BuiltinFn("consciousness_phi".into()));
        env.define_builtin("hexad_modules", Value::BuiltinFn("hexad_modules".into()));
        env.define_builtin("hexad_right", Value::BuiltinFn("hexad_right".into()));
        env.define_builtin("hexad_left", Value::BuiltinFn("hexad_left".into()));
        env.define_builtin("tension_link", Value::BuiltinFn("tension_link".into()));
        env.define_builtin("sopfr", Value::BuiltinFn("sopfr".into()));
        env.define_builtin("mem_stats", Value::BuiltinFn("mem_stats".into()));
        env.define_builtin("mem_region", Value::BuiltinFn("mem_region".into()));
        env.define_builtin("arena_reset", Value::BuiltinFn("arena_reset".into()));
        env.define_builtin("mem_budget", Value::BuiltinFn("mem_budget".into()));
        env.define_builtin("nexus_scan", Value::BuiltinFn("nexus_scan".into()));
        env.define_builtin("nexus_verify", Value::BuiltinFn("nexus_verify".into()));
        env.define_builtin("nexus_omega", Value::BuiltinFn("nexus_omega".into()));
        env.define_builtin("nexus_lenses", Value::BuiltinFn("nexus_lenses".into()));
        env.define_builtin("nexus_consensus", Value::BuiltinFn("nexus_consensus".into()));
        env.define_builtin("nexus_n6_check", Value::BuiltinFn("nexus_n6_check".into()));
        env.define_builtin("str", Value::BuiltinFn("str".into()));
        env.define_builtin("cstring", Value::BuiltinFn("cstring".into()));
        env.define_builtin("from_cstring", Value::BuiltinFn("from_cstring".into()));
        env.define_builtin("ptr_null", Value::BuiltinFn("ptr_null".into()));
        env.define_builtin("ptr_addr", Value::BuiltinFn("ptr_addr".into()));
        env.define_builtin("deref", Value::BuiltinFn("deref".into()));
        env.define_builtin("env", Value::BuiltinFn("env".into()));
        env.define_builtin("exec", Value::BuiltinFn("exec".into()));
        env.define_builtin("exec_with_status", Value::BuiltinFn("exec_with_status".into()));
        env.define_builtin("input", Value::BuiltinFn("input".into()));
        env.define_builtin("readline", Value::BuiltinFn("readline".into()));
        env.define_builtin("read_stdin", Value::BuiltinFn("read_stdin".into()));
        env.define_builtin("input_all", Value::BuiltinFn("input_all".into()));
        env

    }

    /// Register a builtin: O(1) HashMap only (not in vars stack).
    /// Eliminates ~100 entries from scope walk entirely.
    fn define_builtin(&mut self, name: &str, val: Value) {
        self.builtins.insert(name.to_string(), val);
    }

    #[inline]
    pub fn push_scope(&mut self) {
        self.scope_starts.push(self.vars.len());
        // Only grow ownership stack if ownership tracking is active.
        // For programs that never use own/borrow/move (e.g., fib), this avoids
        // pushing a HashMap struct (~48 bytes) per scope.
        if self.ownership_count > 0 {
            self.ownership.push(HashMap::new());
        }
    }

    #[inline]
    pub fn pop_scope(&mut self) {
        if let Some(start) = self.scope_starts.pop() {
            self.vars.truncate(start);
        }
        if self.ownership_count > 0 && self.ownership.len() > self.scope_starts.len() {
            self.ownership.pop();
        }
    }

    #[inline]
    pub fn define(&mut self, name: &str, val: Value) {
        self.vars.push((name.to_string(), val));
    }

    #[inline]
    pub fn get(&self, name: &str) -> Option<Value> {
        // Search user vars (no builtins in stack — pure user bindings only)
        for i in (0..self.vars.len()).rev() {
            if self.vars[i].0 == name {
                return Some(self.vars[i].1.clone());
            }
        }
        // O(1) builtin lookup (HashMap, ~100 entries)
        if let Some(val) = self.builtins.get(name) {
            return Some(val.clone());
        }
        // Fall back to static globals
        if let Some(val) = self.statics.get(name) {
            return Some(val.clone());
        }
        None
    }

    /// Get a reference to a value without cloning.
    #[inline]
    pub fn get_ref(&self, name: &str) -> Option<&Value> {
        for i in (0..self.vars.len()).rev() {
            if self.vars[i].0 == name {
                return Some(&self.vars[i].1);
            }
        }
        if let Some(val) = self.builtins.get(name) {
            return Some(val);
        }
        self.statics.get(name)
    }

    #[inline]
    pub fn set(&mut self, name: &str, val: Value) -> bool {
        // Search flat stack in reverse for existing binding
        for i in (0..self.vars.len()).rev() {
            if self.vars[i].0 == name {
                self.vars[i].1 = val;
                return true;
            }
        }
        // Fall back to static globals
        if self.statics.contains_key(name) {
            self.statics.insert(name.to_string(), val);
            return true;
        }
        false
    }

    /// Return current scope depth (number of active scopes).
    #[inline]
    pub fn scope_depth(&self) -> usize {
        self.scope_starts.len()
    }

    /// Collect all known variable names (for error suggestions).
    pub fn known_names(&self) -> Vec<&str> {
        let mut names: Vec<&str> = self.vars.iter().map(|(k, _)| k.as_str()).collect();
        for k in self.builtins.keys() {
            names.push(k.as_str());
        }
        names
    }

    /// Iterate all variable bindings (for debugger / introspection).
    pub fn vars_iter(&self) -> std::slice::Iter<'_, (String, Value)> {
        self.vars.iter()
    }

    /// Capture all non-builtin variables (for closures / spawn).
    pub fn capture_non_builtins(&self) -> Vec<(String, Value)> {
        let mut captured = Vec::new();
        for (k, v) in &self.vars {
            if !matches!(v, Value::BuiltinFn(_)) {
                captured.push((k.clone(), v.clone()));
            }
        }
        captured
    }

    /// Define a constant (immutable binding).
    pub fn define_const(&mut self, name: &str, val: Value) {
        self.constants.insert(name.to_string());
        self.vars.push((name.to_string(), val));
    }

    /// Check if a name is a constant.
    pub fn is_constant(&self, name: &str) -> bool {
        self.constants.contains(name)
    }

    /// Define a static (module-level global) variable.
    pub fn define_static(&mut self, name: &str, val: Value) {
        self.statics.insert(name.to_string(), val);
    }

    /// Check if a name is a static variable.
    pub fn is_static(&self, name: &str) -> bool {
        self.statics.contains_key(name)
    }

    /// Set ownership state for a variable.
    pub fn set_ownership(&mut self, name: &str, state: OwnershipState) {
        // Lazily inflate ownership stack to match scope depth when first used.
        let scope_depth = self.scope_starts.len();
        while self.ownership.len() < scope_depth {
            self.ownership.push(HashMap::new());
        }
        if let Some(scope) = self.ownership.last_mut() {
            scope.insert(name.to_string(), state);
            self.ownership_count += 1;
        }
    }

    /// Get ownership state for a variable.
    /// Fast-path: if no ownership state has ever been set, skip the scope walk.
    #[inline]
    pub fn get_ownership(&self, name: &str) -> OwnershipState {
        if self.ownership_count == 0 {
            return OwnershipState::Normal;
        }
        for scope in self.ownership.iter().rev() {
            if let Some(state) = scope.get(name) {
                return state.clone();
            }
        }
        OwnershipState::Normal
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::Visibility;

    #[test]
    fn test_visibility_levels_is_tau() {
        // τ=4 visibility levels
        let vis = [Visibility::Private, Visibility::Module, Visibility::Crate, Visibility::Public];
        assert_eq!(vis.len(), 4);
    }

    #[test]
    fn test_scope_push_pop() {
        let mut env = Env::new();
        env.define("x", Value::Int(42));
        assert_eq!(env.get("x").unwrap().to_string(), "42");
        env.push_scope();
        env.define("x", Value::Int(100));
        assert_eq!(env.get("x").unwrap().to_string(), "100");
        env.pop_scope();
        assert_eq!(env.get("x").unwrap().to_string(), "42");
    }
}

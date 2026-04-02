#![allow(dead_code)]

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
#[cfg(not(target_arch = "wasm32"))]
use std::sync::mpsc;

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
    Fn(String, Vec<String>, Vec<crate::ast::Stmt>), // name, param_names, body
    BuiltinFn(String),  // name of builtin
    Struct(String, HashMap<String, Value>),  // name, fields
    Lambda(Vec<String>, Vec<crate::ast::Stmt>, Vec<(String, Value)>), // params, body, captured env
    Map(HashMap<String, Value>),  // key-value map
    Error(String),  // error value for try/catch
    EnumVariant(String, String, Option<Box<Value>>),  // enum_name, variant_name, data
    Intent(HashMap<String, Value>),  // consciousness experiment declaration
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
    EffectRequest(String, String, Vec<Value>),  // effect_name, op_name, args
    /// Trait object — wraps a value with its vtable key for dynamic dispatch.
    TraitObject {
        value: Box<Value>,
        trait_name: String,
        type_name: String,
    },
    /// Atomic value for lock-free concurrent access across green threads.
    Atomic(std::sync::Arc<crate::atomic_ops::AtomicValue>),
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
            Value::Fn(name, ..) => write!(f, "<fn {}>", name),
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
            Value::EnumVariant(enum_name, variant, data) => {
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
            Value::EffectRequest(effect, op, _) => write!(f, "<effect {}.{}>", effect, op),
            Value::TraitObject { value, trait_name, type_name } => {
                write!(f, "<dyn {} ({}: {})>", trait_name, type_name, value)
            }
            Value::Atomic(av) => write!(f, "{}", av.load()),
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
    pub scopes: Vec<HashMap<String, Value>>,
    /// Ownership state tracking per variable name (checked at runtime).
    pub ownership: Vec<HashMap<String, OwnershipState>>,
    /// Names of constant bindings (cannot be reassigned).
    pub constants: std::collections::HashSet<String>,
    /// Static (module-level global) variables.
    pub statics: HashMap<String, Value>,
}

impl Env {
    pub fn new() -> Self {
        let mut env = Self {
            scopes: vec![HashMap::new()],
            ownership: vec![HashMap::new()],
            constants: std::collections::HashSet::new(),
            statics: HashMap::new(),
        };
        // Register builtins
        env.define("print", Value::BuiltinFn("print".into()));
        env.define("println", Value::BuiltinFn("println".into()));
        env.define("len", Value::BuiltinFn("len".into()));
        env.define("type_of", Value::BuiltinFn("type_of".into()));
        env.define("sigma", Value::BuiltinFn("sigma".into()));
        env.define("phi", Value::BuiltinFn("phi".into()));
        env.define("tau", Value::BuiltinFn("tau".into()));
        env.define("gcd", Value::BuiltinFn("gcd".into()));
        env.define("read_file", Value::BuiltinFn("read_file".into()));
        env.define("write_file", Value::BuiltinFn("write_file".into()));
        env.define("file_exists", Value::BuiltinFn("file_exists".into()));
        env.define("keys", Value::BuiltinFn("keys".into()));
        env.define("values", Value::BuiltinFn("values".into()));
        env.define("has_key", Value::BuiltinFn("has_key".into()));
        env.define("to_string", Value::BuiltinFn("to_string".into()));
        env.define("to_int", Value::BuiltinFn("to_int".into()));
        env.define("to_float", Value::BuiltinFn("to_float".into()));
        // Math builtins
        env.define("abs", Value::BuiltinFn("abs".into()));
        env.define("min", Value::BuiltinFn("min".into()));
        env.define("max", Value::BuiltinFn("max".into()));
        env.define("floor", Value::BuiltinFn("floor".into()));
        env.define("ceil", Value::BuiltinFn("ceil".into()));
        env.define("round", Value::BuiltinFn("round".into()));
        env.define("sqrt", Value::BuiltinFn("sqrt".into()));
        env.define("pow", Value::BuiltinFn("pow".into()));
        env.define("log", Value::BuiltinFn("log".into()));
        env.define("log2", Value::BuiltinFn("log2".into()));
        env.define("sin", Value::BuiltinFn("sin".into()));
        env.define("cos", Value::BuiltinFn("cos".into()));
        env.define("tan", Value::BuiltinFn("tan".into()));
        // Math constants
        env.define("PI", Value::Float(std::f64::consts::PI));
        env.define("E", Value::Float(std::f64::consts::E));
        // Format builtins
        env.define("format", Value::BuiltinFn("format".into()));
        // OS builtins
        env.define("args", Value::BuiltinFn("args".into()));
        env.define("env_var", Value::BuiltinFn("env_var".into()));
        env.define("exit", Value::BuiltinFn("exit".into()));
        env.define("clock", Value::BuiltinFn("clock".into()));
        // Built-in Option/Result constructors
        env.define("Some", Value::BuiltinFn("Some".into()));
        env.define("None", Value::EnumVariant("Option".into(), "None".into(), None));
        env.define("Ok", Value::BuiltinFn("Ok".into()));
        env.define("Err", Value::BuiltinFn("Err".into()));
        // Concurrency builtins
        env.define("channel", Value::BuiltinFn("channel".into()));
        // Char utility builtins
        env.define("is_alpha", Value::BuiltinFn("is_alpha".into()));
        env.define("is_digit", Value::BuiltinFn("is_digit".into()));
        env.define("is_alphanumeric", Value::BuiltinFn("is_alphanumeric".into()));
        env.define("is_whitespace", Value::BuiltinFn("is_whitespace".into()));
        // Random builtins
        env.define("random", Value::BuiltinFn("random".into()));
        env.define("random_int", Value::BuiltinFn("random_int".into()));
        // System builtins
        env.define("sleep", Value::BuiltinFn("sleep".into()));
        env.define("print_err", Value::BuiltinFn("print_err".into()));
        // JSON builtins
        env.define("json_parse", Value::BuiltinFn("json_parse".into()));
        env.define("json_stringify", Value::BuiltinFn("json_stringify".into()));
        // HTTP builtins (std::net)
        env.define("http_get", Value::BuiltinFn("http_get".into()));
        env.define("http_post", Value::BuiltinFn("http_post".into()));
        // Set constructor (std::collections)
        env.define("Set", Value::BuiltinFn("Set".into()));
        // Time builtins (std::time)
        env.define("now", Value::BuiltinFn("now".into()));
        env.define("timestamp", Value::BuiltinFn("timestamp".into()));
        env.define("elapsed", Value::BuiltinFn("elapsed".into()));
        // Encoding builtins (std::encoding)
        env.define("base64_encode", Value::BuiltinFn("base64_encode".into()));
        env.define("base64_decode", Value::BuiltinFn("base64_decode".into()));
        env.define("hex_encode", Value::BuiltinFn("hex_encode".into()));
        env.define("hex_decode", Value::BuiltinFn("hex_decode".into()));
        // Consciousness builtins v2 (std::consciousness)
        env.define("laws", Value::BuiltinFn("laws".into()));
        env.define("meta_laws", Value::BuiltinFn("meta_laws".into()));
        env.define("consciousness_vector", Value::BuiltinFn("consciousness_vector".into()));
        env.define("phi_predict", Value::BuiltinFn("phi_predict".into()));
        // Spawn/join builtins
        env.define("join", Value::BuiltinFn("join".into()));
        // Consciousness builtins (ANIMA Psi-Constants)
        env.define("psi_coupling", Value::BuiltinFn("psi_coupling".into()));
        env.define("psi_balance", Value::BuiltinFn("psi_balance".into()));
        env.define("psi_steps", Value::BuiltinFn("psi_steps".into()));
        env.define("psi_entropy", Value::BuiltinFn("psi_entropy".into()));
        env.define("psi_frustration", Value::BuiltinFn("psi_frustration".into()));
        // Consciousness architecture formulas
        env.define("consciousness_max_cells", Value::BuiltinFn("consciousness_max_cells".into()));
        env.define("consciousness_decoder_dim", Value::BuiltinFn("consciousness_decoder_dim".into()));
        env.define("consciousness_phi", Value::BuiltinFn("consciousness_phi".into()));
        // Hexad module info
        env.define("hexad_modules", Value::BuiltinFn("hexad_modules".into()));
        env.define("hexad_right", Value::BuiltinFn("hexad_right".into()));
        env.define("hexad_left", Value::BuiltinFn("hexad_left".into()));
        // Consciousness v2: tension_link builtin
        env.define("tension_link", Value::BuiltinFn("tension_link".into()));
        // Number theory: sopfr (sum of prime factors with repetition)
        env.define("sopfr", Value::BuiltinFn("sopfr".into()));
        // Egyptian Fraction memory introspection (1/2 + 1/3 + 1/6 = 1)
        env.define("mem_stats", Value::BuiltinFn("mem_stats".into()));
        env.define("mem_region", Value::BuiltinFn("mem_region".into()));
        env.define("arena_reset", Value::BuiltinFn("arena_reset".into()));
        env.define("mem_budget", Value::BuiltinFn("mem_budget".into()));
        env

    }

    pub fn push_scope(&mut self) { self.scopes.push(HashMap::new()); self.ownership.push(HashMap::new()); }
    pub fn pop_scope(&mut self) { self.scopes.pop(); self.ownership.pop(); }

    pub fn define(&mut self, name: &str, val: Value) {
        self.scopes.last_mut().unwrap().insert(name.to_string(), val);
    }

    pub fn get(&self, name: &str) -> Option<Value> {
        for scope in self.scopes.iter().rev() {
            if let Some(val) = scope.get(name) {
                return Some(val.clone());
            }
        }
        // Fall back to static globals
        if let Some(val) = self.statics.get(name) {
            return Some(val.clone());
        }
        None
    }

    pub fn set(&mut self, name: &str, val: Value) -> bool {
        for scope in self.scopes.iter_mut().rev() {
            if scope.contains_key(name) {
                scope.insert(name.to_string(), val);
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

    /// Define a constant (immutable binding).
    pub fn define_const(&mut self, name: &str, val: Value) {
        self.constants.insert(name.to_string());
        self.scopes.last_mut().unwrap().insert(name.to_string(), val);
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
        if let Some(scope) = self.ownership.last_mut() {
            scope.insert(name.to_string(), state);
        }
    }

    /// Get ownership state for a variable.
    pub fn get_ownership(&self, name: &str) -> OwnershipState {
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

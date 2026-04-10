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
    /// Arbitrary-precision integer (I6 bigint support).
    BigInt(Box<num_bigint::BigInt>),
    /// Dense f64 tensor — zero-copy via Arc, no per-element Value wrapping.
    Tensor(Arc<TensorData>),
}

/// Dense tensor storage for neural network weights and activations.
#[derive(Debug, Clone)]
pub struct TensorData {
    pub shape: Vec<usize>,
    pub data: Vec<f64>,
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
            Value::Float(n) => {
                if n.is_finite() && *n == n.trunc() && n.abs() < 1e16 {
                    write!(f, "{:.1}", n)
                } else {
                    write!(f, "{}", n)
                }
            }
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
            Value::BigInt(n) => write!(f, "{}", n),
            Value::Tensor(t) => write!(f, "<tensor {:?} len={}>", t.shape, t.data.len()),
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
        env.define_builtin("bigint", Value::BuiltinFn("bigint".into()));
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
        env.define_builtin("log10", Value::BuiltinFn("log10".into()));
        env.define_builtin("exp", Value::BuiltinFn("exp".into()));
        env.define_builtin("ln", Value::BuiltinFn("ln".into()));
        env.define_builtin("sin", Value::BuiltinFn("sin".into()));
        env.define_builtin("cos", Value::BuiltinFn("cos".into()));
        env.define_builtin("tan", Value::BuiltinFn("tan".into()));
        env.define_builtin("asin", Value::BuiltinFn("asin".into()));
        env.define_builtin("acos", Value::BuiltinFn("acos".into()));
        env.define_builtin("atan", Value::BuiltinFn("atan".into()));
        env.define_builtin("atan2", Value::BuiltinFn("atan2".into()));
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
        env.define_builtin("evolve_gen", Value::BuiltinFn("evolve_gen".into()));
        env.define_builtin("sigmoid", Value::BuiltinFn("sigmoid".into()));
        env.define_builtin("tanh_", Value::BuiltinFn("tanh_".into()));
        env.define_builtin("relu", Value::BuiltinFn("relu".into()));
        env.define_builtin("softmax", Value::BuiltinFn("softmax".into()));
        env.define_builtin("cosine_sim", Value::BuiltinFn("cosine_sim".into()));
        env.define_builtin("run_tests", Value::BuiltinFn("run_tests".into()));
        env.define_builtin("run_benchmarks", Value::BuiltinFn("run_benchmarks".into()));
        env.define_builtin("matmul", Value::BuiltinFn("matmul".into()));
        env.define_builtin("transpose", Value::BuiltinFn("transpose".into()));
        env.define_builtin("normalize", Value::BuiltinFn("normalize".into()));
        env.define_builtin("cross_entropy", Value::BuiltinFn("cross_entropy".into()));
        env.define_builtin("argmax", Value::BuiltinFn("argmax".into()));
        env.define_builtin("zeros", Value::BuiltinFn("zeros".into()));
        env.define_builtin("ones", Value::BuiltinFn("ones".into()));
        env.define_builtin("randn", Value::BuiltinFn("randn".into()));
        env.define_builtin("arange", Value::BuiltinFn("arange".into()));
        env.define_builtin("ema", Value::BuiltinFn("ema".into()));
        env.define_builtin("clip", Value::BuiltinFn("clip".into()));
        env.define_builtin("sum", Value::BuiltinFn("sum".into()));
        env.define_builtin("mean", Value::BuiltinFn("mean".into()));
        env.define_builtin("hadamard", Value::BuiltinFn("hadamard".into()));
        env.define_builtin("map_arr", Value::BuiltinFn("map_arr".into()));
        env.define_builtin("filter_arr", Value::BuiltinFn("filter_arr".into()));
        env.define_builtin("reduce", Value::BuiltinFn("reduce".into()));
        env.define_builtin("zip_arr", Value::BuiltinFn("zip_arr".into()));
        env.define_builtin("enumerate_arr", Value::BuiltinFn("enumerate_arr".into()));
        env.define_builtin("flatten", Value::BuiltinFn("flatten".into()));
        env.define_builtin("slice", Value::BuiltinFn("slice".into()));
        env.define_builtin("ring_topology", Value::BuiltinFn("ring_topology".into()));
        env.define_builtin("small_world_topology", Value::BuiltinFn("small_world_topology".into()));
        env.define_builtin("hypercube_topology", Value::BuiltinFn("hypercube_topology".into()));
        env.define_builtin("auto_topology", Value::BuiltinFn("auto_topology".into()));
        env.define_builtin("lorenz_step", Value::BuiltinFn("lorenz_step".into()));
        env.define_builtin("chaos_perturb", Value::BuiltinFn("chaos_perturb".into()));
        env.define_builtin("cell_split", Value::BuiltinFn("cell_split".into()));
        env.define_builtin("cell_merge", Value::BuiltinFn("cell_merge".into()));
        env.define_builtin("faction_consensus", Value::BuiltinFn("faction_consensus".into()));
        env.define_builtin("tension", Value::BuiltinFn("tension".into()));
        env.define_builtin("sgd_step", Value::BuiltinFn("sgd_step".into()));
        env.define_builtin("adam_step", Value::BuiltinFn("adam_step".into()));
        env.define_builtin("numerical_grad", Value::BuiltinFn("numerical_grad".into()));
        env.define_builtin("warmup_lr", Value::BuiltinFn("warmup_lr".into()));
        env.define_builtin("cosine_lr", Value::BuiltinFn("cosine_lr".into()));
        env.define_builtin("phase_lr", Value::BuiltinFn("phase_lr".into()));
        env.define_builtin("batch_matvec", Value::BuiltinFn("batch_matvec".into()));
        env.define_builtin("batch_norm", Value::BuiltinFn("batch_norm".into()));
        env.define_builtin("grad_accumulate", Value::BuiltinFn("grad_accumulate".into()));
        env.define_builtin("layer_norm", Value::BuiltinFn("layer_norm".into()));
        env.define_builtin("dropout", Value::BuiltinFn("dropout".into()));
        env.define_builtin("embedding", Value::BuiltinFn("embedding".into()));
        env.define_builtin("attention", Value::BuiltinFn("attention".into()));
        env.define_builtin("gru_cell", Value::BuiltinFn("gru_cell".into()));
        env.define_builtin("gelu", Value::BuiltinFn("gelu".into()));
        env.define_builtin("silu", Value::BuiltinFn("silu".into()));
        env.define_builtin("sinusoidal_pe", Value::BuiltinFn("sinusoidal_pe".into()));
        env.define_builtin("multi_head_attention", Value::BuiltinFn("multi_head_attention".into()));
        env.define_builtin("topk", Value::BuiltinFn("topk".into()));
        env.define_builtin("sample_token", Value::BuiltinFn("sample_token".into()));
        env.define_builtin("mse_loss", Value::BuiltinFn("mse_loss".into()));
        env.define_builtin("conv1d", Value::BuiltinFn("conv1d".into()));
        env.define_builtin("max_pool1d", Value::BuiltinFn("max_pool1d".into()));
        env.define_builtin("kv_cache_append", Value::BuiltinFn("kv_cache_append".into()));
        env.define_builtin("attention_cached", Value::BuiltinFn("attention_cached".into()));
        env.define_builtin("save_array", Value::BuiltinFn("save_array".into()));
        env.define_builtin("load_array", Value::BuiltinFn("load_array".into()));
        env.define_builtin("quantize_int8", Value::BuiltinFn("quantize_int8".into()));
        env.define_builtin("dequantize_int8", Value::BuiltinFn("dequantize_int8".into()));
        env.define_builtin("beam_search_step", Value::BuiltinFn("beam_search_step".into()));
        env.define_builtin("grad_clip_norm", Value::BuiltinFn("grad_clip_norm".into()));
        env.define_builtin("xavier_init", Value::BuiltinFn("xavier_init".into()));
        env.define_builtin("kaiming_init", Value::BuiltinFn("kaiming_init".into()));
        env.define_builtin("curiosity_reward", Value::BuiltinFn("curiosity_reward".into()));
        env.define_builtin("dialogue_reward", Value::BuiltinFn("dialogue_reward".into()));
        env.define_builtin("combined_reward", Value::BuiltinFn("combined_reward".into()));
        env.define_builtin("magnitude_prune", Value::BuiltinFn("magnitude_prune".into()));
        env.define_builtin("sparsity", Value::BuiltinFn("sparsity".into()));
        env.define_builtin("autocorrelation", Value::BuiltinFn("autocorrelation".into()));
        env.define_builtin("trend_slope", Value::BuiltinFn("trend_slope".into()));
        env.define_builtin("running_stats", Value::BuiltinFn("running_stats".into()));
        env.define_builtin("ascii_plot", Value::BuiltinFn("ascii_plot".into()));
        env.define_builtin("ascii_bar", Value::BuiltinFn("ascii_bar".into()));
        env.define_builtin("data_split", Value::BuiltinFn("data_split".into()));
        env.define_builtin("shuffle", Value::BuiltinFn("shuffle".into()));
        env.define_builtin("one_hot", Value::BuiltinFn("one_hot".into()));
        env.define_builtin("param_count", Value::BuiltinFn("param_count".into()));
        env.define_builtin("model_size_kb", Value::BuiltinFn("model_size_kb".into()));
        env.define_builtin("bytes_encode", Value::BuiltinFn("bytes_encode".into()));
        env.define_builtin("bytes_decode", Value::BuiltinFn("bytes_decode".into()));
        env.define_builtin("moe_gate", Value::BuiltinFn("moe_gate".into()));
        env.define_builtin("moe_combine", Value::BuiltinFn("moe_combine".into()));
        env.define_builtin("ewc_penalty", Value::BuiltinFn("ewc_penalty".into()));
        env.define_builtin("fisher_diagonal", Value::BuiltinFn("fisher_diagonal".into()));
        env.define_builtin("rms_norm", Value::BuiltinFn("rms_norm".into()));
        env.define_builtin("rope", Value::BuiltinFn("rope".into()));
        env.define_builtin("sliding_window_attention", Value::BuiltinFn("sliding_window_attention".into()));
        env.define_builtin("kl_divergence", Value::BuiltinFn("kl_divergence".into()));
        env.define_builtin("js_divergence", Value::BuiltinFn("js_divergence".into()));
        env.define_builtin("grouped_query_attention", Value::BuiltinFn("grouped_query_attention".into()));
        env.define_builtin("label_smoothing", Value::BuiltinFn("label_smoothing".into()));
        env.define_builtin("weight_decay", Value::BuiltinFn("weight_decay".into()));
        env.define_builtin("smooth_l1_loss", Value::BuiltinFn("smooth_l1_loss".into()));
        env.define_builtin("adamw_step", Value::BuiltinFn("adamw_step".into()));
        env.define_builtin("lora_init", Value::BuiltinFn("lora_init".into()));
        env.define_builtin("lora_forward", Value::BuiltinFn("lora_forward".into()));
        env.define_builtin("cosine_sim_matrix", Value::BuiltinFn("cosine_sim_matrix".into()));
        env.define_builtin("mutual_information", Value::BuiltinFn("mutual_information".into()));
        env.define_builtin("linear_attention", Value::BuiltinFn("linear_attention".into()));
        env.define_builtin("group_norm", Value::BuiltinFn("group_norm".into()));
        env.define_builtin("focal_loss", Value::BuiltinFn("focal_loss".into()));
        env.define_builtin("contrastive_loss", Value::BuiltinFn("contrastive_loss".into()));
        env.define_builtin("dot", Value::BuiltinFn("dot".into()));
        env.define_builtin("matvec", Value::BuiltinFn("matvec".into()));
        env.define_builtin("mat_add", Value::BuiltinFn("mat_add".into()));
        env.define_builtin("mat_add_inplace", Value::BuiltinFn("mat_add_inplace".into()));
        env.define_builtin("matmul_into", Value::BuiltinFn("matmul_into".into()));
        env.define_builtin("qkv_fused_into", Value::BuiltinFn("qkv_fused_into".into()));
        env.define_builtin("ffn_fused_into", Value::BuiltinFn("ffn_fused_into".into()));
        env.define_builtin("block_forward_fused", Value::BuiltinFn("block_forward_fused".into()));
        env.define_builtin("block_forward_chain", Value::BuiltinFn("block_forward_chain".into()));
        env.define_builtin("block_forward_chain_f32", Value::BuiltinFn("block_forward_chain_f32".into()));
        env.define_builtin("rope_inplace", Value::BuiltinFn("rope_inplace".into()));
        env.define_builtin("attention_fused_into", Value::BuiltinFn("attention_fused_into".into()));
        env.define_builtin("mat_scale", Value::BuiltinFn("mat_scale".into()));
        env.define_builtin("mat_scale_inplace", Value::BuiltinFn("mat_scale_inplace".into()));
        env.define_builtin("axpy", Value::BuiltinFn("axpy".into()));
        env.define_builtin("exec_with_status", Value::BuiltinFn("exec_with_status".into()));
        env.define_builtin("input", Value::BuiltinFn("input".into()));
        env.define_builtin("readline", Value::BuiltinFn("readline".into()));
        env.define_builtin("read_stdin", Value::BuiltinFn("read_stdin".into()));
        env.define_builtin("input_all", Value::BuiltinFn("input_all".into()));
        env.define_builtin("load_weights_bin", Value::BuiltinFn("load_weights_bin".into()));
        env.define_builtin("mmap_weights", Value::BuiltinFn("mmap_weights".into()));
        env.define_builtin("to_char", Value::BuiltinFn("to_char".into()));
        env.define_builtin("tensor", Value::BuiltinFn("tensor".into()));
        env.define_builtin("tensor_zeros", Value::BuiltinFn("tensor_zeros".into()));
        env.define_builtin("tensor_fill", Value::BuiltinFn("tensor_fill".into()));
        env.define_builtin("repeat_kv", Value::BuiltinFn("repeat_kv".into()));
        env.define_builtin("weight_dict", Value::BuiltinFn("weight_dict".into()));
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

#![allow(dead_code)]

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use crate::compiler::{OpCode, Chunk, CompiledFunction};
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};

/// A call frame on the VM call stack (flat dispatch — no recursive run_code).
#[derive(Debug)]
struct CallFrame {
    /// Return address: instruction pointer in the caller's code.
    return_ip: usize,
    /// Index into code_segments for the caller's code.
    return_code_idx: usize,
    /// Base index in the local slots for this frame.
    local_base: usize,
    /// @memoize: if Some((fn_idx, args_hash)), cache result on return.
    memo_key: Option<(usize, u64)>,
}

/// Stack-based bytecode virtual machine.
/// Uses flat dispatch: function calls push/pop explicit frames instead of
/// recursing into run_code(), eliminating Rust stack frame overhead per call.

/// Fast structural hash for Value — avoids format!("{:?}") heap allocation.
#[inline]
fn hash_value_fast(val: &Value, h: &mut impl std::hash::Hasher) {
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
            for item in arr { hash_value_fast(item, h); }
        }
        other => { std::hash::Hash::hash(&99u8, h); std::hash::Hash::hash(&format!("{:?}", other), h); }
    }
}

pub struct VM {
    /// Value stack.
    stack: Vec<Value>,
    /// Local variable slots (flat array, frames index into it).
    locals: Vec<Value>,
    /// Call stack (flat — no recursion).
    frames: Vec<CallFrame>,
    /// Code segments: index 0 = top-level code, 1+ = function bodies.
    /// Avoids Arc::clone per call by storing all code centrally.
    code_segments: Vec<Vec<OpCode>>,
    /// Constant pool (shared from chunk).
    constants: Vec<Value>,
    /// Interned string pool (shared from chunk) for GetGlobal/SetGlobal/CallFn/CallBuiltin name lookup.
    string_pool: Vec<String>,
    /// Compiled functions registry.
    functions: HashMap<String, CompiledFunction>,
    /// Indexed function table for O(1) lookup (built from functions HashMap).
    fn_table: Vec<CompiledFunction>,
    /// Mapping from string_pool index to fn_table index (u32::MAX = not a function).
    fn_index: Vec<u32>,
    /// Mapping from fn_table index to code_segments index.
    fn_code_idx: Vec<usize>,
    /// Global variables (for builtins like PI, E, etc.).
    globals: HashMap<String, Value>,
    /// Flat global values indexed by string_pool index (O(1) dispatch).
    global_values: Vec<Value>,
    /// Tracks which global slots are defined.
    global_set: Vec<bool>,
    /// Output capture buffer: when set, Print/Println write here instead of stdout.
    output_capture: Option<Arc<Mutex<String>>>,
    /// @memoize cache: fn_index -> (args_hash -> cached result).
    memo_cache: HashMap<usize, HashMap<u64, Value>>,
    /// Which fn_table entries are @memoize.
    fn_is_memoize: Vec<bool>,
}

impl VM {
    pub fn new() -> Self {
        let mut globals = HashMap::new();
        globals.insert("PI".into(), Value::Float(std::f64::consts::PI));
        globals.insert("E".into(), Value::Float(std::f64::consts::E));
        Self {
            stack: Vec::with_capacity(256),
            locals: Vec::new(),
            frames: Vec::with_capacity(64),
            code_segments: Vec::new(),
            constants: Vec::new(),
            string_pool: Vec::new(),
            functions: HashMap::new(),
            fn_table: Vec::new(),
            fn_index: Vec::new(),
            fn_code_idx: Vec::new(),
            globals,
            global_values: Vec::new(),
            global_set: Vec::new(),
            output_capture: None,
            memo_cache: HashMap::new(),
            fn_is_memoize: Vec::new(),
        }
    }

    /// Set an output capture buffer. When set, Print/Println write to this
    /// buffer instead of stdout.
    pub fn set_output_capture(&mut self, buf: Option<Arc<Mutex<String>>>) {
        self.output_capture = buf;
    }

    /// Write a string to the output (captured buffer or stdout).
    #[inline]
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
    #[inline]
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

    /// Setup function tables and code segments from a compiled chunk.
    fn setup_from_chunk(&mut self, chunk: &Chunk) {
        self.constants = chunk.constants.clone();
        self.string_pool = chunk.string_pool.clone();
        self.functions = chunk.functions.clone();

        // Build indexed function table for O(1) call dispatch
        self.fn_table = Vec::new();
        self.fn_index = vec![u32::MAX; self.string_pool.len()];
        // code_segments[0] = top-level code (placeholder, filled by caller)
        self.code_segments = vec![Vec::new()];
        self.fn_code_idx = Vec::new();

        self.fn_is_memoize = Vec::new();
        for (i, name) in self.string_pool.iter().enumerate() {
            if let Some(func) = self.functions.get(name) {
                let fn_idx = self.fn_table.len();
                self.fn_is_memoize.push(func.is_memoize);
                self.fn_table.push(func.clone());
                self.fn_index[i] = fn_idx as u32;
                // Store function code as a new segment (with Return sentinel)
                let code_idx = self.code_segments.len();
                let mut fn_code = (*func.code).clone();
                Self::ensure_return_sentinel(&mut fn_code);
                self.code_segments.push(fn_code);
                self.fn_code_idx.push(code_idx);
            }
        }

        // Build flat global index for O(1) GetGlobal/SetGlobal dispatch
        self.global_values = vec![Value::Void; self.string_pool.len()];
        self.global_set = vec![false; self.string_pool.len()];
        for (i, name) in self.string_pool.iter().enumerate() {
            if let Some(val) = self.globals.get(name) {
                self.global_values[i] = val.clone();
                self.global_set[i] = true;
            }
        }

        // Pre-allocate locals and stack
        self.locals.resize(chunk.local_count.max(256), Value::Void);
        self.stack.reserve(256);
    }

    /// Ensure a code segment ends with Return (sentinel for branchless dispatch).
    fn ensure_return_sentinel(code: &mut Vec<OpCode>) {
        if !matches!(code.last(), Some(OpCode::Return)) {
            code.push(OpCode::Return);
        }
    }

    /// Execute a compiled chunk. Returns the last value on the stack (or Void).
    pub fn execute(&mut self, chunk: &Chunk) -> Result<Value, HexaError> {
        self.setup_from_chunk(chunk);
        let mut code = chunk.code.clone();
        Self::ensure_return_sentinel(&mut code);
        self.code_segments[0] = code;
        self.run_flat(0)
    }

    /// Execute a compiled chunk with dead code elimination applied.
    pub fn execute_optimized(&mut self, chunk: &Chunk) -> Result<Value, HexaError> {
        self.setup_from_chunk(chunk);
        let mut optimized_code = chunk.code.clone();
        crate::compiler::eliminate_dead_code(&mut optimized_code);
        Self::ensure_return_sentinel(&mut optimized_code);
        self.code_segments[0] = optimized_code;
        self.run_flat(0)
    }

    /// Flat dispatch loop: ALL function calls handled via explicit frame stack.
    /// No recursive run_code() — eliminates Rust stack frame overhead per HEXA call.
    #[inline(always)]
    fn run_flat(&mut self, start_code_idx: usize) -> Result<Value, HexaError> {
        let mut ip: usize = 0;
        let mut code_idx: usize = start_code_idx;
        let mut local_base: usize = self.frames.last().map_or(0, |f| f.local_base);

        // Hot-path helpers
        macro_rules! pop_fast {
            ($self:expr) => {
                match $self.stack.pop() {
                    Some(v) => v,
                    None => return Err(runtime_err("stack underflow".into())),
                }
            };
        }
        // In-place Int binary op: avoids pop+push, modifies stack top directly
        macro_rules! int_binop_inplace {
            ($self:expr, $op:ident, $fallback:ident) => {{
                let len = $self.stack.len();
                if len < 2 {
                    return Err(runtime_err("stack underflow".into()));
                }
                let a_ref = unsafe { $self.stack.get_unchecked(len - 2) };
                let b_ref = unsafe { $self.stack.get_unchecked(len - 1) };
                if let (Value::Int(x), Value::Int(y)) = (a_ref, b_ref) {
                    let result = Value::Int((*x).$op(*y));
                    unsafe {
                        $self.stack.set_len(len - 1);
                        *$self.stack.get_unchecked_mut(len - 2) = result;
                    }
                } else {
                    let b = $self.stack.pop().unwrap();
                    let a = $self.stack.pop().unwrap();
                    $self.stack.push($self.$fallback(a, b)?);
                }
            }};
        }
        // Inline truthy check for Bool/Int fast path (avoids function call)
        macro_rules! is_truthy_fast {
            ($val:expr) => {
                match $val {
                    Value::Bool(b) => *b,
                    Value::Int(n) => *n != 0,
                    _ => is_truthy($val),
                }
            };
        }

        // Cache code pointer to avoid code_segments[code_idx] indexing per iteration.
        // SAFETY: all code segments end with Return sentinel, so ip never exceeds bounds.
        let mut code_ptr: *const OpCode = self.code_segments[code_idx].as_ptr();

        macro_rules! reload_code {
            ($self:expr, $ci:expr) => {{
                code_ptr = $self.code_segments[$ci].as_ptr();
            }};
        }

        loop {
            // SAFETY: all code segments end with Return sentinel — ip is always in bounds
            let op = unsafe { &*code_ptr.add(ip) };
            ip += 1;

            match op {
                OpCode::Const(idx) => {
                    let val = unsafe { self.constants.get_unchecked(*idx) };
                    if let Value::Int(n) = val {
                        self.stack.push(Value::Int(*n));
                    } else {
                        let pushed = match val {
                            Value::Float(f) => Value::Float(*f),
                            Value::Bool(b) => Value::Bool(*b),
                            Value::Void => Value::Void,
                            other => other.clone(),
                        };
                        self.stack.push(pushed);
                    }
                }
                OpCode::Void => {
                    self.stack.push(Value::Void);
                }
                OpCode::Pop => {
                    let len = self.stack.len();
                    if len > 0 {
                        let top = unsafe { self.stack.get_unchecked(len - 1) };
                        if top.is_value_type() {
                            unsafe { self.stack.set_len(len - 1); }
                        } else {
                            self.stack.pop();
                        }
                    }
                }
                OpCode::Dup => {
                    if let Some(top) = self.stack.last() {
                        let pushed = match top {
                            Value::Int(n) => Value::Int(*n),
                            Value::Float(f) => Value::Float(*f),
                            Value::Bool(b) => Value::Bool(*b),
                            Value::Void => Value::Void,
                            other => other.clone(),
                        };
                        self.stack.push(pushed);
                    }
                }
                OpCode::Truthy => {
                    let val = pop_fast!(self);
                    self.stack.push(Value::Bool(is_truthy(&val)));
                }

                // Variables — Int-first for branch prediction (most locals are Int in loops)
                OpCode::GetLocal(slot) => {
                    let idx = local_base + *slot;
                    let local = unsafe { self.locals.get_unchecked(idx) };
                    if let Value::Int(n) = local {
                        self.stack.push(Value::Int(*n));
                    } else {
                        let val = match local {
                            Value::Float(f) => Value::Float(*f),
                            Value::Bool(b) => Value::Bool(*b),
                            Value::Void => Value::Void,
                            other => other.clone(),
                        };
                        self.stack.push(val);
                    }
                }
                OpCode::SetLocal(slot) => {
                    let val = pop_fast!(self);
                    let idx = local_base + *slot;
                    if idx >= self.locals.len() { self.locals.resize(idx + 1, Value::Void); }
                    unsafe { *self.locals.get_unchecked_mut(idx) = val; }
                }
                OpCode::GetGlobal(idx) => {
                    let i = *idx as usize;
                    if unsafe { *self.global_set.get_unchecked(i) } {
                        let val = unsafe { self.global_values.get_unchecked(i) };
                        let pushed = match val {
                            Value::Int(n) => Value::Int(*n),
                            Value::Float(f) => Value::Float(*f),
                            Value::Bool(b) => Value::Bool(*b),
                            Value::Void => Value::Void,
                            other => other.clone(),
                        };
                        self.stack.push(pushed);
                    } else {
                        return Err(runtime_err(format!("undefined variable: {}", self.string_pool[i])));
                    }
                }
                OpCode::SetGlobal(idx) => {
                    let val = pop_fast!(self);
                    let i = *idx as usize;
                    unsafe {
                        *self.global_values.get_unchecked_mut(i) = val;
                        *self.global_set.get_unchecked_mut(i) = true;
                    }
                }

                // Arithmetic -- in-place Int fast-path (no pop+push for Int+Int)
                OpCode::Add => {
                    int_binop_inplace!(self, wrapping_add, op_add);
                }
                OpCode::Sub => {
                    int_binop_inplace!(self, wrapping_sub, op_sub);
                }
                OpCode::Mul => {
                    int_binop_inplace!(self, wrapping_mul, op_mul);
                }
                OpCode::Div => {
                    let len = self.stack.len();
                    if len < 2 {
                        return Err(runtime_err("stack underflow".into()));
                    }
                    let a_ref = unsafe { self.stack.get_unchecked(len - 2) };
                    let b_ref = unsafe { self.stack.get_unchecked(len - 1) };
                    if let (Value::Int(x), Value::Int(y)) = (a_ref, b_ref) {
                        if *y == 0 { return Err(runtime_err("division by zero".into())); }
                        let result = Value::Int(*x / *y);
                        unsafe {
                            self.stack.set_len(len - 1);
                            *self.stack.get_unchecked_mut(len - 2) = result;
                        }
                    } else {
                        let b = self.stack.pop().unwrap();
                        let a = self.stack.pop().unwrap();
                        self.stack.push(self.op_div(a, b)?);
                    }
                }
                OpCode::Mod => {
                    let len = self.stack.len();
                    if len < 2 {
                        return Err(runtime_err("stack underflow".into()));
                    }
                    let a_ref = unsafe { self.stack.get_unchecked(len - 2) };
                    let b_ref = unsafe { self.stack.get_unchecked(len - 1) };
                    if let (Value::Int(x), Value::Int(y)) = (a_ref, b_ref) {
                        if *y == 0 { return Err(runtime_err("division by zero".into())); }
                        let result = Value::Int(*x % *y);
                        unsafe {
                            self.stack.set_len(len - 1);
                            *self.stack.get_unchecked_mut(len - 2) = result;
                        }
                    } else {
                        let b = self.stack.pop().unwrap();
                        let a = self.stack.pop().unwrap();
                        self.stack.push(self.op_mod(a, b)?);
                    }
                }
                OpCode::Pow => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    if let (Value::Int(x), Value::Int(y)) = unsafe {
                        (self.stack.get_unchecked(len - 2), self.stack.get_unchecked(len - 1))
                    } {
                        let r = Value::Int((*x).wrapping_pow(*y as u32));
                        unsafe { self.stack.set_len(len - 1); *self.stack.get_unchecked_mut(len - 2) = r; }
                    } else {
                        let b = self.stack.pop().unwrap();
                        let a = self.stack.pop().unwrap();
                        self.stack.push(self.op_pow(a, b)?);
                    }
                }
                OpCode::Neg => {
                    let len = self.stack.len();
                    if len == 0 { return Err(runtime_err("stack underflow".into())); }
                    let top = unsafe { self.stack.get_unchecked_mut(len - 1) };
                    match top {
                        Value::Int(n) => *n = -*n,
                        Value::Float(f) => *f = -*f,
                        _ => return Err(runtime_err("cannot negate non-numeric value".into())),
                    }
                }

                // Comparison -- in-place Int fast-path
                OpCode::Eq => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    let a_ref = unsafe { self.stack.get_unchecked(len - 2) };
                    let b_ref = unsafe { self.stack.get_unchecked(len - 1) };
                    let result = match (a_ref, b_ref) {
                        (Value::Int(x), Value::Int(y)) => *x == *y,
                        _ => values_equal(a_ref, b_ref),
                    };
                    unsafe {
                        self.stack.set_len(len - 1);
                        *self.stack.get_unchecked_mut(len - 2) = Value::Bool(result);
                    }
                }
                OpCode::Ne => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    let a_ref = unsafe { self.stack.get_unchecked(len - 2) };
                    let b_ref = unsafe { self.stack.get_unchecked(len - 1) };
                    let result = match (a_ref, b_ref) {
                        (Value::Int(x), Value::Int(y)) => *x != *y,
                        _ => !values_equal(a_ref, b_ref),
                    };
                    unsafe {
                        self.stack.set_len(len - 1);
                        *self.stack.get_unchecked_mut(len - 2) = Value::Bool(result);
                    }
                }
                OpCode::Lt => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    let a_ref = unsafe { self.stack.get_unchecked(len - 2) };
                    let b_ref = unsafe { self.stack.get_unchecked(len - 1) };
                    let result = match (a_ref, b_ref) {
                        (Value::Int(x), Value::Int(y)) => *x < *y,
                        _ => self.compare_lt(a_ref, b_ref)?,
                    };
                    unsafe {
                        self.stack.set_len(len - 1);
                        *self.stack.get_unchecked_mut(len - 2) = Value::Bool(result);
                    }
                }
                OpCode::Gt => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    let a_ref = unsafe { self.stack.get_unchecked(len - 2) };
                    let b_ref = unsafe { self.stack.get_unchecked(len - 1) };
                    let result = match (a_ref, b_ref) {
                        (Value::Int(x), Value::Int(y)) => *x > *y,
                        _ => self.compare_lt(b_ref, a_ref)?,
                    };
                    unsafe {
                        self.stack.set_len(len - 1);
                        *self.stack.get_unchecked_mut(len - 2) = Value::Bool(result);
                    }
                }
                OpCode::Le => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    let a_ref = unsafe { self.stack.get_unchecked(len - 2) };
                    let b_ref = unsafe { self.stack.get_unchecked(len - 1) };
                    let result = match (a_ref, b_ref) {
                        (Value::Int(x), Value::Int(y)) => *x <= *y,
                        _ => self.compare_lt(a_ref, b_ref)? || values_equal(a_ref, b_ref),
                    };
                    unsafe {
                        self.stack.set_len(len - 1);
                        *self.stack.get_unchecked_mut(len - 2) = Value::Bool(result);
                    }
                }
                OpCode::Ge => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    let a_ref = unsafe { self.stack.get_unchecked(len - 2) };
                    let b_ref = unsafe { self.stack.get_unchecked(len - 1) };
                    let result = match (a_ref, b_ref) {
                        (Value::Int(x), Value::Int(y)) => *x >= *y,
                        _ => self.compare_lt(b_ref, a_ref)? || values_equal(a_ref, b_ref),
                    };
                    unsafe {
                        self.stack.set_len(len - 1);
                        *self.stack.get_unchecked_mut(len - 2) = Value::Bool(result);
                    }
                }

                // Logical / Bitwise
                OpCode::Not => {
                    let len = self.stack.len();
                    if len == 0 { return Err(runtime_err("stack underflow".into())); }
                    let top = unsafe { self.stack.get_unchecked(len - 1) };
                    let result = !is_truthy_fast!(top);
                    unsafe { *self.stack.get_unchecked_mut(len - 1) = Value::Bool(result); }
                }
                OpCode::BitAnd => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    if let (Value::Int(x), Value::Int(y)) = unsafe {
                        (self.stack.get_unchecked(len - 2), self.stack.get_unchecked(len - 1))
                    } {
                        let r = Value::Int(*x & *y);
                        unsafe { self.stack.set_len(len - 1); *self.stack.get_unchecked_mut(len - 2) = r; }
                    } else {
                        return Err(runtime_err("bitwise AND requires integers".into()));
                    }
                }
                OpCode::BitOr => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    if let (Value::Int(x), Value::Int(y)) = unsafe {
                        (self.stack.get_unchecked(len - 2), self.stack.get_unchecked(len - 1))
                    } {
                        let r = Value::Int(*x | *y);
                        unsafe { self.stack.set_len(len - 1); *self.stack.get_unchecked_mut(len - 2) = r; }
                    } else {
                        return Err(runtime_err("bitwise OR requires integers".into()));
                    }
                }
                OpCode::BitXor => {
                    let len = self.stack.len();
                    if len < 2 { return Err(runtime_err("stack underflow".into())); }
                    let (a, b) = unsafe {
                        (self.stack.get_unchecked(len - 2), self.stack.get_unchecked(len - 1))
                    };
                    let r = match (a, b) {
                        (Value::Int(x), Value::Int(y)) => Value::Int(*x ^ *y),
                        (Value::Bool(x), Value::Bool(y)) => Value::Bool(*x ^ *y),
                        _ => return Err(runtime_err("bitwise XOR requires integers or booleans".into())),
                    };
                    unsafe { self.stack.set_len(len - 1); *self.stack.get_unchecked_mut(len - 2) = r; }
                }
                OpCode::BitNot => {
                    let len = self.stack.len();
                    if len == 0 { return Err(runtime_err("stack underflow".into())); }
                    let top = unsafe { self.stack.get_unchecked_mut(len - 1) };
                    if let Value::Int(n) = top { *n = !*n; }
                    else { return Err(runtime_err("bitwise NOT requires integer".into())); }
                }

                // Control flow
                OpCode::Jump(target) => {
                    ip = *target;
                }
                OpCode::JumpIfFalse(target) => {
                    let len = self.stack.len();
                    if len == 0 { return Err(runtime_err("stack underflow".into())); }
                    let top = unsafe { self.stack.get_unchecked(len - 1) };
                    let truthy = match top {
                        Value::Bool(b) => *b,
                        Value::Int(n) => *n != 0,
                        _ => is_truthy(top),
                    };
                    unsafe { self.stack.set_len(len - 1); }
                    if !truthy { ip = *target; }
                }
                OpCode::JumpIfTrue(target) => {
                    let len = self.stack.len();
                    if len == 0 { return Err(runtime_err("stack underflow".into())); }
                    let top = unsafe { self.stack.get_unchecked(len - 1) };
                    let truthy = match top {
                        Value::Bool(b) => *b,
                        Value::Int(n) => *n != 0,
                        _ => is_truthy(top),
                    };
                    unsafe { self.stack.set_len(len - 1); }
                    if truthy { ip = *target; }
                }
                OpCode::CallFn(idx, argc) => {
                    let str_idx = *idx as usize;
                    let argc = *argc as usize;
                    let fn_idx = unsafe { *self.fn_index.get_unchecked(str_idx) };
                    if fn_idx != u32::MAX {
                        let fn_idx = fn_idx as usize;

                        // @memoize: check cache before execution
                        let is_memo = fn_idx < self.fn_is_memoize.len() && unsafe { *self.fn_is_memoize.get_unchecked(fn_idx) };
                        let mut memo_key: Option<(usize, u64)> = None;
                        if is_memo {
                            let stack_start = self.stack.len() - argc;
                            let args_hash = {
                                use std::hash::Hasher;
                                let mut h = std::collections::hash_map::DefaultHasher::new();
                                for i in stack_start..self.stack.len() {
                                    hash_value_fast(&self.stack[i], &mut h);
                                }
                                h.finish()
                            };
                            if let Some(cached) = self.memo_cache.get(&fn_idx).and_then(|m| m.get(&args_hash)) {
                                // Cache hit — pop args, push cached result
                                self.stack.truncate(stack_start);
                                self.stack.push(cached.clone());
                                continue;
                            }
                            memo_key = Some((fn_idx, args_hash));
                        }

                        let func = unsafe { self.fn_table.get_unchecked(fn_idx) };
                        let arity = func.arity;
                        if argc != arity {
                            return Err(runtime_err(format!(
                                "function expects {} arguments, got {}", arity, argc
                            )));
                        }
                        let local_count = func.local_count;
                        let new_local_base = self.locals.len();
                        let needed = new_local_base + local_count;
                        if needed > self.locals.len() {
                            self.locals.resize(needed, Value::Void);
                        }
                        // Move args from stack to locals via unsafe ptr copy
                        let stack_start = self.stack.len() - argc;
                        unsafe {
                            let src = self.stack.as_ptr().add(stack_start);
                            let dst = self.locals.as_mut_ptr().add(new_local_base);
                            std::ptr::copy_nonoverlapping(src, dst, argc);
                            self.stack.set_len(stack_start);
                        }

                        // FLAT DISPATCH: save return address, switch code segment
                        self.frames.push(CallFrame {
                            return_ip: ip,
                            return_code_idx: code_idx,
                            local_base: new_local_base,
                            memo_key,
                        });
                        code_idx = unsafe { *self.fn_code_idx.get_unchecked(fn_idx) };
                        reload_code!(self, code_idx);
                        ip = 0;
                        local_base = new_local_base;
                        continue; // restart loop with new code segment
                    } else {
                        // Fallback: name-based lookup (builtins, etc.)
                        let name = self.string_pool[str_idx].clone();
                        let result = self.call_function_legacy(&name, argc)?;
                        self.stack.push(result);
                    }
                }
                OpCode::Return => {
                    let val = self.stack.pop().unwrap_or(Value::Void);
                    if self.frames.is_empty() {
                        return Ok(val);
                    }
                    // FLAT DISPATCH: restore caller's code segment and IP
                    let frame = unsafe { self.frames.pop().unwrap_unchecked() };
                    // @memoize: cache result on return
                    if let Some((fn_idx, args_hash)) = frame.memo_key {
                        self.memo_cache.entry(fn_idx).or_default().insert(args_hash, val.clone());
                    }
                    self.locals.truncate(frame.local_base);
                    ip = frame.return_ip;
                    code_idx = frame.return_code_idx;
                    reload_code!(self, code_idx);
                    local_base = match self.frames.last() {
                        Some(f) => f.local_base,
                        None => 0,
                    };
                    self.stack.push(val);
                }

                // Print
                OpCode::Print(n) => {
                    let start = self.stack.len().saturating_sub(*n);
                    let mut parts = String::new();
                    for i in start..self.stack.len() {
                        if i > start {
                            parts.push(' ');
                        }
                        parts.push_str(&self.stack[i].to_string());
                    }
                    self.write_output(&parts);
                    self.stack.truncate(start);
                    self.stack.push(Value::Void);
                }
                OpCode::Println(n) => {
                    let start = self.stack.len().saturating_sub(*n);
                    let mut parts = String::new();
                    for i in start..self.stack.len() {
                        if i > start {
                            parts.push(' ');
                        }
                        parts.push_str(&self.stack[i].to_string());
                    }
                    self.writeln_output(&parts);
                    self.stack.truncate(start);
                    self.stack.push(Value::Void);
                }

                // Builtins
                OpCode::CallBuiltin(idx, argc) => {
                    let name = self.string_pool[*idx as usize].clone();
                    let result = self.call_builtin(&name, *argc as usize)?;
                    self.stack.push(result);
                }

                // Data structures
                OpCode::MakeArray(n) => {
                    let start = self.stack.len().saturating_sub(*n);
                    let items: Vec<Value> = self.stack.drain(start..).collect();
                    self.stack.push(Value::Array(items));
                }
                OpCode::Index => {
                    let idx_val = pop_fast!(self);
                    let arr = pop_fast!(self);
                    match (&arr, &idx_val) {
                        (Value::Array(a), Value::Int(i)) => {
                            let i = *i as usize;
                            if i >= a.len() {
                                return Err(runtime_err(format!("index {} out of bounds (len {})", i, a.len())));
                            }
                            self.stack.push(a[i].clone());
                        }
                        (Value::Str(s), Value::Int(i)) => {
                            let i = *i as usize;
                            match s.chars().nth(i) {
                                Some(c) => self.stack.push(Value::Char(c)),
                                None => return Err(runtime_err(format!("string index {} out of bounds", i))),
                            }
                        }
                        _ => return Err(runtime_err("invalid index operation".into())),
                    }
                }
                OpCode::IndexAssign => {
                    let val = pop_fast!(self);
                    let idx_val = pop_fast!(self);
                    let arr = pop_fast!(self);
                    match (arr, &idx_val) {
                        (Value::Array(mut a), Value::Int(i)) => {
                            let i = *i as usize;
                            if i >= a.len() {
                                return Err(runtime_err(format!("index {} out of bounds (len {})", i, a.len())));
                            }
                            a[i] = val;
                            self.stack.push(Value::Array(a));
                        }
                        _ => return Err(runtime_err("invalid index assignment".into())),
                    }
                }
                OpCode::MakeRange(inclusive) => {
                    let end = pop_fast!(self);
                    let start = pop_fast!(self);
                    match (&start, &end) {
                        (Value::Int(a), Value::Int(b)) => {
                            let end_val = if *inclusive { *b + 1 } else { *b };
                            let items: Vec<Value> = (*a..end_val).map(Value::Int).collect();
                            self.stack.push(Value::Array(items));
                        }
                        _ => return Err(runtime_err("range requires integers".into())),
                    }
                }

                OpCode::CallMethod(idx, argc) => {
                    let method_name = self.string_pool[*idx as usize].clone();
                    let argc = *argc as usize;
                    // Stack: [obj, arg0, arg1, ...argN-1]
                    let args_start = self.stack.len().saturating_sub(argc);
                    let args: Vec<Value> = self.stack.drain(args_start..).collect();
                    let obj = pop_fast!(self);
                    let result = self.call_method(obj, &method_name, args)?;
                    self.stack.push(result);
                }

            }
        }
    }

    // ---- Method calls ----

    fn call_method(&mut self, obj: Value, method: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match obj {
            Value::Str(ref s) => self.call_string_method(s, method, args),
            Value::Array(ref a) => self.call_array_method(a, method, args),
            Value::Map(ref m) => self.call_map_method(m, method, args),
            _ => Err(runtime_err(format!("no method .{}() on {:?}", method, obj))),
        }
    }

    fn call_string_method(&self, s: &str, method: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match method {
            "len" => Ok(Value::Int(s.len() as i64)),
            "contains" => {
                match args.first() {
                    Some(Value::Str(sub)) => Ok(Value::Bool(s.contains(sub.as_str()))),
                    _ => Err(runtime_err("contains() requires string argument".into())),
                }
            }
            "starts_with" => {
                match args.first() {
                    Some(Value::Str(prefix)) => Ok(Value::Bool(s.starts_with(prefix.as_str()))),
                    _ => Err(runtime_err("starts_with() requires string argument".into())),
                }
            }
            "ends_with" => {
                match args.first() {
                    Some(Value::Str(suffix)) => Ok(Value::Bool(s.ends_with(suffix.as_str()))),
                    _ => Err(runtime_err("ends_with() requires string argument".into())),
                }
            }
            "split" => {
                match args.first() {
                    Some(Value::Str(delim)) => {
                        let parts: Vec<Value> = s.split(delim.as_str()).map(|p| Value::Str(p.to_string())).collect();
                        Ok(Value::Array(parts))
                    }
                    _ => Err(runtime_err("split() requires string delimiter".into())),
                }
            }
            "trim" => Ok(Value::Str(s.trim().to_string())),
            "to_upper" => Ok(Value::Str(s.to_uppercase())),
            "to_lower" => Ok(Value::Str(s.to_lowercase())),
            "replace" => {
                match (args.first(), args.get(1)) {
                    (Some(Value::Str(old)), Some(Value::Str(new))) => Ok(Value::Str(s.replace(old.as_str(), new.as_str()))),
                    _ => Err(runtime_err("replace() requires 2 string arguments".into())),
                }
            }
            "chars" => {
                let chars: Vec<Value> = s.chars().map(Value::Char).collect();
                Ok(Value::Array(chars))
            }
            "substring" => {
                match (args.first(), args.get(1)) {
                    (Some(Value::Int(start)), Some(Value::Int(end))) => {
                        let start = *start as usize;
                        let end = (*end as usize).min(s.len());
                        Ok(Value::Str(s.get(start..end).unwrap_or("").to_string()))
                    }
                    _ => Err(runtime_err("substring() requires 2 int arguments".into())),
                }
            }
            _ => Err(runtime_err(format!("no method .{}() on string", method))),
        }
    }

    fn call_array_method(&mut self, arr: &[Value], method: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match method {
            "len" => Ok(Value::Int(arr.len() as i64)),
            "push" => {
                let mut new_arr = arr.to_vec();
                for a in args { new_arr.push(a); }
                Ok(Value::Array(new_arr))
            }
            "pop" => {
                let mut new_arr = arr.to_vec();
                let val = new_arr.pop().unwrap_or(Value::Void);
                // Return the popped value
                Ok(val)
            }
            "contains" => {
                match args.first() {
                    Some(val) => {
                        let found = arr.iter().any(|v| format!("{}", v) == format!("{}", val));
                        Ok(Value::Bool(found))
                    }
                    None => Err(runtime_err("contains() requires 1 argument".into())),
                }
            }
            "join" => {
                match args.first() {
                    Some(Value::Str(sep)) => {
                        let parts: Vec<String> = arr.iter().map(|v| format!("{}", v)).collect();
                        Ok(Value::Str(parts.join(sep)))
                    }
                    _ => Err(runtime_err("join() requires string separator".into())),
                }
            }
            "reverse" => {
                let mut new_arr = arr.to_vec();
                new_arr.reverse();
                Ok(Value::Array(new_arr))
            }
            "map" | "filter" | "for_each" => {
                // These require closure execution — bail to runtime error for now
                Err(runtime_err(format!("array.{}() not yet supported in VM", method)))
            }
            _ => Err(runtime_err(format!("no method .{}() on array", method))),
        }
    }

    fn call_map_method(&self, map: &HashMap<String, Value>, method: &str, args: Vec<Value>) -> Result<Value, HexaError> {
        match method {
            "len" => Ok(Value::Int(map.len() as i64)),
            "keys" => {
                let keys: Vec<Value> = map.keys().map(|k| Value::Str(k.clone())).collect();
                Ok(Value::Array(keys))
            }
            "values" => {
                let values: Vec<Value> = map.values().cloned().collect();
                Ok(Value::Array(values))
            }
            "contains_key" => {
                match args.first() {
                    Some(Value::Str(key)) => Ok(Value::Bool(map.contains_key(key))),
                    _ => Err(runtime_err("contains_key() requires string argument".into())),
                }
            }
            "get" => {
                match args.first() {
                    Some(Value::Str(key)) => Ok(map.get(key).cloned().unwrap_or(Value::Void)),
                    _ => Err(runtime_err("get() requires string argument".into())),
                }
            }
            _ => Err(runtime_err(format!("no method .{}() on map", method))),
        }
    }

    #[inline(always)]
    fn current_local_base(&self) -> usize {
        self.frames.last().map_or(0, |f| f.local_base)
    }

    fn pop(&mut self) -> Result<Value, HexaError> {
        self.stack.pop().ok_or_else(|| runtime_err("stack underflow".into()))
    }

    // ---- Function calls ----

    fn call_function_legacy(&mut self, name: &str, argc: usize) -> Result<Value, HexaError> {
        let func = self.functions.get(name).cloned().ok_or_else(|| {
            runtime_err(format!("undefined function: {}", name))
        })?;

        if argc != func.arity {
            return Err(runtime_err(format!(
                "function '{}' expects {} arguments, got {}", name, func.arity, argc
            )));
        }

        let new_local_base = self.locals.len();
        let needed = new_local_base + func.local_count;
        if needed > self.locals.len() {
            self.locals.resize(needed, Value::Void);
        }

        let stack_start = self.stack.len().saturating_sub(argc);
        for i in 0..argc {
            self.locals[new_local_base + i] = std::mem::replace(
                &mut self.stack[stack_start + i], Value::Void
            );
        }
        self.stack.truncate(stack_start);

        // Add code as a temporary segment and run flat
        let tmp_code_idx = self.code_segments.len();
        self.code_segments.push((*func.code).clone());

        self.frames.push(CallFrame {
            return_ip: 0, // not used — run_flat returns when frames empty
            return_code_idx: 0,
            local_base: new_local_base,
            memo_key: None,
        });

        let result = self.run_flat(tmp_code_idx)?;

        // Clean up temp segment
        if self.code_segments.len() > tmp_code_idx {
            self.code_segments.truncate(tmp_code_idx);
        }

        Ok(result)
    }

    // ---- Builtin calls ----

    fn call_builtin(&mut self, name: &str, argc: usize) -> Result<Value, HexaError> {
        let start = self.stack.len().saturating_sub(argc);
        let args: Vec<Value> = self.stack.drain(start..).collect();

        match name {
            "eprintln" => {
                let mut out = String::new();
                for (i, arg) in args.iter().enumerate() {
                    if i > 0 { out.push(' '); }
                    out.push_str(&arg.to_string());
                }
                if let Some(ref buf) = self.output_capture {
                    if let Ok(mut b) = buf.lock() {
                        b.push_str(&out);
                        b.push('\n');
                    }
                } else {
                    eprintln!("{}", out);
                }
                Ok(Value::Void)
            }
            "__assert__" => {
                if args.is_empty() {
                    return Err(runtime_err("assert requires an expression".into()));
                }
                if !is_truthy(&args[0]) {
                    return Err(HexaError {
                        class: ErrorClass::Logic,
                        message: "assertion failed".into(),
                        line: 0,
                        col: 0,
                    hint: None,
                    });
                }
                Ok(Value::Void)
            }
            "len" => {
                if args.is_empty() { return Err(runtime_err("len() requires 1 argument".into())); }
                match &args[0] {
                    Value::Str(s) => Ok(Value::Int(s.len() as i64)),
                    Value::Array(a) => Ok(Value::Int(a.len() as i64)),
                    _ => Err(runtime_err("len() requires string or array".into())),
                }
            }
            "type_of" => {
                if args.is_empty() { return Err(runtime_err("type_of() requires 1 argument".into())); }
                let type_name = match &args[0] {
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
                    Value::TraitObject(to) => to.type_name.as_str(),
                    #[cfg(not(target_arch = "wasm32"))]
                    Value::AsyncFuture(_) => "future",
                    Value::Atomic(_) => "atomic",
                    Value::Pointer(_) => "pointer",
                };
                Ok(Value::Str(type_name.to_string()))
            }
            "sigma" => {
                if args.is_empty() { return Err(runtime_err("sigma() requires 1 argument".into())); }
                if let Value::Int(n) = &args[0] {
                    let mut sum = 0i64;
                    for d in 1..=*n { if n % d == 0 { sum += d; } }
                    Ok(Value::Int(sum))
                } else { Err(runtime_err("sigma() requires int".into())) }
            }
            "phi" => {
                if args.is_empty() { return Err(runtime_err("phi() requires 1 argument".into())); }
                if let Value::Int(n) = &args[0] {
                    let mut count = 0i64;
                    for k in 1..=*n { if gcd(*n, k) == 1 { count += 1; } }
                    Ok(Value::Int(count))
                } else { Err(runtime_err("phi() requires int".into())) }
            }
            "tau" => {
                if args.is_empty() { return Err(runtime_err("tau() requires 1 argument".into())); }
                if let Value::Int(n) = &args[0] {
                    let mut count = 0i64;
                    for d in 1..=*n { if n % d == 0 { count += 1; } }
                    Ok(Value::Int(count))
                } else { Err(runtime_err("tau() requires int".into())) }
            }
            "gcd" => {
                if args.len() < 2 { return Err(runtime_err("gcd() requires 2 arguments".into())); }
                if let (Value::Int(a), Value::Int(b)) = (&args[0], &args[1]) {
                    Ok(Value::Int(gcd(*a, *b)))
                } else { Err(runtime_err("gcd() requires two ints".into())) }
            }
            "abs" => {
                if args.is_empty() { return Err(runtime_err("abs() requires 1 argument".into())); }
                match &args[0] {
                    Value::Int(n) => Ok(Value::Int(n.abs())),
                    Value::Float(f) => Ok(Value::Float(f.abs())),
                    _ => Err(runtime_err("abs() requires numeric".into())),
                }
            }
            "min" => {
                if args.len() < 2 { return Err(runtime_err("min() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(a), Value::Int(b)) => Ok(Value::Int(*a.min(b))),
                    (Value::Float(a), Value::Float(b)) => Ok(Value::Float(a.min(*b))),
                    (Value::Int(a), Value::Float(b)) => Ok(Value::Float((*a as f64).min(*b))),
                    (Value::Float(a), Value::Int(b)) => Ok(Value::Float(a.min(*b as f64))),
                    _ => Err(runtime_err("min() requires numeric".into())),
                }
            }
            "max" => {
                if args.len() < 2 { return Err(runtime_err("max() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(a), Value::Int(b)) => Ok(Value::Int(*a.max(b))),
                    (Value::Float(a), Value::Float(b)) => Ok(Value::Float(a.max(*b))),
                    (Value::Int(a), Value::Float(b)) => Ok(Value::Float((*a as f64).max(*b))),
                    (Value::Float(a), Value::Int(b)) => Ok(Value::Float(a.max(*b as f64))),
                    _ => Err(runtime_err("max() requires numeric".into())),
                }
            }
            "floor" => {
                if args.is_empty() { return Err(runtime_err("floor() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Int(f.floor() as i64)),
                    Value::Int(n) => Ok(Value::Int(*n)),
                    _ => Err(runtime_err("floor() requires float or int".into())),
                }
            }
            "ceil" => {
                if args.is_empty() { return Err(runtime_err("ceil() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Int(f.ceil() as i64)),
                    Value::Int(n) => Ok(Value::Int(*n)),
                    _ => Err(runtime_err("ceil() requires float or int".into())),
                }
            }
            "round" => {
                if args.is_empty() { return Err(runtime_err("round() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Int(f.round() as i64)),
                    Value::Int(n) => Ok(Value::Int(*n)),
                    _ => Err(runtime_err("round() requires float or int".into())),
                }
            }
            "sqrt" => {
                if args.is_empty() { return Err(runtime_err("sqrt() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.sqrt())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).sqrt())),
                    _ => Err(runtime_err("sqrt() requires numeric".into())),
                }
            }
            "pow" => {
                if args.len() < 2 { return Err(runtime_err("pow() requires 2 arguments".into())); }
                match (&args[0], &args[1]) {
                    (Value::Int(a), Value::Int(b)) => Ok(Value::Int((*a as f64).powi(*b as i32) as i64)),
                    (Value::Float(a), Value::Float(b)) => Ok(Value::Float(a.powf(*b))),
                    (Value::Int(a), Value::Float(b)) => Ok(Value::Float((*a as f64).powf(*b))),
                    (Value::Float(a), Value::Int(b)) => Ok(Value::Float(a.powi(*b as i32))),
                    _ => Err(runtime_err("pow() requires numeric".into())),
                }
            }
            "log" => {
                if args.is_empty() { return Err(runtime_err("log() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.ln())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).ln())),
                    _ => Err(runtime_err("log() requires numeric".into())),
                }
            }
            "log2" => {
                if args.is_empty() { return Err(runtime_err("log2() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.log2())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).log2())),
                    _ => Err(runtime_err("log2() requires numeric".into())),
                }
            }
            "log10" => {
                if args.is_empty() { return Err(runtime_err("log10() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.log10())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).log10())),
                    _ => Err(runtime_err("log10() requires numeric".into())),
                }
            }
            "ln" => {
                if args.is_empty() { return Err(runtime_err("ln() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.ln())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).ln())),
                    _ => Err(runtime_err("ln() requires numeric".into())),
                }
            }
            "exp" => {
                if args.is_empty() { return Err(runtime_err("exp() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.exp())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).exp())),
                    _ => Err(runtime_err("exp() requires numeric".into())),
                }
            }
            "sin" => {
                if args.is_empty() { return Err(runtime_err("sin() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.sin())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).sin())),
                    _ => Err(runtime_err("sin() requires numeric".into())),
                }
            }
            "cos" => {
                if args.is_empty() { return Err(runtime_err("cos() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.cos())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).cos())),
                    _ => Err(runtime_err("cos() requires numeric".into())),
                }
            }
            "tan" => {
                if args.is_empty() { return Err(runtime_err("tan() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(f.tan())),
                    Value::Int(n) => Ok(Value::Float((*n as f64).tan())),
                    _ => Err(runtime_err("tan() requires numeric".into())),
                }
            }
            "to_string" => {
                if args.is_empty() { return Err(runtime_err("to_string() requires 1 argument".into())); }
                Ok(Value::Str(format!("{}", args[0])))
            }
            "to_int" => {
                if args.is_empty() { return Err(runtime_err("to_int() requires 1 argument".into())); }
                match &args[0] {
                    Value::Int(n) => Ok(Value::Int(*n)),
                    Value::Float(f) => Ok(Value::Int(*f as i64)),
                    Value::Str(s) => s.parse::<i64>().map(Value::Int)
                        .map_err(|_| runtime_err(format!("cannot convert '{}' to int", s))),
                    Value::Bool(b) => Ok(Value::Int(if *b { 1 } else { 0 })),
                    _ => Err(runtime_err("to_int() cannot convert this type".into())),
                }
            }
            "to_float" => {
                if args.is_empty() { return Err(runtime_err("to_float() requires 1 argument".into())); }
                match &args[0] {
                    Value::Float(f) => Ok(Value::Float(*f)),
                    Value::Int(n) => Ok(Value::Float(*n as f64)),
                    Value::Str(s) => s.parse::<f64>().map(Value::Float)
                        .map_err(|_| runtime_err(format!("cannot convert '{}' to float", s))),
                    Value::Bool(b) => Ok(Value::Float(if *b { 1.0 } else { 0.0 })),
                    _ => Err(runtime_err("to_float() cannot convert this type".into())),
                }
            }
            "format" => {
                if args.is_empty() { return Err(runtime_err("format() requires at least 1 argument".into())); }
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
                    _ => Err(runtime_err("format() first argument must be string".into())),
                }
            }
            "clock" => {
                use std::time::{SystemTime, UNIX_EPOCH};
                let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
                Ok(Value::Float(now.as_secs_f64()))
            }
            _ => Err(runtime_err(format!("unknown builtin: {}", name))),
        }
    }

    // ---- Arithmetic helpers (cold path — only hit for non-Int types) ----

    #[cold]
    fn op_add(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int(x.wrapping_add(y))),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x + y)),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float(x as f64 + y)),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x + y as f64)),
            (Value::Str(x), Value::Str(y)) => Ok(Value::Str(format!("{}{}", x, y))),
            (Value::Array(mut x), Value::Array(y)) => {
                x.extend(y.into_iter());
                Ok(Value::Array(x))
            }
            (a, b) => Err(runtime_err(format!("cannot add {:?} and {:?}", a, b))),
        }
    }

    #[cold]
    fn op_sub(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int(x.wrapping_sub(y))),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x - y)),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float(x as f64 - y)),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x - y as f64)),
            (a, b) => Err(runtime_err(format!("cannot subtract {:?} from {:?}", b, a))),
        }
    }

    #[cold]
    fn op_mul(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int(x.wrapping_mul(y))),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x * y)),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float(x as f64 * y)),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x * y as f64)),
            (a, b) => Err(runtime_err(format!("cannot multiply {:?} and {:?}", a, b))),
        }
    }

    #[cold]
    fn op_div(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(_), Value::Int(0)) => Err(runtime_err("division by zero".into())),
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int(x / y)),
            (Value::Float(_), Value::Float(y)) if y == 0.0 => Err(runtime_err("division by zero".into())),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x / y)),
            (Value::Int(x), Value::Float(y)) => {
                if y == 0.0 { return Err(runtime_err("division by zero".into())); }
                Ok(Value::Float(x as f64 / y))
            }
            (Value::Float(x), Value::Int(y)) => {
                if y == 0 { return Err(runtime_err("division by zero".into())); }
                Ok(Value::Float(x / y as f64))
            }
            (a, b) => Err(runtime_err(format!("cannot divide {:?} by {:?}", a, b))),
        }
    }

    #[cold]
    fn op_mod(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(_), Value::Int(0)) => Err(runtime_err("division by zero".into())),
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int(x % y)),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x % y)),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float(x as f64 % y)),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x % y as f64)),
            (a, b) => Err(runtime_err(format!("cannot modulo {:?} by {:?}", a, b))),
        }
    }

    #[cold]
    fn op_pow(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int((x as f64).powi(y as i32) as i64)),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x.powf(y))),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float((x as f64).powf(y))),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x.powi(y as i32))),
            (a, b) => Err(runtime_err(format!("cannot pow {:?} by {:?}", a, b))),
        }
    }

    #[cold]
    fn compare_lt(&self, a: &Value, b: &Value) -> Result<bool, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(x < y),
            (Value::Float(x), Value::Float(y)) => Ok(x < y),
            (Value::Int(x), Value::Float(y)) => Ok((*x as f64) < *y),
            (Value::Float(x), Value::Int(y)) => Ok(*x < (*y as f64)),
            (Value::Str(x), Value::Str(y)) => Ok(x < y),
            _ => Err(runtime_err("cannot compare these types".into())),
        }
    }
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

#[cold]
fn values_equal(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Int(x), Value::Int(y)) => x == y,
        (Value::Float(x), Value::Float(y)) => x == y,
        (Value::Bool(x), Value::Bool(y)) => x == y,
        (Value::Str(x), Value::Str(y)) => x == y,
        (Value::Char(x), Value::Char(y)) => x == y,
        (Value::Void, Value::Void) => true,
        // Cross-type int/float comparison
        (Value::Int(x), Value::Float(y)) => (*x as f64) == *y,
        (Value::Float(x), Value::Int(y)) => *x == (*y as f64),
        _ => false,
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

#[cold]
#[inline(never)]
fn runtime_err(msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: msg,
        line: 0,
        col: 0,
    hint: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::compiler::Compiler;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn vm_eval(src: &str) -> Value {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut compiler = Compiler::new();
        let chunk = compiler.compile(&stmts).unwrap();
        let mut vm = VM::new();
        vm.execute(&chunk).unwrap()
    }

    #[test]
    fn test_vm_arithmetic() {
        assert!(matches!(vm_eval("1 + 2 * 3"), Value::Int(7)));
    }

    #[test]
    fn test_vm_let_and_use() {
        assert!(matches!(vm_eval("let x = 6\nlet y = 4\nx * y"), Value::Int(24)));
    }

    #[test]
    fn test_vm_function_call() {
        let src = "fn double(x) {\n  return x * 2\n}\ndouble(6)";
        assert!(matches!(vm_eval(src), Value::Int(12)));
    }

    #[test]
    fn test_vm_recursion() {
        let src = "fn fact(n) {\n  if n <= 1 { return 1 }\n  return n * fact(n - 1)\n}\nfact(6)";
        assert!(matches!(vm_eval(src), Value::Int(720)));
    }

    #[test]
    fn test_vm_fibonacci() {
        let src = r#"
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(10)
"#;
        assert!(matches!(vm_eval(src), Value::Int(55)));
    }

    #[test]
    fn test_vm_while_loop() {
        let src = "let mut n = 0\nlet mut i = 1\nwhile i <= 6 {\n  n = n + i\n  i = i + 1\n}\nn";
        assert!(matches!(vm_eval(src), Value::Int(21)));
    }

    #[test]
    fn test_vm_for_loop() {
        let src = "let mut s = 0\nfor i in 1..7 {\n  s = s + i\n}\ns";
        assert!(matches!(vm_eval(src), Value::Int(21)));
    }

    #[test]
    fn test_vm_if_else() {
        assert!(matches!(vm_eval("if 6 > 5 { 12 } else { 0 }"), Value::Int(12)));
        assert!(matches!(vm_eval("if 3 > 5 { 12 } else { 0 }"), Value::Int(0)));
    }

    #[test]
    fn test_vm_string_concat() {
        let result = vm_eval("\"hello\" + \" \" + \"world\"");
        assert!(matches!(result, Value::Str(s) if s == "hello world"));
    }

    #[test]
    fn test_vm_array() {
        let result = vm_eval("[1, 2, 3][1]");
        assert!(matches!(result, Value::Int(2)));
    }

    #[test]
    fn test_vm_comparison() {
        assert!(matches!(vm_eval("6 == 6"), Value::Bool(true)));
        assert!(matches!(vm_eval("6 != 7"), Value::Bool(true)));
        assert!(matches!(vm_eval("12 > 4"), Value::Bool(true)));
    }

    #[test]
    fn test_vm_boolean_ops() {
        assert!(matches!(vm_eval("true && false"), Value::Bool(false)));
        assert!(matches!(vm_eval("true || false"), Value::Bool(true)));
    }

    #[test]
    fn test_vm_unary() {
        assert!(matches!(vm_eval("-6"), Value::Int(-6)));
        assert!(matches!(vm_eval("!true"), Value::Bool(false)));
    }

    #[test]
    fn test_vm_sigma_builtin() {
        assert!(matches!(vm_eval("sigma(6)"), Value::Int(12)));
    }

    #[test]
    fn test_vm_nested_function() {
        let src = r#"
fn outer(x) {
    fn inner(y) {
        return y * 2
    }
    return inner(x) + 1
}
outer(5)
"#;
        assert!(matches!(vm_eval(src), Value::Int(11)));
    }

    #[test]
    fn test_vm_array_in_loop() {
        let src = r#"
let mut arr = [0, 0, 0]
for i in 0..3 {
    arr[i] = i * 10
}
arr[2]
"#;
        assert!(matches!(vm_eval(src), Value::Int(20)));
    }

    // ---- Optimization tests ----

    fn vm_eval_optimized(src: &str) -> Value {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut compiler = Compiler::new();
        let chunk = compiler.compile(&stmts).unwrap();
        let mut vm = VM::new();
        vm.execute_optimized(&chunk).unwrap()
    }

    #[test]
    fn test_vm_optimized_fib() {
        let src = r#"
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(10)
"#;
        assert!(matches!(vm_eval_optimized(src), Value::Int(55)));
    }

    #[test]
    fn test_vm_const_folded_arithmetic() {
        // Constant folding: 3 + 4 * 5 is parsed as 3 + (4*5)
        // 4*5 = 20 (folded), then 3+20 = 23 (folded)
        assert!(matches!(vm_eval("3 + 4 * 5"), Value::Int(23)));
    }

    #[test]
    fn test_vm_const_folded_string() {
        let result = vm_eval("\"hello\" + \" world\"");
        assert!(matches!(result, Value::Str(s) if s == "hello world"));
    }

    #[test]
    fn test_vm_const_folded_comparison() {
        assert!(matches!(vm_eval("10 > 5"), Value::Bool(true)));
        assert!(matches!(vm_eval("3 == 3"), Value::Bool(true)));
    }

    #[test]
    fn test_vm_optimized_loop() {
        let src = r#"
let mut s = 0
let mut i = 1
while i <= 100 {
    s = s + i
    i = i + 1
}
s
"#;
        assert!(matches!(vm_eval_optimized(src), Value::Int(5050)));
    }

    #[test]
    fn test_vm_nanbox_value_roundtrip() {
        // Verify NaN-boxing roundtrip through Value conversion
        use crate::nanbox::NanBoxed;

        let values = vec![
            Value::Int(42),
            Value::Int(-100),
            Value::Int(0),
            Value::Float(3.14),
            Value::Float(0.0),
            Value::Bool(true),
            Value::Bool(false),
            Value::Void,
            Value::Str("hello".into()),
            Value::Array(vec![Value::Int(1), Value::Int(2)]),
        ];

        for val in &values {
            let boxed = NanBoxed::from_value(val);
            let recovered = boxed.to_value();
            assert_eq!(val.to_string(), recovered.to_string(),
                "roundtrip failed for {:?}", val);
        }
    }

    #[test]
    fn test_vm_stack_preallocation() {
        // Verify VM pre-allocates stack capacity
        let vm = VM::new();
        assert!(vm.stack.capacity() >= 256);
    }
}

#[test]
fn _tmp_opcode_size() {
    eprintln!("OpCode: {} bytes", std::mem::size_of::<crate::compiler::OpCode>());
    eprintln!("Value: {} bytes", std::mem::size_of::<Value>());
}

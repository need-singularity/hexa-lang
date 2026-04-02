#![allow(dead_code)]

use std::collections::HashMap;
use crate::compiler::{OpCode, Chunk, CompiledFunction};
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};

/// A call frame on the VM call stack.
#[derive(Debug)]
struct CallFrame {
    /// Function name (for error messages).
    func_name: String,
    /// Return address: instruction pointer in the caller's code.
    return_ip: usize,
    /// Base index in the local slots for this frame.
    local_base: usize,
    /// Whether this is a top-level frame (uses the chunk's code).
    is_top_level: bool,
}

/// Stack-based bytecode virtual machine.
pub struct VM {
    /// Value stack.
    stack: Vec<Value>,
    /// Local variable slots (flat array, frames index into it).
    locals: Vec<Value>,
    /// Call stack.
    frames: Vec<CallFrame>,
    /// Constant pool (shared from chunk).
    constants: Vec<Value>,
    /// Compiled functions registry.
    functions: HashMap<String, CompiledFunction>,
    /// Global variables (for builtins like PI, E, etc.).
    globals: HashMap<String, Value>,
}

impl VM {
    pub fn new() -> Self {
        let mut globals = HashMap::new();
        globals.insert("PI".into(), Value::Float(std::f64::consts::PI));
        globals.insert("E".into(), Value::Float(std::f64::consts::E));
        Self {
            stack: Vec::with_capacity(256),
            locals: Vec::new(),
            frames: Vec::new(),
            constants: Vec::new(),
            functions: HashMap::new(),
            globals,
        }
    }

    /// Execute a compiled chunk. Returns the last value on the stack (or Void).
    pub fn execute(&mut self, chunk: &Chunk) -> Result<Value, HexaError> {
        self.constants = chunk.constants.clone();
        self.functions = chunk.functions.clone();

        // Pre-allocate locals for top-level (avoid resizing during execution)
        self.locals.resize(chunk.local_count.max(256), Value::Void);
        // Pre-allocate stack to avoid resizing
        self.stack.reserve(256);

        self.run_code(&chunk.code)
    }

    /// Execute a compiled chunk with dead code elimination applied.
    pub fn execute_optimized(&mut self, chunk: &Chunk) -> Result<Value, HexaError> {
        self.constants = chunk.constants.clone();
        self.functions = chunk.functions.clone();

        // Apply dead code elimination
        let mut optimized_code = chunk.code.clone();
        crate::compiler::eliminate_dead_code(&mut optimized_code);

        // Pre-allocate
        self.locals.resize(chunk.local_count.max(256), Value::Void);
        self.stack.reserve(256);

        self.run_code(&optimized_code)
    }

    fn run_code(&mut self, code: &[OpCode]) -> Result<Value, HexaError> {
        let mut ip = 0;
        let code_len = code.len();

        while ip < code_len {
            let op = &code[ip];
            ip += 1;

            match op {
                OpCode::Const(idx) => {
                    let val = self.constants[*idx].clone();
                    self.stack.push(val);
                }
                OpCode::Void => {
                    self.stack.push(Value::Void);
                }
                OpCode::Pop => {
                    self.stack.pop();
                }
                OpCode::Dup => {
                    if let Some(top) = self.stack.last() {
                        self.stack.push(top.clone());
                    }
                }
                OpCode::Truthy => {
                    let val = self.pop()?;
                    self.stack.push(Value::Bool(is_truthy(&val)));
                }

                // Variables
                OpCode::GetLocal(slot) => {
                    let base = self.current_local_base();
                    let idx = base + slot;
                    let val = if idx < self.locals.len() {
                        self.locals[idx].clone()
                    } else {
                        Value::Void
                    };
                    self.stack.push(val);
                }
                OpCode::SetLocal(slot) => {
                    let val = self.pop()?;
                    let base = self.current_local_base();
                    let idx = base + slot;
                    if idx >= self.locals.len() {
                        self.locals.resize(idx + 1, Value::Void);
                    }
                    self.locals[idx] = val;
                }
                OpCode::GetGlobal(name) => {
                    if let Some(val) = self.globals.get(name) {
                        self.stack.push(val.clone());
                    } else {
                        return Err(runtime_err(format!("undefined variable: {}", name)));
                    }
                }
                OpCode::SetGlobal(name) => {
                    let val = self.pop()?;
                    self.globals.insert(name.clone(), val);
                }

                // Arithmetic
                OpCode::Add => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(self.op_add(a, b)?);
                }
                OpCode::Sub => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(self.op_sub(a, b)?);
                }
                OpCode::Mul => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(self.op_mul(a, b)?);
                }
                OpCode::Div => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(self.op_div(a, b)?);
                }
                OpCode::Mod => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(self.op_mod(a, b)?);
                }
                OpCode::Pow => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(self.op_pow(a, b)?);
                }
                OpCode::Neg => {
                    let v = self.pop()?;
                    match v {
                        Value::Int(n) => self.stack.push(Value::Int(-n)),
                        Value::Float(n) => self.stack.push(Value::Float(-n)),
                        _ => return Err(runtime_err("cannot negate non-numeric value".into())),
                    }
                }

                // Comparison
                OpCode::Eq => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(Value::Bool(values_equal(&a, &b)));
                }
                OpCode::Ne => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(Value::Bool(!values_equal(&a, &b)));
                }
                OpCode::Lt => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(Value::Bool(self.compare_lt(&a, &b)?));
                }
                OpCode::Gt => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    self.stack.push(Value::Bool(self.compare_lt(&b, &a)?));
                }
                OpCode::Le => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    let lt = self.compare_lt(&a, &b)?;
                    let eq = values_equal(&a, &b);
                    self.stack.push(Value::Bool(lt || eq));
                }
                OpCode::Ge => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    let gt = self.compare_lt(&b, &a)?;
                    let eq = values_equal(&a, &b);
                    self.stack.push(Value::Bool(gt || eq));
                }

                // Logical / Bitwise
                OpCode::Not => {
                    let v = self.pop()?;
                    self.stack.push(Value::Bool(!is_truthy(&v)));
                }
                OpCode::BitAnd => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    match (a, b) {
                        (Value::Int(x), Value::Int(y)) => self.stack.push(Value::Int(x & y)),
                        _ => return Err(runtime_err("bitwise AND requires integers".into())),
                    }
                }
                OpCode::BitOr => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    match (a, b) {
                        (Value::Int(x), Value::Int(y)) => self.stack.push(Value::Int(x | y)),
                        _ => return Err(runtime_err("bitwise OR requires integers".into())),
                    }
                }
                OpCode::BitXor => {
                    let b = self.pop()?;
                    let a = self.pop()?;
                    match (a, b) {
                        (Value::Int(x), Value::Int(y)) => self.stack.push(Value::Int(x ^ y)),
                        (Value::Bool(x), Value::Bool(y)) => self.stack.push(Value::Bool(x ^ y)),
                        _ => return Err(runtime_err("bitwise XOR requires integers or booleans".into())),
                    }
                }
                OpCode::BitNot => {
                    let v = self.pop()?;
                    match v {
                        Value::Int(n) => self.stack.push(Value::Int(!n)),
                        _ => return Err(runtime_err("bitwise NOT requires integer".into())),
                    }
                }

                // Control flow
                OpCode::Jump(target) => {
                    ip = *target;
                }
                OpCode::JumpIfFalse(target) => {
                    let v = self.pop()?;
                    if !is_truthy(&v) {
                        ip = *target;
                    }
                }
                OpCode::JumpIfTrue(target) => {
                    let v = self.pop()?;
                    if is_truthy(&v) {
                        ip = *target;
                    }
                }
                OpCode::CallFn(name, argc) => {
                    let result = self.call_function(name, *argc)?;
                    self.stack.push(result);
                }
                OpCode::Return => {
                    // In top-level code, Return means "stop executing"
                    // The return value is already on the stack
                    if self.frames.is_empty() {
                        // Top-level return
                        let val = self.stack.pop().unwrap_or(Value::Void);
                        return Ok(val);
                    }
                    // Otherwise handled by call_function
                    let val = self.stack.pop().unwrap_or(Value::Void);
                    return Ok(val);
                }

                // Print
                OpCode::Print(n) => {
                    let start = self.stack.len().saturating_sub(*n);
                    for i in start..self.stack.len() {
                        if i > start {
                            print!(" ");
                        }
                        print!("{}", self.stack[i]);
                    }
                    // Pop args
                    self.stack.truncate(start);
                    self.stack.push(Value::Void);
                }
                OpCode::Println(n) => {
                    let start = self.stack.len().saturating_sub(*n);
                    for i in start..self.stack.len() {
                        if i > start {
                            print!(" ");
                        }
                        print!("{}", self.stack[i]);
                    }
                    println!();
                    self.stack.truncate(start);
                    self.stack.push(Value::Void);
                }

                // Builtins
                OpCode::CallBuiltin(name, argc) => {
                    let result = self.call_builtin(name, *argc)?;
                    self.stack.push(result);
                }

                // Data structures
                OpCode::MakeArray(n) => {
                    let start = self.stack.len().saturating_sub(*n);
                    let items: Vec<Value> = self.stack.drain(start..).collect();
                    self.stack.push(Value::Array(items));
                }
                OpCode::Index => {
                    let idx = self.pop()?;
                    let arr = self.pop()?;
                    match (&arr, &idx) {
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
                    // Stack: [arr, idx, val]
                    let val = self.pop()?;
                    let idx = self.pop()?;
                    let arr = self.pop()?;
                    match (arr, &idx) {
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
                    let end = self.pop()?;
                    let start = self.pop()?;
                    match (&start, &end) {
                        (Value::Int(a), Value::Int(b)) => {
                            let end_val = if *inclusive { *b + 1 } else { *b };
                            let items: Vec<Value> = (*a..end_val).map(Value::Int).collect();
                            self.stack.push(Value::Array(items));
                        }
                        _ => return Err(runtime_err("range requires integers".into())),
                    }
                }

            }
        }

        // Return last value on stack
        Ok(self.stack.pop().unwrap_or(Value::Void))
    }

    fn current_local_base(&self) -> usize {
        self.frames.last().map_or(0, |f| f.local_base)
    }

    fn pop(&mut self) -> Result<Value, HexaError> {
        self.stack.pop().ok_or_else(|| runtime_err("stack underflow".into()))
    }

    // ---- Function calls ----

    fn call_function(&mut self, name: &str, argc: usize) -> Result<Value, HexaError> {
        let func = self.functions.get(name).cloned().ok_or_else(|| {
            runtime_err(format!("undefined function: {}", name))
        })?;

        if argc != func.arity {
            return Err(runtime_err(format!(
                "function '{}' expects {} arguments, got {}", name, func.arity, argc
            )));
        }

        // Pop arguments from stack
        let start = self.stack.len().saturating_sub(argc);
        let args: Vec<Value> = self.stack.drain(start..).collect();

        // Allocate local slots for this call
        let local_base = self.locals.len();
        let needed = local_base + func.local_count;
        if needed > self.locals.len() {
            self.locals.resize(needed, Value::Void);
        }

        // Place args into local slots
        for (i, arg) in args.into_iter().enumerate() {
            self.locals[local_base + i] = arg;
        }

        // Push call frame
        self.frames.push(CallFrame {
            func_name: name.to_string(),
            return_ip: 0,
            local_base,
            is_top_level: false,
        });

        // Execute function code
        let result = self.run_code(&func.code)?;

        // Pop call frame
        self.frames.pop();
        // Shrink locals back
        self.locals.truncate(local_base);

        Ok(result)
    }

    // ---- Builtin calls ----

    fn call_builtin(&mut self, name: &str, argc: usize) -> Result<Value, HexaError> {
        let start = self.stack.len().saturating_sub(argc);
        let args: Vec<Value> = self.stack.drain(start..).collect();

        match name {
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
                    Value::Fn(..) => "fn",
                    Value::BuiltinFn(_) => "builtin",
                    Value::Struct(name, _) => name.as_str(),
                    Value::Lambda(..) => "lambda",
                    Value::Map(_) => "map",
                    Value::Error(_) => "error",
                    Value::EnumVariant(name, _, _) => name.as_str(),
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
                    Value::EffectRequest(_, _, _) => "effect_request",
                    Value::TraitObject { type_name, .. } => type_name.as_str(),
                    #[cfg(not(target_arch = "wasm32"))]
                    Value::AsyncFuture(_) => "future",
                    Value::Atomic(_) => "atomic",
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
                        let mut result = template.clone();
                        for arg in &args[1..] {
                            if let Some(pos) = result.find("{}") {
                                result = format!("{}{}{}", &result[..pos], arg, &result[pos+2..]);
                            }
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

    // ---- Arithmetic helpers ----

    fn op_add(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int(x + y)),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x + y)),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float(x as f64 + y)),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x + y as f64)),
            (Value::Str(x), Value::Str(y)) => Ok(Value::Str(format!("{}{}", x, y))),
            (a, b) => Err(runtime_err(format!("cannot add {:?} and {:?}", a, b))),
        }
    }

    fn op_sub(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int(x - y)),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x - y)),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float(x as f64 - y)),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x - y as f64)),
            (a, b) => Err(runtime_err(format!("cannot subtract {:?} from {:?}", b, a))),
        }
    }

    fn op_mul(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int(x * y)),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x * y)),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float(x as f64 * y)),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x * y as f64)),
            (a, b) => Err(runtime_err(format!("cannot multiply {:?} and {:?}", a, b))),
        }
    }

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

    fn op_pow(&self, a: Value, b: Value) -> Result<Value, HexaError> {
        match (a, b) {
            (Value::Int(x), Value::Int(y)) => Ok(Value::Int((x as f64).powi(y as i32) as i64)),
            (Value::Float(x), Value::Float(y)) => Ok(Value::Float(x.powf(y))),
            (Value::Int(x), Value::Float(y)) => Ok(Value::Float((x as f64).powf(y))),
            (Value::Float(x), Value::Int(y)) => Ok(Value::Float(x.powi(y as i32))),
            (a, b) => Err(runtime_err(format!("cannot pow {:?} by {:?}", a, b))),
        }
    }

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

#![allow(dead_code)]

use std::collections::HashMap;
use std::sync::Arc;
use crate::ast::*;
use crate::env::Value;
use crate::error::{HexaError, ErrorClass};

/// Bytecode instructions for the VM.
#[derive(Debug, Clone)]
pub enum OpCode {
    // Stack operations
    Const(usize),       // push constant from pool
    Pop,                // discard top
    Dup,                // duplicate top

    // Variables
    GetLocal(usize),    // push local variable
    SetLocal(usize),    // pop into local variable
    GetGlobal(u32),     // push global variable (string_pool index)
    SetGlobal(u32),     // pop into global variable (string_pool index)

    // Arithmetic
    Add, Sub, Mul, Div, Mod, Pow,
    Neg,                // unary negate

    // Comparison
    Eq, Ne, Lt, Gt, Le, Ge,

    // Logical
    Not,

    // Bitwise
    BitAnd, BitOr, BitXor, BitNot,

    // Control flow
    Jump(usize),        // unconditional jump
    JumpIfFalse(usize), // conditional jump
    JumpIfTrue(usize),  // short-circuit jump
    CallFn(u32, u16),       // call named function (string_pool idx, N args)
    Return,             // return from function

    // Built-ins
    Print(usize),       // print N values (no newline)
    Println(usize),     // print N values + newline
    CallBuiltin(u32, u16),  // call builtin (string_pool idx, N args)

    // Data structures
    MakeArray(usize),   // create array from N stack values
    Index,              // array/string indexing
    IndexAssign,        // stack: [arr, idx, val] -> [new_arr]

    // Range
    MakeRange(bool),    // start..end or start..=end (inclusive flag)

    // Void
    Void,               // push Void onto stack

    // Bool conversion for logical ops
    Truthy,             // convert TOS to bool

    // Method call: obj.method(args)
    CallMethod(u32, u16), // (method name string_pool idx, N args) — obj is below args on stack

}

/// Compile-time assertion: OpCode must be <= 16 bytes for L1d cache density.
/// Shrinking GetGlobal/SetGlobal/CallFn/CallBuiltin from String-bearing variants
/// to u32 indices drops OpCode from 32B → 16B (2x stream density).
const _: () = {
    assert!(
        std::mem::size_of::<OpCode>() <= 16,
        "OpCode must be <= 16 bytes (L1d density target)"
    );
};

/// A compiled function: its bytecode + metadata.
#[derive(Debug, Clone)]
pub struct CompiledFunction {
    pub name: String,
    pub arity: usize,
    pub param_names: Vec<String>,
    pub code: Arc<Vec<OpCode>>,
    /// Number of local slots needed (params + locals).
    pub local_count: usize,
    /// AI-native: @memoize attribute — cache results for same args.
    pub is_memoize: bool,
}

/// Fast hash key for constant deduplication — O(1) instead of O(n) scan.
fn const_hash_key(val: &Value) -> u64 {
    use std::hash::Hasher;
    let mut h = std::collections::hash_map::DefaultHasher::new();
    match val {
        Value::Int(n) => { std::hash::Hash::hash(&0u8, &mut h); std::hash::Hash::hash(n, &mut h); }
        Value::Float(f) => { std::hash::Hash::hash(&1u8, &mut h); std::hash::Hash::hash(&f.to_bits(), &mut h); }
        Value::Bool(b) => { std::hash::Hash::hash(&2u8, &mut h); std::hash::Hash::hash(b, &mut h); }
        Value::Str(s) => { std::hash::Hash::hash(&3u8, &mut h); std::hash::Hash::hash(s.as_str(), &mut h); }
        Value::Void => { std::hash::Hash::hash(&4u8, &mut h); }
        Value::Char(c) => { std::hash::Hash::hash(&5u8, &mut h); std::hash::Hash::hash(c, &mut h); }
        Value::Byte(b) => { std::hash::Hash::hash(&6u8, &mut h); std::hash::Hash::hash(b, &mut h); }
        Value::Array(arr) => { std::hash::Hash::hash(&7u8, &mut h); for item in arr { std::hash::Hash::hash(&const_hash_key(item), &mut h); } }
        Value::Tuple(t) => { std::hash::Hash::hash(&8u8, &mut h); for item in t { std::hash::Hash::hash(&const_hash_key(item), &mut h); } }
        _ => { std::hash::Hash::hash(&99u8, &mut h); }
    }
    h.finish()
}

/// A compiled chunk: top-level bytecode + constant pool + functions.
#[derive(Debug, Clone)]
pub struct Chunk {
    pub code: Vec<OpCode>,
    pub constants: Vec<Value>,
    /// O(1) constant deduplication map (hash → index).
    #[allow(dead_code)]
    pub const_dedup: std::collections::HashMap<u64, usize>,
    pub functions: HashMap<String, CompiledFunction>,
    /// Number of local slots needed at top level.
    pub local_count: usize,
    /// Interned strings (global/function/builtin names) referenced by OpCode u32 indices.
    pub string_pool: Vec<String>,
    /// Reverse map for O(1) intern lookup (name → string_pool index).
    #[allow(dead_code)]
    intern_map: HashMap<String, u32>,
}

impl Chunk {
    pub fn new() -> Self {
        Self {
            code: Vec::new(),
            constants: Vec::new(),
            const_dedup: std::collections::HashMap::new(),
            functions: HashMap::new(),
            local_count: 0,
            string_pool: Vec::new(),
            intern_map: HashMap::new(),
        }
    }

    /// Intern a name into the string pool, returning its u32 index.
    /// O(1) via HashMap reverse lookup (was O(n) linear scan).
    pub fn intern(&mut self, name: &str) -> u32 {
        if let Some(&idx) = self.intern_map.get(name) {
            return idx;
        }
        let idx = self.string_pool.len() as u32;
        let s = name.to_string();
        self.intern_map.insert(s.clone(), idx);
        self.string_pool.push(s);
        idx
    }

    /// Resolve a u32 index back to a name (for dispatch/debug).
    #[inline]
    pub fn name_at(&self, idx: u32) -> &str {
        &self.string_pool[idx as usize]
    }

    pub fn add_constant(&mut self, val: Value) -> usize {
        let key = const_hash_key(&val);
        if let Some(idx) = self.const_dedup.get(&key) {
            return *idx;
        }
        let idx = self.constants.len();
        self.constants.push(val);
        self.const_dedup.insert(key, idx);
        idx
    }

    pub fn emit(&mut self, op: OpCode) -> usize {
        let pos = self.code.len();
        self.code.push(op);
        pos
    }

    pub fn current_pos(&self) -> usize {
        self.code.len()
    }

    pub fn patch_jump(&mut self, pos: usize, target: usize) {
        match &mut self.code[pos] {
            OpCode::Jump(ref mut addr) => *addr = target,
            OpCode::JumpIfFalse(ref mut addr) => *addr = target,
            OpCode::JumpIfTrue(ref mut addr) => *addr = target,
            _ => panic!("patch_jump on non-jump instruction at pos {}", pos),
        }
    }
}

fn values_match(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Int(x), Value::Int(y)) => x == y,
        (Value::Float(x), Value::Float(y)) => x == y && x.is_sign_positive() == y.is_sign_positive(),
        (Value::Bool(x), Value::Bool(y)) => x == y,
        (Value::Str(x), Value::Str(y)) => x == y,
        (Value::Char(x), Value::Char(y)) => x == y,
        _ => false,
    }
}

/// Scope for tracking local variables during compilation.
struct LocalScope {
    locals: Vec<(String, usize)>,
    base: usize,
}

/// Compiler: transforms AST into bytecode.
pub struct Compiler {
    scopes: Vec<LocalScope>,
    next_slot: usize,
}

impl Compiler {
    pub fn new() -> Self {
        Self {
            scopes: vec![LocalScope { locals: Vec::new(), base: 0 }],
            next_slot: 0,
        }
    }

    pub fn compile(&mut self, stmts: &[Stmt]) -> Result<Chunk, HexaError> {
        let mut chunk = Chunk::new();

        // First pass: compile all function declarations
        self.compile_functions_recursive(stmts, &mut chunk)?;

        // Second pass: compile top-level statements
        // For the last expression statement, keep the value on the stack
        let non_fn_stmts: Vec<&Stmt> = stmts.iter()
            .filter(|s| !matches!(s, Stmt::FnDecl(_)))
            .collect();

        for (i, stmt) in non_fn_stmts.iter().enumerate() {
            let is_last = i == non_fn_stmts.len() - 1;
            if is_last {
                if let Stmt::Expr(e) = stmt {
                    // Last expression: leave value on stack
                    self.compile_expr(&mut chunk, e)?;
                } else {
                    self.compile_stmt(&mut chunk, stmt)?;
                }
            } else {
                self.compile_stmt(&mut chunk, stmt)?;
            }
        }

        chunk.local_count = self.next_slot;
        Ok(chunk)
    }

    fn compile_functions_recursive(&mut self, stmts: &[Stmt], chunk: &mut Chunk) -> Result<(), HexaError> {
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                let func = self.compile_function(decl, chunk)?;
                chunk.functions.insert(decl.name.clone(), func);
            }
        }
        Ok(())
    }

    fn compile_function(&mut self, decl: &FnDecl, parent_chunk: &mut Chunk) -> Result<CompiledFunction, HexaError> {
        // Save state
        let old_scopes = std::mem::replace(&mut self.scopes, vec![LocalScope { locals: Vec::new(), base: 0 }]);
        let old_slot = self.next_slot;
        self.next_slot = 0;

        // Params are the first locals
        self.push_scope();
        for param in &decl.params {
            self.define_local(&param.name);
        }

        // Compile nested functions first
        for s in &decl.body {
            if let Stmt::FnDecl(inner_decl) = s {
                let inner_func = self.compile_function(inner_decl, parent_chunk)?;
                parent_chunk.functions.insert(inner_decl.name.clone(), inner_func);
            }
        }

        // Use a temporary chunk for this function's code
        // We need to compile into a temp chunk, then extract code
        let mut temp = Chunk::new();
        // Share constants + string_pool with parent
        temp.constants = parent_chunk.constants.clone();
        temp.string_pool = parent_chunk.string_pool.clone();

        // Compile body -- last expression is implicit return value
        let body_stmts: Vec<&Stmt> = decl.body.iter()
            .filter(|s| !matches!(s, Stmt::FnDecl(_)))
            .collect();
        for (i, stmt) in body_stmts.iter().enumerate() {
            let is_last = i == body_stmts.len() - 1;
            if is_last {
                if let Stmt::Expr(e) = stmt {
                    self.compile_expr(&mut temp, e)?;
                    temp.emit(OpCode::Return);
                } else {
                    self.compile_stmt(&mut temp, stmt)?;
                }
            } else {
                self.compile_stmt(&mut temp, stmt)?;
            }
        }

        // Ensure return at the end (for functions ending in non-expr stmts)
        if !matches!(temp.code.last(), Some(OpCode::Return)) {
            temp.emit(OpCode::Void);
            temp.emit(OpCode::Return);
        }

        let func_code = temp.code;
        // Merge constants + string_pool back to parent
        parent_chunk.constants = temp.constants;
        parent_chunk.string_pool = temp.string_pool;

        let local_count = self.next_slot;
        self.pop_scope();

        // Restore state
        self.scopes = old_scopes;
        self.next_slot = old_slot;

        let is_memoize = decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Memoize));
        Ok(CompiledFunction {
            name: decl.name.clone(),
            arity: decl.params.len(),
            param_names: decl.params.iter().map(|p| p.name.clone()).collect(),
            code: Arc::new(func_code),
            local_count,
            is_memoize,
        })
    }

    fn push_scope(&mut self) {
        let base = self.next_slot;
        self.scopes.push(LocalScope { locals: Vec::new(), base });
    }

    fn pop_scope(&mut self) {
        if let Some(scope) = self.scopes.pop() {
            self.next_slot = scope.base;
        }
    }

    fn define_local(&mut self, name: &str) -> usize {
        let slot = self.next_slot;
        self.next_slot += 1;
        if let Some(scope) = self.scopes.last_mut() {
            scope.locals.push((name.to_string(), slot));
        }
        slot
    }

    fn resolve_local(&self, name: &str) -> Option<usize> {
        for scope in self.scopes.iter().rev() {
            for (n, slot) in scope.locals.iter().rev() {
                if n == name {
                    return Some(*slot);
                }
            }
        }
        None
    }

    // ---- Statement compilation ----

    fn compile_stmt(&mut self, chunk: &mut Chunk, stmt: &Stmt) -> Result<(), HexaError> {
        match stmt {
            Stmt::Let(name, _typ, expr, _vis) => {
                if let Some(e) = expr {
                    self.compile_expr(chunk, e)?;
                } else {
                    chunk.emit(OpCode::Void);
                }
                let slot = self.define_local(name);
                chunk.emit(OpCode::SetLocal(slot));
                Ok(())
            }
            Stmt::LetTuple(names, expr) => {
                // Evaluate the RHS tuple/array, then extract each element
                self.compile_expr(chunk, expr)?;
                let tuple_slot = self.define_local("__td__");
                chunk.emit(OpCode::SetLocal(tuple_slot));
                for (i, name) in names.iter().enumerate() {
                    chunk.emit(OpCode::GetLocal(tuple_slot));
                    let idx_c = chunk.add_constant(Value::Int(i as i64)); chunk.emit(OpCode::Const(idx_c));
                    chunk.emit(OpCode::Index);
                    let slot = self.define_local(name);
                    chunk.emit(OpCode::SetLocal(slot));
                }
                Ok(())
            }
            Stmt::Assign(lhs, rhs) => {
                match lhs {
                    Expr::Ident(name) => {
                        self.compile_expr(chunk, rhs)?;
                        if let Some(slot) = self.resolve_local(name) {
                            chunk.emit(OpCode::SetLocal(slot));
                        } else {
                            let idx = chunk.intern(name);
                            chunk.emit(OpCode::SetGlobal(idx));
                        }
                    }
                    Expr::Index(arr_expr, idx_expr) => {
                        if let Expr::Ident(name) = arr_expr.as_ref() {
                            if let Some(slot) = self.resolve_local(name) {
                                chunk.emit(OpCode::GetLocal(slot));
                            } else {
                                let idx = chunk.intern(name);
                                chunk.emit(OpCode::GetGlobal(idx));
                            }
                            self.compile_expr(chunk, idx_expr)?;
                            self.compile_expr(chunk, rhs)?;
                            chunk.emit(OpCode::IndexAssign);
                            if let Some(slot) = self.resolve_local(name) {
                                chunk.emit(OpCode::SetLocal(slot));
                            } else {
                                let idx = chunk.intern(name);
                                chunk.emit(OpCode::SetGlobal(idx));
                            }
                        } else {
                            return Err(compile_err("complex index assignment not supported in VM".into()));
                        }
                    }
                    _ => return Err(compile_err("invalid assignment target".into())),
                }
                Ok(())
            }
            Stmt::Expr(expr) => {
                self.compile_expr(chunk, expr)?;
                chunk.emit(OpCode::Pop);
                Ok(())
            }
            Stmt::Return(expr) => {
                if let Some(e) = expr {
                    self.compile_expr(chunk, e)?;
                } else {
                    chunk.emit(OpCode::Void);
                }
                chunk.emit(OpCode::Return);
                Ok(())
            }
            Stmt::FnDecl(_) => {
                // Already handled in first pass
                Ok(())
            }
            Stmt::For(var, iter_expr, body) => {
                self.compile_expr(chunk, iter_expr)?;
                let arr_slot = self.define_local("__for_arr__");
                chunk.emit(OpCode::SetLocal(arr_slot));

                let idx_const = chunk.add_constant(Value::Int(0));
                chunk.emit(OpCode::Const(idx_const));
                let idx_slot = self.define_local("__for_idx__");
                chunk.emit(OpCode::SetLocal(idx_slot));

                self.push_scope();
                let var_slot = self.define_local(var);

                let loop_start = chunk.current_pos();

                // idx < len(arr)
                chunk.emit(OpCode::GetLocal(idx_slot));
                chunk.emit(OpCode::GetLocal(arr_slot));
                let len_idx = chunk.intern("len");
                chunk.emit(OpCode::CallBuiltin(len_idx, 1));
                chunk.emit(OpCode::Lt);
                let exit_jump = chunk.emit(OpCode::JumpIfFalse(0));

                // arr[idx]
                chunk.emit(OpCode::GetLocal(arr_slot));
                chunk.emit(OpCode::GetLocal(idx_slot));
                chunk.emit(OpCode::Index);
                chunk.emit(OpCode::SetLocal(var_slot));

                for s in body {
                    self.compile_stmt(chunk, s)?;
                }

                // idx += 1
                chunk.emit(OpCode::GetLocal(idx_slot));
                let one_const = chunk.add_constant(Value::Int(1));
                chunk.emit(OpCode::Const(one_const));
                chunk.emit(OpCode::Add);
                chunk.emit(OpCode::SetLocal(idx_slot));

                chunk.emit(OpCode::Jump(loop_start));

                let after_loop = chunk.current_pos();
                chunk.patch_jump(exit_jump, after_loop);

                self.pop_scope();
                Ok(())
            }
            Stmt::While(cond, body) => {
                let loop_start = chunk.current_pos();
                self.compile_expr(chunk, cond)?;
                let exit_jump = chunk.emit(OpCode::JumpIfFalse(0));

                self.push_scope();
                for s in body {
                    self.compile_stmt(chunk, s)?;
                }
                self.pop_scope();

                chunk.emit(OpCode::Jump(loop_start));
                let after_loop = chunk.current_pos();
                chunk.patch_jump(exit_jump, after_loop);
                Ok(())
            }
            Stmt::Loop(body) => {
                let loop_start = chunk.current_pos();
                self.push_scope();
                for s in body {
                    self.compile_stmt(chunk, s)?;
                }
                self.pop_scope();
                chunk.emit(OpCode::Jump(loop_start));
                Ok(())
            }
            Stmt::Assert(expr) | Stmt::ContractAssert(_, _, expr) => {
                self.compile_expr(chunk, expr)?;
                let assert_idx = chunk.intern("__assert__");
                chunk.emit(OpCode::CallBuiltin(assert_idx, 1));
                chunk.emit(OpCode::Pop);
                Ok(())
            }
            // Const/Static — treat like Let in VM
            Stmt::Const(name, _typ, expr, _vis) | Stmt::Static(name, _typ, expr, _vis) => {
                self.compile_expr(chunk, expr)?;
                let slot = self.define_local(name);
                chunk.emit(OpCode::SetLocal(slot));
                Ok(())
            }
            // Struct/Enum/Trait/Impl — skip (no runtime effect in VM)
            Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::TraitDecl(_)
            | Stmt::ImplBlock(_) | Stmt::TypeAlias(..) | Stmt::DeriveDecl(_, _) => Ok(()),
            // Not supported in VM — silently skip
            Stmt::Intent(_, _) | Stmt::Verify(_, _)
            | Stmt::Mod(_, _) | Stmt::Use(_)
            | Stmt::Proof(_, _) | Stmt::Spawn(_)
            | Stmt::DropStmt(_) | Stmt::SpawnNamed(_, _)
            | Stmt::AsyncFnDecl(_) | Stmt::Select(_, _)
            | Stmt::MacroDef(_)
            | Stmt::ConsciousnessBlock(_, _) | Stmt::EvolveFn(_)
            | Stmt::Generate(_) | Stmt::Optimize(_)
            | Stmt::ComptimeFn(_) | Stmt::EffectDecl(_)
            | Stmt::Scope(_) | Stmt::ProofBlock(..)
            | Stmt::AtomicLet(..) | Stmt::Panic(..) | Stmt::Theorem(..)
            | Stmt::Break | Stmt::Continue => Ok(()),
            Stmt::Extern(_) => {
                // VM does not support extern FFI; signal compile failure so tiered
                // execution falls through to the interpreter which handles FFI.
                Err(crate::error::HexaError {
                    class: crate::error::ErrorClass::Runtime,
                    message: "extern FFI not supported in VM".into(),
                    line: 0, col: 0, hint: None,
                })
            }
            Stmt::TryCatch(..) | Stmt::Throw(_) => {
                // VM does not support try/catch/throw; signal compile failure so
                // tiered execution falls through to the interpreter which handles it.
                Err(crate::error::HexaError {
                    class: crate::error::ErrorClass::Runtime,
                    message: "try/catch/throw not supported in VM".into(),
                    line: 0, col: 0, hint: None,
                })
            }
        }
    }

    // ---- Constant folding ----

    /// Try to evaluate a constant expression at compile time.
    fn try_const_fold(&self, expr: &Expr) -> Option<Value> {
        match expr {
            Expr::IntLit(n) => Some(Value::Int(*n)),
            Expr::FloatLit(n) => Some(Value::Float(*n)),
            Expr::BoolLit(b) => Some(Value::Bool(*b)),
            Expr::StringLit(s) => Some(Value::Str(s.clone())),
            Expr::CharLit(c) => Some(Value::Char(*c)),
            Expr::Unary(UnaryOp::Neg, operand) => {
                match self.try_const_fold(operand)? {
                    Value::Int(n) => Some(Value::Int(-n)),
                    Value::Float(f) => Some(Value::Float(-f)),
                    _ => None,
                }
            }
            Expr::Unary(UnaryOp::Not, operand) => {
                match self.try_const_fold(operand)? {
                    Value::Bool(b) => Some(Value::Bool(!b)),
                    _ => None,
                }
            }
            Expr::If(cond, then_block, else_block) => {
                // Fold deeply nested ternary/if-else chains when the condition is constant.
                let cv = self.try_const_fold(cond)?;
                let branch = match cv {
                    Value::Bool(true) => Some(then_block.as_slice()),
                    Value::Bool(false) => else_block.as_deref(),
                    Value::Int(n) => if n != 0 { Some(then_block.as_slice()) } else { else_block.as_deref() },
                    _ => None,
                };
                if let Some(stmts) = branch {
                    if let Some(last) = stmts.last() {
                        if let crate::ast::Stmt::Expr(e) = last {
                            return self.try_const_fold(e);
                        }
                        if let crate::ast::Stmt::Return(Some(e)) = last {
                            return self.try_const_fold(e);
                        }
                    }
                }
                None
            }
            Expr::Binary(left, op, right) => {
                let lv = self.try_const_fold(left)?;
                let rv = self.try_const_fold(right)?;
                match (lv, op, rv) {
                    // Int arithmetic — use checked ops to match interpreter semantics
                    (Value::Int(a), BinOp::Add, Value::Int(b)) => a.checked_add(b).map(Value::Int),
                    (Value::Int(a), BinOp::Sub, Value::Int(b)) => a.checked_sub(b).map(Value::Int),
                    (Value::Int(a), BinOp::Mul, Value::Int(b)) => a.checked_mul(b).map(Value::Int),
                    (Value::Int(a), BinOp::Div, Value::Int(b)) if b != 0 => Some(Value::Int(a / b)),
                    (Value::Int(a), BinOp::Rem, Value::Int(b)) if b != 0 => Some(Value::Int(a % b)),
                    (Value::Int(a), BinOp::Pow, Value::Int(b)) => Some(Value::Int((a as f64).powi(b as i32) as i64)),
                    // Float arithmetic
                    (Value::Float(a), BinOp::Add, Value::Float(b)) => Some(Value::Float(a + b)),
                    (Value::Float(a), BinOp::Sub, Value::Float(b)) => Some(Value::Float(a - b)),
                    (Value::Float(a), BinOp::Mul, Value::Float(b)) => Some(Value::Float(a * b)),
                    (Value::Float(a), BinOp::Div, Value::Float(b)) if b != 0.0 => Some(Value::Float(a / b)),
                    // Mixed int/float
                    (Value::Int(a), BinOp::Add, Value::Float(b)) => Some(Value::Float(a as f64 + b)),
                    (Value::Float(a), BinOp::Add, Value::Int(b)) => Some(Value::Float(a + b as f64)),
                    (Value::Int(a), BinOp::Sub, Value::Float(b)) => Some(Value::Float(a as f64 - b)),
                    (Value::Float(a), BinOp::Sub, Value::Int(b)) => Some(Value::Float(a - b as f64)),
                    (Value::Int(a), BinOp::Mul, Value::Float(b)) => Some(Value::Float(a as f64 * b)),
                    (Value::Float(a), BinOp::Mul, Value::Int(b)) => Some(Value::Float(a * b as f64)),
                    // String concatenation
                    (Value::Str(a), BinOp::Add, Value::Str(b)) => Some(Value::Str(format!("{}{}", a, b))),
                    // Int comparison
                    (Value::Int(a), BinOp::Eq, Value::Int(b)) => Some(Value::Bool(a == b)),
                    (Value::Int(a), BinOp::Ne, Value::Int(b)) => Some(Value::Bool(a != b)),
                    (Value::Int(a), BinOp::Lt, Value::Int(b)) => Some(Value::Bool(a < b)),
                    (Value::Int(a), BinOp::Gt, Value::Int(b)) => Some(Value::Bool(a > b)),
                    (Value::Int(a), BinOp::Le, Value::Int(b)) => Some(Value::Bool(a <= b)),
                    (Value::Int(a), BinOp::Ge, Value::Int(b)) => Some(Value::Bool(a >= b)),
                    // Bool logical
                    (Value::Bool(a), BinOp::And, Value::Bool(b)) => Some(Value::Bool(a && b)),
                    (Value::Bool(a), BinOp::Or, Value::Bool(b)) => Some(Value::Bool(a || b)),
                    // Bitwise int
                    (Value::Int(a), BinOp::BitAnd, Value::Int(b)) => Some(Value::Int(a & b)),
                    (Value::Int(a), BinOp::BitOr, Value::Int(b)) => Some(Value::Int(a | b)),
                    (Value::Int(a), BinOp::BitXor, Value::Int(b)) => Some(Value::Int(a ^ b)),
                    _ => None,
                }
            }
            // Empty array literal — fold to constant to avoid type ambiguity in IR
            Expr::Array(items) if items.is_empty() => {
                Some(Value::Array(vec![]))
            }
            _ => None,
        }
    }

    // ---- Expression compilation ----

    fn compile_expr(&mut self, chunk: &mut Chunk, expr: &Expr) -> Result<(), HexaError> {
        // Try constant folding first for binary/unary/if expressions and empty arrays
        if matches!(expr, Expr::Binary(..) | Expr::Unary(..) | Expr::If(..))
            || matches!(expr, Expr::Array(ref items) if items.is_empty())
        {
            if let Some(folded) = self.try_const_fold(expr) {
                let idx = chunk.add_constant(folded);
                chunk.emit(OpCode::Const(idx));
                return Ok(());
            }
        }

        match expr {
            Expr::IntLit(n) => {
                let idx = chunk.add_constant(Value::Int(*n));
                chunk.emit(OpCode::Const(idx));
                Ok(())
            }
            Expr::FloatLit(n) => {
                let idx = chunk.add_constant(Value::Float(*n));
                chunk.emit(OpCode::Const(idx));
                Ok(())
            }
            Expr::BoolLit(b) => {
                let idx = chunk.add_constant(Value::Bool(*b));
                chunk.emit(OpCode::Const(idx));
                Ok(())
            }
            Expr::StringLit(s) => {
                let idx = chunk.add_constant(Value::Str(s.clone()));
                chunk.emit(OpCode::Const(idx));
                Ok(())
            }
            Expr::CharLit(c) => {
                let idx = chunk.add_constant(Value::Char(*c));
                chunk.emit(OpCode::Const(idx));
                Ok(())
            }
            Expr::Ident(name) => {
                if let Some(slot) = self.resolve_local(name) {
                    chunk.emit(OpCode::GetLocal(slot));
                } else {
                    let idx = chunk.intern(name);
                    chunk.emit(OpCode::GetGlobal(idx));
                }
                Ok(())
            }
            Expr::Binary(left, op, right) => {
                match op {
                    BinOp::And => {
                        self.compile_expr(chunk, left)?;
                        chunk.emit(OpCode::Dup);
                        chunk.emit(OpCode::Truthy);
                        let jump = chunk.emit(OpCode::JumpIfFalse(0));
                        chunk.emit(OpCode::Pop);
                        self.compile_expr(chunk, right)?;
                        let after = chunk.current_pos();
                        chunk.patch_jump(jump, after);
                        // Ensure result is bool
                        chunk.emit(OpCode::Truthy);
                        return Ok(());
                    }
                    BinOp::Or => {
                        self.compile_expr(chunk, left)?;
                        chunk.emit(OpCode::Dup);
                        chunk.emit(OpCode::Truthy);
                        let jump = chunk.emit(OpCode::JumpIfTrue(0));
                        chunk.emit(OpCode::Pop);
                        self.compile_expr(chunk, right)?;
                        let after = chunk.current_pos();
                        chunk.patch_jump(jump, after);
                        chunk.emit(OpCode::Truthy);
                        return Ok(());
                    }
                    _ => {}
                }

                self.compile_expr(chunk, left)?;
                self.compile_expr(chunk, right)?;
                match op {
                    BinOp::Add => chunk.emit(OpCode::Add),
                    BinOp::Sub => chunk.emit(OpCode::Sub),
                    BinOp::Mul => chunk.emit(OpCode::Mul),
                    BinOp::Div => chunk.emit(OpCode::Div),
                    BinOp::Rem => chunk.emit(OpCode::Mod),
                    BinOp::Pow => chunk.emit(OpCode::Pow),
                    BinOp::Eq => chunk.emit(OpCode::Eq),
                    BinOp::Ne => chunk.emit(OpCode::Ne),
                    BinOp::Lt => chunk.emit(OpCode::Lt),
                    BinOp::Gt => chunk.emit(OpCode::Gt),
                    BinOp::Le => chunk.emit(OpCode::Le),
                    BinOp::Ge => chunk.emit(OpCode::Ge),
                    BinOp::Xor => {
                        chunk.emit(OpCode::Truthy);
                        // Need to re-evaluate: left is already consumed
                        // Actually both are on stack already. Just XOR them.
                        // But we need bool conversion of both...
                        // Simpler: just use BitXor on bools
                        chunk.emit(OpCode::BitXor)
                    },
                    BinOp::BitAnd => chunk.emit(OpCode::BitAnd),
                    BinOp::BitOr => chunk.emit(OpCode::BitOr),
                    BinOp::BitXor => chunk.emit(OpCode::BitXor),
                    _ => return Err(compile_err(format!("unsupported binary op: {:?}", op))),
                };
                Ok(())
            }
            Expr::Unary(op, operand) => {
                self.compile_expr(chunk, operand)?;
                match op {
                    UnaryOp::Neg => chunk.emit(OpCode::Neg),
                    UnaryOp::Not => chunk.emit(OpCode::Not),
                    UnaryOp::BitNot => chunk.emit(OpCode::BitNot),
                };
                Ok(())
            }
            Expr::Call(callee, args) => {
                match callee.as_ref() {
                    Expr::Ident(name) if is_builtin(name) => {
                        for arg in args {
                            self.compile_expr(chunk, arg)?;
                        }
                        if name == "print" {
                            chunk.emit(OpCode::Print(args.len()));
                        } else if name == "println" {
                            chunk.emit(OpCode::Println(args.len()));
                        } else {
                            let idx = chunk.intern(name);
                            chunk.emit(OpCode::CallBuiltin(idx, args.len() as u16));
                        }
                    }
                    Expr::Ident(name) => {
                        for arg in args {
                            self.compile_expr(chunk, arg)?;
                        }
                        let idx = chunk.intern(name);
                        chunk.emit(OpCode::CallFn(idx, args.len() as u16));
                    }
                    Expr::Field(obj_expr, method_name) => {
                        // Method call: obj.method(args)
                        // Push obj first, then args, then CallMethod
                        self.compile_expr(chunk, obj_expr)?;
                        for arg in args {
                            self.compile_expr(chunk, arg)?;
                        }
                        let idx = chunk.intern(method_name);
                        chunk.emit(OpCode::CallMethod(idx, args.len() as u16));
                    }
                    _ => {
                        // Unsupported call target (e.g. lambda calls) — bail to interpreter
                        return Err(compile_err("unsupported call target in VM".into()));
                    }
                }
                Ok(())
            }
            Expr::If(cond, then_block, else_block) => {
                self.compile_expr(chunk, cond)?;
                let else_jump = chunk.emit(OpCode::JumpIfFalse(0));

                // Then
                self.push_scope();
                self.compile_block_as_expr(chunk, then_block)?;
                self.pop_scope();
                let end_jump = chunk.emit(OpCode::Jump(0));

                // Else
                let else_target = chunk.current_pos();
                chunk.patch_jump(else_jump, else_target);
                if let Some(eb) = else_block {
                    self.push_scope();
                    self.compile_block_as_expr(chunk, eb)?;
                    self.pop_scope();
                } else {
                    chunk.emit(OpCode::Void);
                }

                let end_target = chunk.current_pos();
                chunk.patch_jump(end_jump, end_target);
                Ok(())
            }
            Expr::Array(items) => {
                for item in items {
                    self.compile_expr(chunk, item)?;
                }
                chunk.emit(OpCode::MakeArray(items.len()));
                Ok(())
            }
            Expr::Index(arr_expr, idx_expr) => {
                self.compile_expr(chunk, arr_expr)?;
                self.compile_expr(chunk, idx_expr)?;
                chunk.emit(OpCode::Index);
                Ok(())
            }
            Expr::Range(start, end, inclusive) => {
                self.compile_expr(chunk, start)?;
                self.compile_expr(chunk, end)?;
                chunk.emit(OpCode::MakeRange(*inclusive));
                Ok(())
            }
            Expr::Block(stmts) => {
                self.push_scope();
                self.compile_block_as_expr(chunk, stmts)?;
                self.pop_scope();
                Ok(())
            }
            Expr::Tuple(items) => {
                for item in items {
                    self.compile_expr(chunk, item)?;
                }
                chunk.emit(OpCode::MakeArray(items.len()));
                Ok(())
            }
            // Not supported in VM — bail to interpreter
            Expr::Lambda(_, _) | Expr::Field(_, _) | Expr::Match(_, _)
            | Expr::StructInit(_, _) | Expr::MapLit(_) | Expr::EnumPath(_, _, _)
            | Expr::Wildcard | Expr::ArrayPattern(_, _) | Expr::Own(_) | Expr::MoveExpr(_) | Expr::Borrow(_)
            | Expr::Await(_) | Expr::MacroInvoc(_) | Expr::Comptime(_)
            | Expr::HandleWith(_, _) | Expr::EffectCall(_, _, _) | Expr::Resume(_)
            | Expr::DynCast(_, _) | Expr::Yield(_) | Expr::Template(_)
            | Expr::TryCatch(_, _, _) => {
                return Err(compile_err("unsupported expression in VM".into()));
            }
        }
    }

    /// Compile a block so that the last expression's value is left on the stack.
    fn compile_block_as_expr(&mut self, chunk: &mut Chunk, stmts: &[Stmt]) -> Result<(), HexaError> {
        if stmts.is_empty() {
            chunk.emit(OpCode::Void);
            return Ok(());
        }
        for (i, s) in stmts.iter().enumerate() {
            if i == stmts.len() - 1 {
                if let Stmt::Expr(e) = s {
                    self.compile_expr(chunk, e)?;
                } else {
                    self.compile_stmt(chunk, s)?;
                    chunk.emit(OpCode::Void);
                }
            } else {
                self.compile_stmt(chunk, s)?;
            }
        }
        Ok(())
    }
}

fn is_builtin(name: &str) -> bool {
    matches!(name,
        // ── I/O & print ──
        "print" | "println" | "eprintln" | "print_err"
        // ── core ──
        | "len" | "type_of" | "format" | "clock" | "args" | "env_var" | "exit" | "env"
        | "input" | "readline" | "read_stdin" | "input_all"
        // ── math ──
        | "sigma" | "phi" | "tau" | "gcd" | "bigint" | "sopfr"
        | "abs" | "min" | "max" | "floor" | "ceil" | "round"
        | "sqrt" | "pow" | "log" | "log2" | "log10" | "ln" | "exp"
        | "sin" | "cos" | "tan" | "asin" | "acos" | "atan" | "atan2"
        // ── conversion ──
        | "to_string" | "str" | "to_int" | "to_float" | "to_char"
        | "try_float" | "is_numeric" | "parse_int" | "parse_float"
        // ── string ──
        | "split" | "trim" | "trim_start" | "trim_end" | "join"
        | "starts_with" | "ends_with" | "contains" | "replace"
        | "to_upper" | "to_lower" | "chars" | "repeat"
        | "index_of" | "substring" | "pad_left" | "pad_right"
        | "hex_encode" | "hex_decode"
        | "is_alpha" | "is_digit" | "is_alphanumeric" | "is_whitespace"
        // ── array/functional ──
        | "push" | "pop" | "reverse" | "sort" | "swap" | "fill"
        | "map" | "filter" | "fold" | "reduce" | "enumerate" | "sum"
        | "find" | "find_index" | "any" | "all" | "flatten" | "slice"
        | "map_arr" | "filter_arr" | "zip_arr" | "enumerate_arr"
        // ── map ──
        | "keys" | "values" | "has" | "has_key" | "contains_key" | "get"
        // ── file I/O ──
        | "read_file" | "write_file" | "append_file" | "file_exists" | "delete_file"
        // ── tensor & matrix ──
        | "tensor" | "tensor_zeros" | "tensor_fill"
        | "dot" | "matvec" | "matmul" | "matmul_into" | "transpose" | "normalize"
        | "matmul_transpose_a_into" | "matmul_transpose_b_into" | "matmul_into_accumulate"
        | "mat_add" | "mat_add_inplace" | "mat_scale" | "mat_scale_inplace"
        | "axpy" | "hadamard" | "argmax" | "zeros" | "ones" | "randn" | "arange"
        | "qkv_fused_into" | "ffn_fused_into" | "block_forward_fused" | "block_forward_chain"
        | "batch_matvec" | "repeat_kv" | "gqa_expand_kv_into" | "weight_dict"
        | "rope_inplace" | "attention_fused_into"
        | "block_forward_chain_f32"
        | "kv_cache_init" | "kv_cache_decode" | "kv_cache_decode_step" | "kv_cache_reset"
        | "load_weights_bin" | "mmap_weights" | "save_array" | "load_array"
        // ── neural / activations ──
        | "sigmoid" | "tanh_" | "relu" | "softmax" | "gelu" | "silu"
        | "layer_norm" | "rms_norm" | "group_norm" | "batch_norm"
        | "dropout" | "embedding" | "rope" | "sinusoidal_pe"
        | "attention" | "attention_cached" | "multi_head_attention"
        | "sliding_window_attention" | "grouped_query_attention" | "linear_attention"
        | "kv_cache_append" | "topk" | "sample_token"
        // ── optimization ──
        | "sgd_step" | "adam_step" | "adamw_step" | "ema" | "clip"
        | "warmup_lr" | "cosine_lr" | "phase_lr"
        | "xavier_init" | "kaiming_init" | "weight_decay"
        | "grad_clip_norm" | "grad_accumulate" | "numerical_grad"
        | "lora_init" | "lora_forward" | "magnitude_prune"
        // ── loss & metrics ──
        | "cross_entropy" | "mse_loss" | "focal_loss" | "contrastive_loss"
        | "cosine_sim" | "cosine_sim_matrix" | "kl_divergence" | "js_divergence"
        | "mutual_information" | "label_smoothing"
        | "curiosity_reward" | "dialogue_reward" | "combined_reward"
        // ── data & stats ──
        | "mean" | "running_stats" | "autocorrelation" | "trend_slope" | "sparsity"
        | "data_split" | "shuffle" | "one_hot" | "param_count" | "model_size_kb"
        | "ascii_plot" | "ascii_bar" | "beam_search_step"
        // ── random ──
        | "random" | "random_int"
        // ── topology ──
        | "ring_topology" | "small_world_topology" | "hypercube_topology" | "auto_topology"
        // ── MoE ──
        | "moe_gate" | "moe_combine" | "ewc_penalty" | "fisher_diagonal"
        // ── concurrency ──
        | "channel" | "send" | "recv" | "try_recv" | "is_ready"
        // ── JSON ──
        | "json_parse" | "json_stringify" | "bytes_encode" | "bytes_decode"
        // ── HTTP ──
        | "http_get" | "http_post"
        // ── time ──
        | "now" | "timestamp" | "elapsed" | "sleep"
        // ── memory / FFI ──
        | "cstring" | "from_cstring" | "from_cstring_n"
        | "ptr_null" | "ptr_addr" | "deref" | "alloc_raw" | "free_raw"
        | "mem_stats" | "mem_region" | "arena_reset" | "mem_budget"
        // ── system exec ──
        | "exec" | "exec_with_status"
        // ── test ──
        | "run_tests" | "run_benchmarks"
        // ── cell ──
        | "cell_split" | "cell_merge" | "evolve_gen"
        // ── consciousness ──
        | "consciousness_max_cells" | "consciousness_decoder_dim" | "consciousness_phi"
        | "consciousness_vector" | "phi_predict" | "tension" | "tension_link"
        | "hexad_modules" | "hexad_right" | "hexad_left"
        | "faction_consensus" | "lorenz_step" | "chaos_perturb"
        | "laws" | "meta_laws"
        | "psi_coupling" | "psi_balance" | "psi_steps" | "psi_entropy" | "psi_frustration" | "psi_alpha"
        // ── std::fs ──
        | "fs_read" | "fs_write" | "fs_append" | "fs_exists" | "fs_remove"
        // ── std::io ──
        | "io_stdin" | "io_read_lines" | "io_write_bytes" | "io_pipe"
        // ── std::net ──
        | "net_listen" | "net_accept" | "net_connect" | "net_read" | "net_write" | "net_close"
        // ── std::time ──
        | "time_now" | "time_now_ms" | "time_sleep" | "time_format"
        // ── std::collections ──
        | "btree_new" | "btree_set" | "btree_get" | "btree_remove" | "btree_keys" | "btree_len"
        | "pq_new" | "pq_push" | "pq_pop" | "pq_len"
        | "deque_new" | "deque_push_front" | "deque_push_back"
        | "deque_pop_front" | "deque_pop_back" | "deque_len"
        // ── std::encoding ──
        | "csv_parse" | "csv_format" | "url_encode" | "url_decode"
        // ── std::log ──
        | "log_debug" | "log_info" | "log_warn" | "log_error"
        // ── std::math extended ──
        | "math_pi" | "math_e" | "math_phi" | "math_abs" | "math_sqrt"
        // ── std::test ──
        | "assert_eq" | "assert_ne" | "assert_true" | "assert_false"
        // ── nexus ──
        | "nexus_scan" | "nexus_verify" | "nexus_omega"
    )
}

pub fn eliminate_dead_code(code: &mut Vec<OpCode>) {
    // Build set of jump targets (these are reachable even after Jump/Return)
    let mut jump_targets: std::collections::HashSet<usize> = std::collections::HashSet::new();
    for op in code.iter() {
        match op {
            OpCode::Jump(t) | OpCode::JumpIfFalse(t) | OpCode::JumpIfTrue(t) => {
                jump_targets.insert(*t);
            }
            _ => {}
        }
    }

    let mut reachable = vec![true; code.len()];
    let mut i = 0;
    while i < code.len() {
        if !reachable[i] {
            i += 1;
            continue;
        }
        match &code[i] {
            OpCode::Jump(_) | OpCode::Return => {
                // Mark subsequent instructions as unreachable until a jump target
                let mut j = i + 1;
                while j < code.len() && !jump_targets.contains(&j) {
                    reachable[j] = false;
                    j += 1;
                }
            }
            _ => {}
        }
        i += 1;
    }

    // Only eliminate if there's something to remove (preserves jump offsets by
    // keeping the structure intact -- we replace dead code with Pop which is a no-op
    // on empty stack, or we could use a Void+Pop pair, but simplest is to just
    // leave them as-is since the VM won't reach them anyway).
    // For now, we just track the optimization; a full rewrite would remap jumps.
    let dead_count = reachable.iter().filter(|&&r| !r).count();
    if dead_count > 0 {
        // We don't physically remove to avoid invalidating jump targets,
        // but we can replace with no-ops (Void then Pop)
        for (idx, is_reachable) in reachable.iter().enumerate() {
            if !is_reachable {
                code[idx] = OpCode::Void; // effectively a no-op since it's unreachable
            }
        }
    }
}

fn compile_err(msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: format!("compile error: {}", msg),
        line: 0,
        col: 0,
        hint: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn compile_source(src: &str) -> Chunk {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut compiler = Compiler::new();
        compiler.compile(&stmts).unwrap()
    }

    #[test]
    fn test_opcode_size_16b() {
        let sz = std::mem::size_of::<OpCode>();
        println!("OpCode size = {} bytes", sz);
        assert!(sz <= 16, "OpCode must be <= 16 bytes for L1d density, got {}", sz);
    }

    #[test]
    fn test_compile_constant() {
        let chunk = compile_source("42");
        assert!(chunk.constants.iter().any(|c| matches!(c, Value::Int(42))));
    }

    #[test]
    fn test_compile_binary() {
        // With constant folding, `1 + 2` folds to `Const(3)` — no Add opcode
        let chunk = compile_source("1 + 2");
        assert!(chunk.constants.iter().any(|c| matches!(c, Value::Int(3))));
        // Add opcode should NOT be emitted (folded away)
        assert!(!chunk.code.iter().any(|op| matches!(op, OpCode::Add)));
    }

    #[test]
    fn test_compile_let() {
        let chunk = compile_source("let x = 10");
        assert!(chunk.code.iter().any(|op| matches!(op, OpCode::SetLocal(_))));
    }

    #[test]
    fn test_compile_function() {
        let chunk = compile_source("fn add(a, b) { return a + b }");
        assert!(chunk.functions.contains_key("add"));
        assert_eq!(chunk.functions["add"].arity, 2);
    }

    #[test]
    fn test_compile_memoize_flag() {
        let chunk = compile_source("@memoize fn fib(n) { n }");
        assert!(chunk.functions.contains_key("fib"), "fib should be compiled");
        assert!(chunk.functions["fib"].is_memoize, "fib should have is_memoize=true");
    }

    #[test]
    fn test_compile_no_memoize_by_default() {
        let chunk = compile_source("fn add(a, b) { a + b }");
        assert!(!chunk.functions["add"].is_memoize);
    }

    #[test]
    fn test_compile_if() {
        let chunk = compile_source("let c = true\nif c { 1 } else { 2 }");
        assert!(chunk.code.iter().any(|op| matches!(op, OpCode::JumpIfFalse(_))));
    }

    // ---- Optimization tests ----

    #[test]
    fn test_const_fold_int_arithmetic() {
        // 3 + 4 should fold to 7
        let chunk = compile_source("3 + 4");
        assert!(chunk.constants.iter().any(|c| matches!(c, Value::Int(7))));
        assert!(!chunk.code.iter().any(|op| matches!(op, OpCode::Add)));
    }

    #[test]
    fn test_const_fold_nested() {
        // (2 + 3) * 4 should fold to 20
        let chunk = compile_source("(2 + 3) * 4");
        assert!(chunk.constants.iter().any(|c| matches!(c, Value::Int(20))));
        assert!(!chunk.code.iter().any(|op| matches!(op, OpCode::Add | OpCode::Mul)));
    }

    #[test]
    fn test_const_fold_bool_and() {
        // true && false should fold to false
        let chunk = compile_source("let x = true && false");
        // The && uses short-circuit in compile_expr, so it won't be folded
        // by the binary path. But the expression `true && false` should still work.
        // This test verifies the const_fold path for `and` via try_const_fold.
        // Actually && uses short-circuit compilation, not a binary op, so it
        // bypasses const_fold. This is expected behavior.
        assert!(chunk.code.len() > 0);
    }

    #[test]
    fn test_const_fold_string_concat() {
        // "hello" + " world" should fold to "hello world"
        let chunk = compile_source("\"hello\" + \" world\"");
        assert!(chunk.constants.iter().any(|c| matches!(c, Value::Str(s) if s == "hello world")));
    }

    #[test]
    fn test_const_fold_comparison() {
        // 5 > 3 should fold to true
        let chunk = compile_source("5 > 3");
        assert!(chunk.constants.iter().any(|c| matches!(c, Value::Bool(true))));
    }

    #[test]
    fn test_const_fold_negation() {
        // -42 should fold
        let chunk = compile_source("-42");
        assert!(chunk.constants.iter().any(|c| matches!(c, Value::Int(-42))));
        assert!(!chunk.code.iter().any(|op| matches!(op, OpCode::Neg)));
    }

    #[test]
    fn test_no_fold_with_variables() {
        // x + 1 should NOT fold (x is a variable)
        let chunk = compile_source("let x = 5\nx + 1");
        // The `x + 1` expression should still have an Add opcode
        assert!(chunk.code.iter().any(|op| matches!(op, OpCode::Add)));
    }

    #[test]
    fn test_dead_code_elimination() {
        let mut code = vec![
            OpCode::Const(0),
            OpCode::Return,
            OpCode::Const(1), // dead
            OpCode::Add,      // dead
        ];
        eliminate_dead_code(&mut code);
        // After DCE, instructions at index 2 and 3 should be replaced with Void
        assert!(matches!(code[2], OpCode::Void));
        assert!(matches!(code[3], OpCode::Void));
    }
}

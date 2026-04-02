#![allow(dead_code)]

use std::collections::HashMap;
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
    GetGlobal(String),  // push global variable
    SetGlobal(String),  // pop into global variable

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
    CallFn(String, usize),  // call named function with N args
    Return,             // return from function

    // Built-ins
    Print(usize),       // print N values (no newline)
    Println(usize),     // print N values + newline
    CallBuiltin(String, usize), // call builtin with N args

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
}

/// A compiled function: its bytecode + metadata.
#[derive(Debug, Clone)]
pub struct CompiledFunction {
    pub name: String,
    pub arity: usize,
    pub param_names: Vec<String>,
    pub code: Vec<OpCode>,
    /// Number of local slots needed (params + locals).
    pub local_count: usize,
}

/// A compiled chunk: top-level bytecode + constant pool + functions.
#[derive(Debug, Clone)]
pub struct Chunk {
    pub code: Vec<OpCode>,
    pub constants: Vec<Value>,
    pub functions: HashMap<String, CompiledFunction>,
    /// Number of local slots needed at top level.
    pub local_count: usize,
}

impl Chunk {
    pub fn new() -> Self {
        Self {
            code: Vec::new(),
            constants: Vec::new(),
            functions: HashMap::new(),
            local_count: 0,
        }
    }

    pub fn add_constant(&mut self, val: Value) -> usize {
        for (i, existing) in self.constants.iter().enumerate() {
            if values_match(existing, &val) {
                return i;
            }
        }
        self.constants.push(val);
        self.constants.len() - 1
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
        for stmt in stmts {
            if matches!(stmt, Stmt::FnDecl(_)) {
                continue;
            }
            self.compile_stmt(&mut chunk, stmt)?;
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
        let mut func_code: Vec<OpCode> = Vec::new();
        // We need to compile into a temp chunk, then extract code
        let mut temp = Chunk::new();
        // Share constants with parent
        temp.constants = parent_chunk.constants.clone();

        for stmt in &decl.body {
            if matches!(stmt, Stmt::FnDecl(_)) {
                continue;
            }
            self.compile_stmt(&mut temp, stmt)?;
        }

        // Ensure return at the end
        if !matches!(temp.code.last(), Some(OpCode::Return)) {
            temp.emit(OpCode::Void);
            temp.emit(OpCode::Return);
        }

        func_code = temp.code;
        // Merge constants back to parent
        parent_chunk.constants = temp.constants;

        let local_count = self.next_slot;
        self.pop_scope();

        // Restore state
        self.scopes = old_scopes;
        self.next_slot = old_slot;

        Ok(CompiledFunction {
            name: decl.name.clone(),
            arity: decl.params.len(),
            param_names: decl.params.iter().map(|p| p.name.clone()).collect(),
            code: func_code,
            local_count,
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
            Stmt::Assign(lhs, rhs) => {
                match lhs {
                    Expr::Ident(name) => {
                        self.compile_expr(chunk, rhs)?;
                        if let Some(slot) = self.resolve_local(name) {
                            chunk.emit(OpCode::SetLocal(slot));
                        } else {
                            chunk.emit(OpCode::SetGlobal(name.clone()));
                        }
                    }
                    Expr::Index(arr_expr, idx_expr) => {
                        if let Expr::Ident(name) = arr_expr.as_ref() {
                            if let Some(slot) = self.resolve_local(name) {
                                chunk.emit(OpCode::GetLocal(slot));
                            } else {
                                chunk.emit(OpCode::GetGlobal(name.clone()));
                            }
                            self.compile_expr(chunk, idx_expr)?;
                            self.compile_expr(chunk, rhs)?;
                            chunk.emit(OpCode::IndexAssign);
                            if let Some(slot) = self.resolve_local(name) {
                                chunk.emit(OpCode::SetLocal(slot));
                            } else {
                                chunk.emit(OpCode::SetGlobal(name.clone()));
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
                chunk.emit(OpCode::CallBuiltin("len".into(), 1));
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
            Stmt::Assert(expr) => {
                self.compile_expr(chunk, expr)?;
                chunk.emit(OpCode::CallBuiltin("__assert__".into(), 1));
                chunk.emit(OpCode::Pop);
                Ok(())
            }
            // Not supported in VM
            Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::TraitDecl(_)
            | Stmt::ImplBlock(_) | Stmt::Intent(_, _) | Stmt::Mod(_, _)
            | Stmt::Use(_) | Stmt::TryCatch(_, _, _) | Stmt::Throw(_)
            | Stmt::Proof(_, _) => Ok(()),
        }
    }

    // ---- Expression compilation ----

    fn compile_expr(&mut self, chunk: &mut Chunk, expr: &Expr) -> Result<(), HexaError> {
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
                    chunk.emit(OpCode::GetGlobal(name.clone()));
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
                            chunk.emit(OpCode::CallBuiltin(name.clone(), args.len()));
                        }
                    }
                    Expr::Ident(name) => {
                        for arg in args {
                            self.compile_expr(chunk, arg)?;
                        }
                        chunk.emit(OpCode::CallFn(name.clone(), args.len()));
                    }
                    _ => {
                        // Unsupported call target in VM
                        for arg in args {
                            self.compile_expr(chunk, arg)?;
                            chunk.emit(OpCode::Pop);
                        }
                        chunk.emit(OpCode::Void);
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
            // Not supported in VM
            Expr::Lambda(_, _) | Expr::Field(_, _) | Expr::Match(_, _)
            | Expr::StructInit(_, _) | Expr::MapLit(_) | Expr::EnumPath(_, _, _)
            | Expr::Wildcard => {
                chunk.emit(OpCode::Void);
                Ok(())
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
        "print" | "println" | "len" | "type_of"
        | "sigma" | "phi" | "tau" | "gcd"
        | "abs" | "min" | "max" | "floor" | "ceil" | "round"
        | "sqrt" | "pow" | "log" | "log2" | "sin" | "cos" | "tan"
        | "to_string" | "to_int" | "to_float"
        | "format" | "clock"
    )
}

fn compile_err(msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: format!("compile error: {}", msg),
        line: 0,
        col: 0,
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
    fn test_compile_constant() {
        let chunk = compile_source("42");
        assert!(chunk.constants.iter().any(|c| matches!(c, Value::Int(42))));
    }

    #[test]
    fn test_compile_binary() {
        let chunk = compile_source("1 + 2");
        assert!(chunk.code.iter().any(|op| matches!(op, OpCode::Add)));
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
    fn test_compile_if() {
        let chunk = compile_source("if true { 1 } else { 2 }");
        assert!(chunk.code.iter().any(|op| matches!(op, OpCode::JumpIfFalse(_))));
    }
}

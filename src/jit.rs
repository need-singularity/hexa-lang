#![allow(dead_code)]

//! Cranelift JIT compiler for HEXA-LANG.
//!
//! Compiles a subset of the AST to native machine code via Cranelift IR.
//! Supported: integer arithmetic, variables, functions (incl. recursion),
//! if/else, while, return, println.

use std::collections::HashMap;

use cranelift::prelude::*;
use cranelift_codegen::ir::types::I64;
use cranelift_codegen::ir::{AbiParam, UserFuncName};
use cranelift_jit::{JITBuilder, JITModule};
use cranelift_module::{FuncId, Linkage, Module};

use crate::ast::*;
use crate::error::{ErrorClass, HexaError};

/// Extern helper: print an i64 value followed by newline.
extern "C" fn hexa_println_i64(val: i64) {
    println!("{}", val);
}

/// Extern helper: print an i64 value (no newline).
extern "C" fn hexa_print_i64(val: i64) {
    print!("{}", val);
}

fn jit_err(msg: String) -> HexaError {
    HexaError {
        class: ErrorClass::Runtime,
        message: format!("JIT error: {}", msg),
        line: 0,
        col: 0,
        hint: None,
    }
}

/// Information about a JIT-compiled function.
struct JitFuncInfo {
    id: FuncId,
    param_count: usize,
}

/// Cranelift JIT compiler for HEXA.
pub struct JitCompiler {
    /// The JIT module that holds compiled code.
    module: JITModule,
    /// Map of function name -> (FuncId, param_count).
    functions: HashMap<String, JitFuncInfo>,
    /// FuncId for the println helper.
    println_id: FuncId,
    /// FuncId for the print helper.
    print_id: FuncId,
}

impl JitCompiler {
    pub fn new() -> Result<Self, HexaError> {
        let mut flag_builder = settings::builder();
        flag_builder
            .set("use_colocated_libcalls", "false")
            .map_err(|e| jit_err(format!("setting flag: {}", e)))?;
        flag_builder
            .set("is_pic", "false")
            .map_err(|e| jit_err(format!("setting flag: {}", e)))?;

        let isa_builder = cranelift_native::builder()
            .map_err(|msg| jit_err(format!("host ISA: {}", msg)))?;

        let isa = isa_builder
            .finish(settings::Flags::new(flag_builder))
            .map_err(|e| jit_err(format!("ISA finish: {}", e)))?;

        let mut builder = JITBuilder::with_isa(
            isa,
            cranelift_module::default_libcall_names(),
        );

        // Register extern helpers.
        builder.symbol("hexa_println_i64", hexa_println_i64 as *const u8);
        builder.symbol("hexa_print_i64", hexa_print_i64 as *const u8);

        let mut module = JITModule::new(builder);

        // Declare the println helper.
        let mut println_sig = module.make_signature();
        println_sig.params.push(AbiParam::new(I64));
        let println_id = module
            .declare_function("hexa_println_i64", Linkage::Import, &println_sig)
            .map_err(|e| jit_err(format!("declare println: {}", e)))?;

        // Declare the print helper.
        let mut print_sig = module.make_signature();
        print_sig.params.push(AbiParam::new(I64));
        let print_id = module
            .declare_function("hexa_print_i64", Linkage::Import, &print_sig)
            .map_err(|e| jit_err(format!("declare print: {}", e)))?;

        Ok(Self {
            module,
            functions: HashMap::new(),
            println_id,
            print_id,
        })
    }

    /// Compile and execute a program. Returns the result of the last expression (as i64).
    pub fn compile_and_run(&mut self, stmts: &[Stmt]) -> Result<i64, HexaError> {
        // First pass: declare all functions (so recursive/mutual calls work).
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                self.declare_function(decl)?;
            }
        }

        // Second pass: define (compile bodies of) all functions.
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                self.define_function(decl)?;
            }
        }

        // Collect top-level non-function statements.
        let top_stmts: Vec<&Stmt> = stmts
            .iter()
            .filter(|s| !matches!(s, Stmt::FnDecl(_)))
            .collect();

        // Compile the top-level as a special "__hexa_main__" function with no params.
        let main_id = self.compile_main(&top_stmts)?;

        // Finalize all function code.
        self.module
            .finalize_definitions()
            .map_err(|e| jit_err(format!("finalize: {}", e)))?;

        // Get pointer to main and call it.
        let main_ptr = self.module.get_finalized_function(main_id);
        let main_fn: fn() -> i64 = unsafe { std::mem::transmute(main_ptr) };
        Ok(main_fn())
    }

    /// Declare a HEXA function in the Cranelift module (signature only, no body).
    fn declare_function(&mut self, decl: &FnDecl) -> Result<(), HexaError> {
        let mut sig = self.module.make_signature();
        for _ in &decl.params {
            sig.params.push(AbiParam::new(I64));
        }
        sig.returns.push(AbiParam::new(I64));

        let id = self
            .module
            .declare_function(&decl.name, Linkage::Local, &sig)
            .map_err(|e| jit_err(format!("declare fn '{}': {}", decl.name, e)))?;

        self.functions.insert(
            decl.name.clone(),
            JitFuncInfo {
                id,
                param_count: decl.params.len(),
            },
        );
        Ok(())
    }

    /// Compile the body of a HEXA function.
    fn define_function(&mut self, decl: &FnDecl) -> Result<(), HexaError> {
        let info = self
            .functions
            .get(&decl.name)
            .ok_or_else(|| jit_err(format!("fn '{}' not declared", decl.name)))?;
        let func_id = info.id;

        let mut ctx = self.module.make_context();
        for _ in &decl.params {
            ctx.func.signature.params.push(AbiParam::new(I64));
        }
        ctx.func.signature.returns.push(AbiParam::new(I64));
        ctx.func.name = UserFuncName::user(0, func_id.as_u32());

        let mut func_ctx = FunctionBuilderContext::new();

        {
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut func_ctx);
            let entry_block = builder.create_block();
            builder.append_block_params_for_function_params(entry_block);
            builder.switch_to_block(entry_block);
            builder.seal_block(entry_block);

            let mut trans = FuncTranslator::new(
                &mut self.module,
                &self.functions,
                self.println_id,
                self.print_id,
            );

            // Bind params to variables.
            for (i, param) in decl.params.iter().enumerate() {
                let val = builder.block_params(entry_block)[i];
                let var = trans.declare_var(&param.name, &mut builder);
                builder.def_var(var, val);
            }

            // Compile body.
            let mut last_val = None;
            for stmt in &decl.body {
                last_val = trans.compile_stmt(&mut builder, stmt)?;
            }

            // If block is not terminated, return last value or 0.
            if !trans.is_block_terminated(&builder) {
                let ret_val = last_val.unwrap_or_else(|| builder.ins().iconst(I64, 0));
                builder.ins().return_(&[ret_val]);
            }

            builder.seal_all_blocks();
            builder.finalize();
        }

        self.module
            .define_function(func_id, &mut ctx)
            .map_err(|e| jit_err(format!("define fn '{}': {}", decl.name, e)))?;

        self.module.clear_context(&mut ctx);
        Ok(())
    }

    /// Compile top-level statements into a __hexa_main__ function.
    fn compile_main(&mut self, stmts: &[&Stmt]) -> Result<FuncId, HexaError> {
        let mut sig = self.module.make_signature();
        sig.returns.push(AbiParam::new(I64));

        let main_id = self
            .module
            .declare_function("__hexa_main__", Linkage::Local, &sig)
            .map_err(|e| jit_err(format!("declare main: {}", e)))?;

        let mut ctx = self.module.make_context();
        ctx.func.signature = sig;
        ctx.func.name = UserFuncName::user(0, main_id.as_u32());

        let mut func_ctx = FunctionBuilderContext::new();

        {
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut func_ctx);
            let entry_block = builder.create_block();
            builder.switch_to_block(entry_block);
            builder.seal_block(entry_block);

            let mut trans = FuncTranslator::new(
                &mut self.module,
                &self.functions,
                self.println_id,
                self.print_id,
            );

            let mut last_val = None;
            for stmt in stmts {
                last_val = trans.compile_stmt(&mut builder, stmt)?;
            }

            if !trans.is_block_terminated(&builder) {
                let ret_val = last_val.unwrap_or_else(|| builder.ins().iconst(I64, 0));
                builder.ins().return_(&[ret_val]);
            }

            builder.seal_all_blocks();
            builder.finalize();
        }

        self.module
            .define_function(main_id, &mut ctx)
            .map_err(|e| jit_err(format!("define main: {}", e)))?;

        self.module.clear_context(&mut ctx);
        Ok(main_id)
    }
}

/// Translates HEXA AST nodes into Cranelift IR within a single function.
struct FuncTranslator<'a> {
    module: &'a mut JITModule,
    functions: &'a HashMap<String, JitFuncInfo>,
    println_id: FuncId,
    print_id: FuncId,
    /// Variable map: name -> Cranelift Variable.
    vars: HashMap<String, Variable>,
}

impl<'a> FuncTranslator<'a> {
    fn new(
        module: &'a mut JITModule,
        functions: &'a HashMap<String, JitFuncInfo>,
        println_id: FuncId,
        print_id: FuncId,
    ) -> Self {
        Self {
            module,
            functions,
            println_id,
            print_id,
            vars: HashMap::new(),
        }
    }

    fn declare_var(&mut self, name: &str, builder: &mut FunctionBuilder) -> Variable {
        if let Some(&var) = self.vars.get(name) {
            return var;
        }
        let var = builder.declare_var(I64);
        self.vars.insert(name.to_string(), var);
        var
    }

    fn is_block_terminated(&self, builder: &FunctionBuilder) -> bool {
        builder.is_unreachable()
    }

    /// Compile a statement. Returns the last expression value (if any).
    fn compile_stmt(
        &mut self,
        builder: &mut FunctionBuilder,
        stmt: &Stmt,
    ) -> Result<Option<cranelift::prelude::Value>, HexaError> {
        if self.is_block_terminated(builder) {
            return Ok(None);
        }
        match stmt {
            Stmt::Let(name, _typ, expr, _vis) => {
                let val = if let Some(e) = expr {
                    self.compile_expr(builder, e)?
                } else {
                    builder.ins().iconst(I64, 0)
                };
                let var = self.declare_var(name, builder);
                builder.def_var(var, val);
                Ok(None)
            }
            Stmt::Assign(lhs, rhs) => {
                let val = self.compile_expr(builder, rhs)?;
                if let Expr::Ident(name) = lhs {
                    let var = self.declare_var(name, builder);
                    builder.def_var(var, val);
                }
                Ok(None)
            }
            Stmt::Expr(expr) => {
                let val = self.compile_expr(builder, expr)?;
                Ok(Some(val))
            }
            Stmt::Return(expr) => {
                let val = if let Some(e) = expr {
                    self.compile_expr(builder, e)?
                } else {
                    builder.ins().iconst(I64, 0)
                };
                builder.ins().return_(&[val]);
                Ok(None)
            }
            Stmt::While(cond, body) => {
                let header_block = builder.create_block();
                let body_block = builder.create_block();
                let exit_block = builder.create_block();

                builder.ins().jump(header_block, &[]);
                builder.switch_to_block(header_block);

                let cond_val = self.compile_expr(builder, cond)?;
                builder
                    .ins()
                    .brif(cond_val, body_block, &[], exit_block, &[]);

                builder.seal_block(body_block);
                builder.switch_to_block(body_block);

                for s in body {
                    self.compile_stmt(builder, s)?;
                }
                if !self.is_block_terminated(builder) {
                    builder.ins().jump(header_block, &[]);
                }
                builder.seal_block(header_block);

                builder.switch_to_block(exit_block);
                builder.seal_block(exit_block);
                Ok(None)
            }
            Stmt::FnDecl(_) => {
                // Already handled at top level.
                Ok(None)
            }
            // Unsupported statements are silently skipped for the JIT subset.
            _ => Ok(None),
        }
    }

    /// Compile an expression, returning a Cranelift IR Value (i64).
    fn compile_expr(
        &mut self,
        builder: &mut FunctionBuilder,
        expr: &Expr,
    ) -> Result<cranelift::prelude::Value, HexaError> {
        match expr {
            Expr::IntLit(n) => Ok(builder.ins().iconst(I64, *n)),
            Expr::BoolLit(b) => Ok(builder.ins().iconst(I64, if *b { 1 } else { 0 })),
            Expr::Ident(name) => {
                if let Some(&var) = self.vars.get(name) {
                    Ok(builder.use_var(var))
                } else {
                    Err(jit_err(format!("undefined variable '{}'", name)))
                }
            }
            Expr::Binary(left, op, right) => {
                let lhs = self.compile_expr(builder, left)?;
                let rhs = self.compile_expr(builder, right)?;
                match op {
                    BinOp::Add => Ok(builder.ins().iadd(lhs, rhs)),
                    BinOp::Sub => Ok(builder.ins().isub(lhs, rhs)),
                    BinOp::Mul => Ok(builder.ins().imul(lhs, rhs)),
                    BinOp::Div => Ok(builder.ins().sdiv(lhs, rhs)),
                    BinOp::Rem => Ok(builder.ins().srem(lhs, rhs)),
                    BinOp::Eq => {
                        let cmp = builder.ins().icmp(IntCC::Equal, lhs, rhs);
                        Ok(builder.ins().uextend(I64, cmp))
                    }
                    BinOp::Ne => {
                        let cmp = builder.ins().icmp(IntCC::NotEqual, lhs, rhs);
                        Ok(builder.ins().uextend(I64, cmp))
                    }
                    BinOp::Lt => {
                        let cmp = builder.ins().icmp(IntCC::SignedLessThan, lhs, rhs);
                        Ok(builder.ins().uextend(I64, cmp))
                    }
                    BinOp::Gt => {
                        let cmp =
                            builder.ins().icmp(IntCC::SignedGreaterThan, lhs, rhs);
                        Ok(builder.ins().uextend(I64, cmp))
                    }
                    BinOp::Le => {
                        let cmp = builder
                            .ins()
                            .icmp(IntCC::SignedLessThanOrEqual, lhs, rhs);
                        Ok(builder.ins().uextend(I64, cmp))
                    }
                    BinOp::Ge => {
                        let cmp = builder
                            .ins()
                            .icmp(IntCC::SignedGreaterThanOrEqual, lhs, rhs);
                        Ok(builder.ins().uextend(I64, cmp))
                    }
                    BinOp::And => {
                        let lhs_bool = builder.ins().icmp_imm(IntCC::NotEqual, lhs, 0);
                        let rhs_bool = builder.ins().icmp_imm(IntCC::NotEqual, rhs, 0);
                        let result = builder.ins().band(lhs_bool, rhs_bool);
                        Ok(builder.ins().uextend(I64, result))
                    }
                    BinOp::Or => {
                        let lhs_bool = builder.ins().icmp_imm(IntCC::NotEqual, lhs, 0);
                        let rhs_bool = builder.ins().icmp_imm(IntCC::NotEqual, rhs, 0);
                        let result = builder.ins().bor(lhs_bool, rhs_bool);
                        Ok(builder.ins().uextend(I64, result))
                    }
                    BinOp::BitAnd => Ok(builder.ins().band(lhs, rhs)),
                    BinOp::BitOr => Ok(builder.ins().bor(lhs, rhs)),
                    BinOp::BitXor => Ok(builder.ins().bxor(lhs, rhs)),
                    _ => Err(jit_err(format!("unsupported binary op {:?} in JIT", op))),
                }
            }
            Expr::Unary(op, operand) => {
                let val = self.compile_expr(builder, operand)?;
                match op {
                    UnaryOp::Neg => Ok(builder.ins().ineg(val)),
                    UnaryOp::Not => {
                        let is_zero = builder.ins().icmp_imm(IntCC::Equal, val, 0);
                        Ok(builder.ins().uextend(I64, is_zero))
                    }
                    UnaryOp::BitNot => Ok(builder.ins().bnot(val)),
                }
            }
            Expr::Call(callee, args) => {
                if let Expr::Ident(name) = callee.as_ref() {
                    // Handle println/print builtins.
                    if name == "println" || name == "print" {
                        let helper_id = if name == "println" {
                            self.println_id
                        } else {
                            self.print_id
                        };
                        // Print each argument.
                        for arg in args {
                            let val = self.compile_expr(builder, arg)?;
                            let func_ref = self
                                .module
                                .declare_func_in_func(helper_id, &mut builder.func);
                            builder.ins().call(func_ref, &[val]);
                        }
                        return Ok(builder.ins().iconst(I64, 0));
                    }

                    // User-defined function call.
                    if let Some(info) = self.functions.get(name.as_str()) {
                        let mut arg_vals = Vec::new();
                        for arg in args {
                            arg_vals.push(self.compile_expr(builder, arg)?);
                        }
                        let func_ref = self
                            .module
                            .declare_func_in_func(info.id, &mut builder.func);
                        let call = builder.ins().call(func_ref, &arg_vals);
                        let results = builder.inst_results(call);
                        Ok(results[0])
                    } else {
                        Err(jit_err(format!(
                            "undefined function '{}' in JIT (only integer functions supported)",
                            name
                        )))
                    }
                } else {
                    Err(jit_err(
                        "only direct function calls supported in JIT".into(),
                    ))
                }
            }
            Expr::If(cond, then_block, else_block) => {
                let cond_val = self.compile_expr(builder, cond)?;

                let then_bb = builder.create_block();
                let else_bb = builder.create_block();
                let merge_bb = builder.create_block();
                let merge_param = builder.append_block_param(merge_bb, I64);

                builder
                    .ins()
                    .brif(cond_val, then_bb, &[], else_bb, &[]);

                // Then branch.
                builder.seal_block(then_bb);
                builder.switch_to_block(then_bb);
                let then_val = self.compile_block(builder, then_block)?;
                if !self.is_block_terminated(builder) {
                    builder.ins().jump(merge_bb, &[then_val.into()]);
                }

                // Else branch.
                builder.seal_block(else_bb);
                builder.switch_to_block(else_bb);
                let else_val = if let Some(eb) = else_block {
                    self.compile_block(builder, eb)?
                } else {
                    builder.ins().iconst(I64, 0)
                };
                if !self.is_block_terminated(builder) {
                    builder.ins().jump(merge_bb, &[else_val.into()]);
                }

                builder.seal_block(merge_bb);
                builder.switch_to_block(merge_bb);
                Ok(merge_param)
            }
            Expr::Block(stmts) => self.compile_block(builder, stmts),
            // Unsupported expressions return 0.
            _ => Ok(builder.ins().iconst(I64, 0)),
        }
    }

    /// Compile a block of statements, returning the value of the last expression.
    fn compile_block(
        &mut self,
        builder: &mut FunctionBuilder,
        stmts: &[Stmt],
    ) -> Result<cranelift::prelude::Value, HexaError> {
        let mut last_val = builder.ins().iconst(I64, 0);
        for stmt in stmts {
            if self.is_block_terminated(builder) {
                break;
            }
            if let Some(val) = self.compile_stmt(builder, stmt)? {
                last_val = val;
            }
        }
        Ok(last_val)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn jit_run(src: &str) -> i64 {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut jit = JitCompiler::new().unwrap();
        jit.compile_and_run(&stmts).unwrap()
    }

    #[test]
    fn test_jit_arithmetic() {
        assert_eq!(jit_run("2 + 3 * 4"), 14);
        assert_eq!(jit_run("10 - 3"), 7);
        assert_eq!(jit_run("100 / 5"), 20);
        assert_eq!(jit_run("17 % 5"), 2);
    }

    #[test]
    fn test_jit_variables_and_functions() {
        let src = r#"
fn double(x) { return x * 2 }
double(21)
"#;
        assert_eq!(jit_run(src), 42);
    }

    #[test]
    fn test_jit_fibonacci() {
        let src = r#"
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(10)
"#;
        assert_eq!(jit_run(src), 55);
    }

    #[test]
    fn test_jit_if_else() {
        let src = r#"
fn abs_val(x) {
    if x < 0 { return -x }
    return x
}
abs_val(-42)
"#;
        assert_eq!(jit_run(src), 42);
    }

    #[test]
    fn test_jit_while_loop() {
        let src = r#"
fn sum_to(n) {
    let total = 0
    let i = 1
    while i <= n {
        total = total + i
        i = i + 1
    }
    return total
}
sum_to(10)
"#;
        assert_eq!(jit_run(src), 55);
    }

    #[test]
    fn test_jit_nested_calls() {
        let src = r#"
fn add(a, b) { return a + b }
fn mul(a, b) { return a * b }
add(mul(3, 4), mul(5, 6))
"#;
        assert_eq!(jit_run(src), 42);
    }
}

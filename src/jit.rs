//! Cranelift JIT compiler for HEXA-LANG.
//!
//! Compiles a subset of the AST to native machine code via Cranelift IR.
//! Supported: integer arithmetic, variables, functions (incl. recursion),
//! if/else, while, return, println, structs (creation/field access/methods),
//! arrays (creation/indexing), enums (creation/match), closures (basic).

use std::collections::HashMap;

use cranelift::prelude::*;
use cranelift_codegen::ir::types::I64;
use cranelift_codegen::ir::{AbiParam, UserFuncName};
use cranelift_jit::{JITBuilder, JITModule};
use cranelift_module::{FuncId, Linkage, Module};

use crate::ast::*;
use crate::error::{ErrorClass, HexaError};

/// All values in JIT are 8 bytes (i64).
const SLOT_SIZE: i64 = 8;

/// Extern helper: print an i64 value followed by newline.
extern "C" fn hexa_println_i64(val: i64) {
    println!("{}", val);
}

/// Extern helper: print an i64 value (no newline).
extern "C" fn hexa_print_i64(val: i64) {
    print!("{}", val);
}

/// Extern helper: allocate `size` bytes on the heap, return pointer as i64.
extern "C" fn hexa_alloc(size: i64) -> i64 {
    let layout = std::alloc::Layout::from_size_align(size as usize, 8).unwrap();
    let ptr = unsafe { std::alloc::alloc_zeroed(layout) };
    ptr as i64
}

/// Extern helper: free a heap allocation of `size` bytes.
extern "C" fn hexa_free(ptr: i64, size: i64) {
    if ptr == 0 {
        return;
    }
    let layout = std::alloc::Layout::from_size_align(size as usize, 8).unwrap();
    unsafe { std::alloc::dealloc(ptr as *mut u8, layout) };
}

/// Extern helper: bounds check — aborts with message if out of bounds.
extern "C" fn hexa_bounds_check(index: i64, length: i64) {
    if index < 0 || index >= length {
        eprintln!(
            "JIT runtime error: array index {} out of bounds (length {})",
            index, length
        );
        std::process::exit(1);
    }
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

/// Layout information for a struct type.
#[derive(Debug, Clone)]
struct StructLayout {
    /// Ordered field names.
    fields: Vec<String>,
    /// Field name -> byte offset from struct base pointer.
    offsets: HashMap<String, i64>,
    /// Total size in bytes (fields.len() * 8).
    total_size: i64,
}

impl StructLayout {
    fn from_fields(field_names: &[(String, String, Visibility)]) -> Self {
        let mut fields = Vec::new();
        let mut offsets = HashMap::new();
        for (i, (name, _typ, _vis)) in field_names.iter().enumerate() {
            fields.push(name.clone());
            offsets.insert(name.clone(), i as i64 * SLOT_SIZE);
        }
        StructLayout {
            total_size: fields.len() as i64 * SLOT_SIZE,
            fields,
            offsets,
        }
    }
}

/// Layout information for an enum type.
#[derive(Debug, Clone)]
struct EnumLayout {
    /// Variant name -> tag value.
    variant_tags: HashMap<String, i64>,
    /// Total size: 8 (tag) + 8 (payload) = 16 bytes.
    total_size: i64,
}

impl EnumLayout {
    fn from_variants(variants: &[(String, Option<Vec<String>>)]) -> Self {
        let mut variant_tags = HashMap::new();
        for (i, (name, _)) in variants.iter().enumerate() {
            variant_tags.insert(name.clone(), i as i64);
        }
        EnumLayout {
            variant_tags,
            total_size: 16, // 8-byte tag + 8-byte payload
        }
    }
}

/// Check whether a list of statements can be fully JIT-compiled.
/// Returns `true` if all constructs are within the JIT's supported subset.
pub fn can_jit(stmts: &[Stmt]) -> bool {
    stmts.iter().all(|s| can_jit_stmt(s))
}

fn can_jit_stmt(stmt: &Stmt) -> bool {
    match stmt {
        Stmt::Let(_, _, expr, _) => expr.as_ref().map_or(true, |e| can_jit_expr(e)),
        Stmt::Assign(lhs, rhs) => can_jit_expr(lhs) && can_jit_expr(rhs),
        Stmt::Expr(e) => can_jit_expr(e),
        Stmt::Return(e) => e.as_ref().map_or(true, |e| can_jit_expr(e)),
        Stmt::FnDecl(decl) => decl.body.iter().all(|s| can_jit_stmt(s)),
        Stmt::StructDecl(_) => true,
        Stmt::EnumDecl(_) => true,
        Stmt::ImplBlock(ib) => ib.methods.iter().all(|m| m.body.iter().all(|s| can_jit_stmt(s))),
        Stmt::While(cond, body) => can_jit_expr(cond) && body.iter().all(|s| can_jit_stmt(s)),
        // These are not yet supported in JIT:
        Stmt::For(..) | Stmt::Loop(..) | Stmt::TryCatch(..) | Stmt::Throw(..) |
        Stmt::Spawn(..) | Stmt::SpawnNamed(..) | Stmt::Mod(..) | Stmt::Use(..) |
        Stmt::Proof(..) | Stmt::Assert(..) | Stmt::Intent(..) | Stmt::Verify(..) |
        Stmt::DropStmt(..) | Stmt::AsyncFnDecl(..) | Stmt::Select(..) |
        Stmt::MacroDef(..) | Stmt::DeriveDecl(..) | Stmt::Generate(..) |
        Stmt::Optimize(..) | Stmt::ComptimeFn(..) | Stmt::EffectDecl(..) |
        Stmt::TraitDecl(..) | Stmt::Const(..) | Stmt::Static(..) => false,
    }
}

fn can_jit_expr(expr: &Expr) -> bool {
    match expr {
        Expr::IntLit(_) | Expr::BoolLit(_) | Expr::Ident(_) | Expr::Wildcard => true,
        Expr::Binary(l, _, r) => can_jit_expr(l) && can_jit_expr(r),
        Expr::Unary(_, e) => can_jit_expr(e),
        Expr::Call(callee, args) => can_jit_expr(callee) && args.iter().all(|a| can_jit_expr(a)),
        Expr::If(c, then_b, else_b) => {
            can_jit_expr(c)
                && then_b.iter().all(|s| can_jit_stmt(s))
                && else_b.as_ref().map_or(true, |b| b.iter().all(|s| can_jit_stmt(s)))
        }
        Expr::Block(stmts) => stmts.iter().all(|s| can_jit_stmt(s)),
        Expr::StructInit(_, fields) => fields.iter().all(|(_, e)| can_jit_expr(e)),
        Expr::Field(obj, _) => can_jit_expr(obj),
        Expr::Array(items) => items.iter().all(|e| can_jit_expr(e)),
        Expr::Index(arr, idx) => can_jit_expr(arr) && can_jit_expr(idx),
        Expr::EnumPath(_, _, data) => data.as_ref().map_or(true, |e| can_jit_expr(e)),
        Expr::Match(scrutinee, arms) => {
            can_jit_expr(scrutinee)
                && arms.iter().all(|arm| {
                    can_jit_expr(&arm.pattern)
                        && arm.guard.as_ref().map_or(true, |g| can_jit_expr(g))
                        && arm.body.iter().all(|s| can_jit_stmt(s))
                })
        }
        Expr::Lambda(_, body) => can_jit_expr(body),
        // Not supported in JIT:
        Expr::FloatLit(_) | Expr::StringLit(_) | Expr::CharLit(_) |
        Expr::Tuple(..) | Expr::Range(..) | Expr::MapLit(..) |
        Expr::Own(..) | Expr::MoveExpr(..) | Expr::Borrow(..) |
        Expr::Await(..) | Expr::MacroInvoc(..) | Expr::Comptime(..) |
        Expr::HandleWith(..) | Expr::EffectCall(..) | Expr::Resume(..) |
        Expr::DynCast(..) => false,
    }
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
    /// FuncId for the heap allocator.
    alloc_id: FuncId,
    /// FuncId for bounds check.
    bounds_check_id: FuncId,
    /// Struct type layouts.
    struct_layouts: HashMap<String, StructLayout>,
    /// Enum type layouts.
    enum_layouts: HashMap<String, EnumLayout>,
    /// Impl methods: type_name -> method_name -> FnDecl.
    impl_methods: HashMap<String, HashMap<String, FnDecl>>,
    /// Pre-compiled lambda infos (shared across all function translations).
    lambda_infos: Vec<LambdaInfo>,
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
        builder.symbol("hexa_alloc", hexa_alloc as *const u8);
        builder.symbol("hexa_free", hexa_free as *const u8);
        builder.symbol("hexa_bounds_check", hexa_bounds_check as *const u8);

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

        // Declare the allocator helper: i64 -> i64.
        let mut alloc_sig = module.make_signature();
        alloc_sig.params.push(AbiParam::new(I64));
        alloc_sig.returns.push(AbiParam::new(I64));
        let alloc_id = module
            .declare_function("hexa_alloc", Linkage::Import, &alloc_sig)
            .map_err(|e| jit_err(format!("declare alloc: {}", e)))?;

        // Declare bounds_check helper: (i64, i64) -> void.
        let mut bc_sig = module.make_signature();
        bc_sig.params.push(AbiParam::new(I64));
        bc_sig.params.push(AbiParam::new(I64));
        let bounds_check_id = module
            .declare_function("hexa_bounds_check", Linkage::Import, &bc_sig)
            .map_err(|e| jit_err(format!("declare bounds_check: {}", e)))?;

        Ok(Self {
            module,
            functions: HashMap::new(),
            println_id,
            print_id,
            alloc_id,
            bounds_check_id,
            struct_layouts: HashMap::new(),
            enum_layouts: HashMap::new(),
            impl_methods: HashMap::new(),
            lambda_infos: Vec::new(),
        })
    }

    /// Compile and execute a program. Returns the result of the last expression (as i64).
    pub fn compile_and_run(&mut self, stmts: &[Stmt]) -> Result<i64, HexaError> {
        // Pass 0: register struct/enum layouts and impl methods.
        for stmt in stmts {
            match stmt {
                Stmt::StructDecl(decl) => {
                    let layout = StructLayout::from_fields(&decl.fields);
                    self.struct_layouts.insert(decl.name.clone(), layout);
                }
                Stmt::EnumDecl(decl) => {
                    let layout = EnumLayout::from_variants(&decl.variants);
                    self.enum_layouts.insert(decl.name.clone(), layout);
                }
                Stmt::ImplBlock(ib) => {
                    let methods = self.impl_methods.entry(ib.target.clone()).or_insert_with(HashMap::new);
                    for method in &ib.methods {
                        methods.insert(method.name.clone(), method.clone());
                    }
                }
                _ => {}
            }
        }

        // Pass 1: declare all functions (so recursive/mutual calls work).
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                self.declare_function(decl)?;
            }
        }

        // Declare impl methods as functions with mangled names: Type__method.
        // They get an extra first param (self pointer).
        let impl_methods_snapshot: Vec<(String, HashMap<String, FnDecl>)> =
            self.impl_methods.iter().map(|(k, v)| (k.clone(), v.clone())).collect();
        for (type_name, methods) in &impl_methods_snapshot {
            for (method_name, decl) in methods {
                let mangled = format!("{}_{}", type_name, method_name);
                // Use the decl params as-is (parser already includes `self`).
                let modified = FnDecl {
                    name: mangled.clone(),
                    type_params: decl.type_params.clone(),
                    params: decl.params.clone(),
                    ret_type: decl.ret_type.clone(),
                    where_clauses: decl.where_clauses.clone(),
                    body: decl.body.clone(),
                    vis: decl.vis.clone(),
                    is_pure: decl.is_pure,
                };
                self.declare_function(&modified)?;
            }
        }

        // Pass 2: scan for lambdas and pre-compile them as module-level functions.
        // This must happen before Pass 3 (function body compilation) so that
        // lambda_infos are available for all FuncTranslators.
        let lambda_descs = scan_lambdas(stmts);
        for (i, (param_names, captures, body)) in lambda_descs.iter().enumerate() {
            let info = self.compile_lambda(i, param_names, captures, body)?;
            self.lambda_infos.push(info);
        }

        // Pass 3: define (compile bodies of) all functions.
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                self.define_function(decl)?;
            }
        }

        // Define impl methods with self type tracking.
        for (type_name, methods) in &impl_methods_snapshot {
            for (method_name, decl) in methods {
                let mangled = format!("{}_{}", type_name, method_name);
                let modified = FnDecl {
                    name: mangled,
                    type_params: decl.type_params.clone(),
                    params: decl.params.clone(),
                    ret_type: decl.ret_type.clone(),
                    where_clauses: decl.where_clauses.clone(),
                    body: decl.body.clone(),
                    vis: decl.vis.clone(),
                    is_pure: decl.is_pure,
                };
                self.define_function_with_self_type(&modified, type_name)?;
            }
        }

        // Collect top-level non-declaration statements.
        let top_stmts: Vec<&Stmt> = stmts
            .iter()
            .filter(|s| !matches!(s, Stmt::FnDecl(_) | Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::ImplBlock(_)))
            .collect();

        // Compile the top-level as a special "__hexa_main__" function with no params.
        let main_id = self.compile_main_with_lambdas(&top_stmts)?;

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

    /// Compile the body of a HEXA function, optionally with a known self type.
    fn define_function(&mut self, decl: &FnDecl) -> Result<(), HexaError> {
        self.define_function_inner(decl, None)
    }

    fn define_function_with_self_type(&mut self, decl: &FnDecl, self_type: &str) -> Result<(), HexaError> {
        self.define_function_inner(decl, Some(self_type.to_string()))
    }

    fn define_function_inner(&mut self, decl: &FnDecl, self_type: Option<String>) -> Result<(), HexaError> {
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

            let mut trans = FuncTranslator::new(
                &mut self.module,
                &self.functions,
                self.println_id,
                self.print_id,
                self.alloc_id,
                self.bounds_check_id,
                &self.struct_layouts,
                &self.enum_layouts,
                &self.impl_methods,
            );

            // Pass pre-compiled lambda info to the translator.
            trans.lambdas = self.lambda_infos.clone();

            // If this is an impl method, track `self` as the struct type.
            if let Some(ref st) = self_type {
                trans.var_types.insert("self".to_string(), st.clone());
            }

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

    /// Pre-compile a lambda as a module-level function.
    /// The compiled function signature is: (env_ptr: i64, param0: i64, ...) -> i64.
    /// Inside the function body, captured variables are loaded from env_ptr.
    fn compile_lambda(
        &mut self,
        index: usize,
        param_names: &[String],
        captures: &[String],
        body: &Expr,
    ) -> Result<LambdaInfo, HexaError> {
        let func_name = format!("__lambda_{}__", index);

        // Signature: env_ptr + params -> i64
        let mut sig = self.module.make_signature();
        sig.params.push(AbiParam::new(I64)); // env_ptr
        for _ in param_names {
            sig.params.push(AbiParam::new(I64));
        }
        sig.returns.push(AbiParam::new(I64));

        let func_id = self.module
            .declare_function(&func_name, Linkage::Local, &sig)
            .map_err(|e| jit_err(format!("declare lambda '{}': {}", func_name, e)))?;

        let mut ctx = self.module.make_context();
        ctx.func.signature = sig;
        ctx.func.name = UserFuncName::user(0, func_id.as_u32());

        let mut func_ctx = FunctionBuilderContext::new();
        {
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut func_ctx);
            let entry_block = builder.create_block();
            builder.append_block_params_for_function_params(entry_block);
            builder.switch_to_block(entry_block);

            let mut trans = FuncTranslator::new(
                &mut self.module,
                &self.functions,
                self.println_id,
                self.print_id,
                self.alloc_id,
                self.bounds_check_id,
                &self.struct_layouts,
                &self.enum_layouts,
                &self.impl_methods,
            );

            // Param 0 is env_ptr.
            let env_ptr_val = builder.block_params(entry_block)[0];

            // Load captured variables from environment.
            for (i, cap_name) in captures.iter().enumerate() {
                let var = trans.declare_var(cap_name, &mut builder);
                let val = trans.load_from_offset(
                    &mut builder,
                    env_ptr_val,
                    i as i64 * SLOT_SIZE,
                );
                builder.def_var(var, val);
            }

            // Bind lambda parameters (offset by 1 for env_ptr).
            for (i, pname) in param_names.iter().enumerate() {
                let val = builder.block_params(entry_block)[i + 1];
                let var = trans.declare_var(pname, &mut builder);
                builder.def_var(var, val);
            }

            // Compile the body.
            let body_val = trans.compile_expr(&mut builder, body)?;

            if !trans.is_block_terminated(&builder) {
                builder.ins().return_(&[body_val]);
            }

            builder.seal_all_blocks();
            builder.finalize();
        }

        self.module
            .define_function(func_id, &mut ctx)
            .map_err(|e| jit_err(format!("define lambda '{}': {}", func_name, e)))?;
        self.module.clear_context(&mut ctx);

        Ok(LambdaInfo {
            func_name,
            func_id,
            captures: captures.to_vec(),
            param_count: param_names.len(),
        })
    }

    /// Compile top-level statements into a __hexa_main__ function, with lambda info.
    fn compile_main_with_lambdas(
        &mut self,
        stmts: &[&Stmt],
    ) -> Result<FuncId, HexaError> {
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

            let mut trans = FuncTranslator::new(
                &mut self.module,
                &self.functions,
                self.println_id,
                self.print_id,
                self.alloc_id,
                self.bounds_check_id,
                &self.struct_layouts,
                &self.enum_layouts,
                &self.impl_methods,
            );
            trans.lambdas = self.lambda_infos.clone();

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

// ── FuncTranslator ─────────────────────────────────────────────────

/// Information about a pre-compiled lambda (closure) function.
#[derive(Debug, Clone)]
struct LambdaInfo {
    /// Unique function name for this lambda (e.g., "__lambda_0__").
    func_name: String,
    /// FuncId in the Cranelift module.
    func_id: FuncId,
    /// Names of captured (free) variables, in order.
    captures: Vec<String>,
    /// Number of lambda parameters (excluding env_ptr).
    param_count: usize,
}

/// Walk an expression AST to collect free variables (identifiers that are
/// not bound by lambda parameters or local let bindings in the body).
fn collect_free_vars(expr: &Expr, params: &[String], bound: &mut Vec<String>) -> Vec<String> {
    let mut free = Vec::new();
    collect_free_vars_inner(expr, params, bound, &mut free);
    // Deduplicate while preserving order.
    let mut seen = std::collections::HashSet::new();
    free.retain(|v| seen.insert(v.clone()));
    free
}

fn collect_free_vars_inner(
    expr: &Expr,
    params: &[String],
    bound: &[String],
    free: &mut Vec<String>,
) {
    match expr {
        Expr::Ident(name) => {
            if !params.contains(name) && !bound.contains(name) && !is_builtin(name) {
                free.push(name.clone());
            }
        }
        Expr::Binary(l, _, r) => {
            collect_free_vars_inner(l, params, bound, free);
            collect_free_vars_inner(r, params, bound, free);
        }
        Expr::Unary(_, e) => collect_free_vars_inner(e, params, bound, free),
        Expr::Call(callee, args) => {
            collect_free_vars_inner(callee, params, bound, free);
            for a in args {
                collect_free_vars_inner(a, params, bound, free);
            }
        }
        Expr::If(c, then_b, else_b) => {
            collect_free_vars_inner(c, params, bound, free);
            for s in then_b {
                collect_free_vars_stmt(s, params, bound, free);
            }
            if let Some(eb) = else_b {
                for s in eb {
                    collect_free_vars_stmt(s, params, bound, free);
                }
            }
        }
        Expr::Block(stmts) => {
            for s in stmts {
                collect_free_vars_stmt(s, params, bound, free);
            }
        }
        Expr::Array(items) => {
            for item in items {
                collect_free_vars_inner(item, params, bound, free);
            }
        }
        Expr::Index(arr, idx) => {
            collect_free_vars_inner(arr, params, bound, free);
            collect_free_vars_inner(idx, params, bound, free);
        }
        Expr::Field(obj, _) => collect_free_vars_inner(obj, params, bound, free),
        Expr::Lambda(inner_params, body) => {
            // Inner lambda: its params are bound within its body, not free here
            let inner_param_names: Vec<String> = inner_params.iter().map(|p| p.name.clone()).collect();
            let mut combined = params.to_vec();
            combined.extend(inner_param_names);
            combined.extend(bound.to_vec());
            collect_free_vars_inner(body, &combined, &[], free);
        }
        Expr::StructInit(_, fields) => {
            for (_, e) in fields {
                collect_free_vars_inner(e, params, bound, free);
            }
        }
        Expr::Match(scrut, arms) => {
            collect_free_vars_inner(scrut, params, bound, free);
            for arm in arms {
                collect_free_vars_inner(&arm.pattern, params, bound, free);
                if let Some(g) = &arm.guard {
                    collect_free_vars_inner(g, params, bound, free);
                }
                for s in &arm.body {
                    collect_free_vars_stmt(s, params, bound, free);
                }
            }
        }
        _ => {} // Literals, etc.
    }
}

fn collect_free_vars_stmt(
    stmt: &Stmt,
    params: &[String],
    bound: &[String],
    free: &mut Vec<String>,
) {
    match stmt {
        Stmt::Let(_, _, Some(e), _) => collect_free_vars_inner(e, params, bound, free),
        Stmt::Assign(l, r) => {
            collect_free_vars_inner(l, params, bound, free);
            collect_free_vars_inner(r, params, bound, free);
        }
        Stmt::Expr(e) => collect_free_vars_inner(e, params, bound, free),
        Stmt::Return(Some(e)) => collect_free_vars_inner(e, params, bound, free),
        Stmt::While(c, body) => {
            collect_free_vars_inner(c, params, bound, free);
            for s in body {
                collect_free_vars_stmt(s, params, bound, free);
            }
        }
        _ => {}
    }
}

fn is_builtin(name: &str) -> bool {
    matches!(name, "println" | "print" | "len" | "true" | "false")
}

/// Scan an AST for all Lambda expressions and return info about each.
/// Assigns sequential names __lambda_0__, __lambda_1__, etc.
fn scan_lambdas(stmts: &[Stmt]) -> Vec<(Vec<String>, Vec<String>, Box<Expr>)> {
    let mut lambdas = Vec::new();
    for stmt in stmts {
        scan_lambdas_stmt(stmt, &[], &mut lambdas);
    }
    lambdas
}

/// (param_names, captures, body) for each lambda found.
fn scan_lambdas_stmt(
    stmt: &Stmt,
    scope_vars: &[String],
    out: &mut Vec<(Vec<String>, Vec<String>, Box<Expr>)>,
) {
    match stmt {
        Stmt::Let(name, _, Some(e), _) => {
            scan_lambdas_expr(e, scope_vars, out);
            // After this let, `name` is in scope for subsequent statements
            // (but we handle this at the block level, not here)
            let _ = name;
        }
        Stmt::Assign(l, r) => {
            scan_lambdas_expr(l, scope_vars, out);
            scan_lambdas_expr(r, scope_vars, out);
        }
        Stmt::Expr(e) => scan_lambdas_expr(e, scope_vars, out),
        Stmt::Return(Some(e)) => scan_lambdas_expr(e, scope_vars, out),
        Stmt::FnDecl(f) => {
            let mut inner_scope: Vec<String> = scope_vars.to_vec();
            for p in &f.params {
                inner_scope.push(p.name.clone());
            }
            for s in &f.body {
                scan_lambdas_stmt(s, &inner_scope, out);
            }
        }
        Stmt::While(c, body) => {
            scan_lambdas_expr(c, scope_vars, out);
            for s in body {
                scan_lambdas_stmt(s, scope_vars, out);
            }
        }
        _ => {}
    }
}

fn scan_lambdas_expr(
    expr: &Expr,
    scope_vars: &[String],
    out: &mut Vec<(Vec<String>, Vec<String>, Box<Expr>)>,
) {
    match expr {
        Expr::Lambda(params, body) => {
            let param_names: Vec<String> = params.iter().map(|p| p.name.clone()).collect();
            let captures = collect_free_vars(body, &param_names, &mut scope_vars.to_vec());
            out.push((param_names, captures, body.clone()));
            // Also scan inside the lambda body for nested lambdas
            scan_lambdas_expr(body, scope_vars, out);
        }
        Expr::Binary(l, _, r) => {
            scan_lambdas_expr(l, scope_vars, out);
            scan_lambdas_expr(r, scope_vars, out);
        }
        Expr::Unary(_, e) => scan_lambdas_expr(e, scope_vars, out),
        Expr::Call(callee, args) => {
            scan_lambdas_expr(callee, scope_vars, out);
            for a in args {
                scan_lambdas_expr(a, scope_vars, out);
            }
        }
        Expr::If(c, then_b, else_b) => {
            scan_lambdas_expr(c, scope_vars, out);
            for s in then_b {
                scan_lambdas_stmt(s, scope_vars, out);
            }
            if let Some(eb) = else_b {
                for s in eb {
                    scan_lambdas_stmt(s, scope_vars, out);
                }
            }
        }
        Expr::Block(stmts) => {
            for s in stmts {
                scan_lambdas_stmt(s, scope_vars, out);
            }
        }
        Expr::Array(items) => {
            for item in items {
                scan_lambdas_expr(item, scope_vars, out);
            }
        }
        Expr::Index(a, b) => {
            scan_lambdas_expr(a, scope_vars, out);
            scan_lambdas_expr(b, scope_vars, out);
        }
        Expr::Field(obj, _) => scan_lambdas_expr(obj, scope_vars, out),
        _ => {}
    }
}

/// Translates HEXA AST nodes into Cranelift IR within a single function.
struct FuncTranslator<'a> {
    module: &'a mut JITModule,
    functions: &'a HashMap<String, JitFuncInfo>,
    println_id: FuncId,
    print_id: FuncId,
    alloc_id: FuncId,
    bounds_check_id: FuncId,
    /// Variable map: name -> Cranelift Variable.
    vars: HashMap<String, Variable>,
    /// Struct layouts for field offset computation.
    struct_layouts: &'a HashMap<String, StructLayout>,
    /// Enum layouts for tag/payload computation.
    enum_layouts: &'a HashMap<String, EnumLayout>,
    /// Impl methods for method call resolution.
    impl_methods: &'a HashMap<String, HashMap<String, FnDecl>>,
    /// Track which variables hold struct pointers and their type name.
    var_types: HashMap<String, String>,
    /// Pre-compiled lambda info, indexed to match scan order.
    lambdas: Vec<LambdaInfo>,
    /// Counter for matching lambdas during compilation (incremented each
    /// time we encounter a Lambda expression).
    lambda_counter: usize,
}

impl<'a> FuncTranslator<'a> {
    fn new(
        module: &'a mut JITModule,
        functions: &'a HashMap<String, JitFuncInfo>,
        println_id: FuncId,
        print_id: FuncId,
        alloc_id: FuncId,
        bounds_check_id: FuncId,
        struct_layouts: &'a HashMap<String, StructLayout>,
        enum_layouts: &'a HashMap<String, EnumLayout>,
        impl_methods: &'a HashMap<String, HashMap<String, FnDecl>>,
    ) -> Self {
        Self {
            module,
            functions,
            println_id,
            print_id,
            alloc_id,
            bounds_check_id,
            vars: HashMap::new(),
            struct_layouts,
            enum_layouts,
            impl_methods,
            var_types: HashMap::new(),
            lambdas: Vec::new(),
            lambda_counter: 0,
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
        let block = builder.current_block().unwrap();
        if let Some(last_inst) = builder.func.layout.last_inst(block) {
            let opcode = builder.func.dfg.insts[last_inst].opcode();
            opcode.is_branch() || opcode.is_return()
        } else {
            false
        }
    }

    /// Allocate `size` bytes via the hexa_alloc extern, returning a pointer Value.
    fn call_alloc(
        &mut self,
        builder: &mut FunctionBuilder,
        size: i64,
    ) -> cranelift::prelude::Value {
        let size_val = builder.ins().iconst(I64, size);
        let alloc_ref = self
            .module
            .declare_func_in_func(self.alloc_id, &mut builder.func);
        let call = builder.ins().call(alloc_ref, &[size_val]);
        builder.inst_results(call)[0]
    }

    /// Store an i64 value at ptr + offset.
    fn store_at_offset(
        &self,
        builder: &mut FunctionBuilder,
        ptr: cranelift::prelude::Value,
        offset: i64,
        val: cranelift::prelude::Value,
    ) {
        builder.ins().store(MemFlags::trusted(), val, ptr, offset as i32);
    }

    /// Load an i64 value from ptr + offset.
    fn load_from_offset(
        &self,
        builder: &mut FunctionBuilder,
        ptr: cranelift::prelude::Value,
        offset: i64,
    ) -> cranelift::prelude::Value {
        builder.ins().load(I64, MemFlags::trusted(), ptr, offset as i32)
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

                // Track type for struct/enum init expressions.
                if let Some(e) = expr {
                    match e {
                        Expr::StructInit(sname, _) => {
                            self.var_types.insert(name.clone(), sname.clone());
                        }
                        Expr::EnumPath(ename, _, _) => {
                            self.var_types.insert(name.clone(), ename.clone());
                        }
                        Expr::Array(_) => {
                            self.var_types.insert(name.clone(), "__array__".to_string());
                        }
                        Expr::Call(callee, _) => {
                            // If calling a function that returns a struct, try to track it.
                            if let Expr::Ident(fname) = callee.as_ref() {
                                // Look for a function whose name matches a known constructor pattern.
                                // For now, if variable name hints at type, skip.
                                let _ = fname;
                            }
                        }
                        _ => {}
                    }
                }

                Ok(None)
            }
            Stmt::Assign(lhs, rhs) => {
                match lhs {
                    Expr::Ident(name) => {
                        let val = self.compile_expr(builder, rhs)?;
                        let var = self.declare_var(name, builder);
                        builder.def_var(var, val);
                    }
                    Expr::Field(obj_expr, field_name) => {
                        // Assign to struct field: obj.field = rhs
                        let ptr = self.compile_expr(builder, obj_expr)?;
                        let val = self.compile_expr(builder, rhs)?;
                        // Resolve type from the object expression.
                        if let Some(type_name) = self.resolve_expr_type(obj_expr) {
                            if let Some(layout) = self.struct_layouts.get(&type_name) {
                                if let Some(&offset) = layout.offsets.get(field_name) {
                                    self.store_at_offset(builder, ptr, offset, val);
                                }
                            }
                        }
                    }
                    Expr::Index(arr_expr, idx_expr) => {
                        // Assign to array element: arr[i] = rhs
                        let arr_ptr = self.compile_expr(builder, arr_expr)?;
                        let idx = self.compile_expr(builder, idx_expr)?;
                        let val = self.compile_expr(builder, rhs)?;
                        // Array layout: [length, elem0, elem1, ...]
                        // Offset = (idx + 1) * 8
                        let one = builder.ins().iconst(I64, 1);
                        let idx_plus_one = builder.ins().iadd(idx, one);
                        let slot_size = builder.ins().iconst(I64, SLOT_SIZE);
                        let byte_offset = builder.ins().imul(idx_plus_one, slot_size);
                        builder.ins().store(MemFlags::trusted(), val, arr_ptr, 0);
                        // Use iadd for dynamic offset, then store.
                        let target = builder.ins().iadd(arr_ptr, byte_offset);
                        builder.ins().store(MemFlags::trusted(), val, target, 0);
                    }
                    _ => {}
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
            Stmt::FnDecl(_) | Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::ImplBlock(_) => {
                // Already handled at top level.
                Ok(None)
            }
            // Unsupported statements are silently skipped for the JIT subset.
            _ => Ok(None),
        }
    }

    /// Try to resolve the type name for an expression (for struct field access).
    fn resolve_expr_type(&self, expr: &Expr) -> Option<String> {
        match expr {
            Expr::Ident(name) => self.var_types.get(name).cloned(),
            Expr::StructInit(sname, _) => Some(sname.clone()),
            Expr::EnumPath(ename, _, _) => Some(ename.clone()),
            _ => None,
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
                // Method call: obj.method(args)
                if let Expr::Field(obj_expr, method_name) = callee.as_ref() {
                    return self.compile_method_call(builder, obj_expr, method_name, args);
                }

                // Enum variant construction with payload: EnumName::Variant(data)
                // Parser produces Call(EnumPath(name, variant, None), [data])
                if let Expr::EnumPath(enum_name, variant_name, None) = callee.as_ref() {
                    let layout = self.enum_layouts.get(enum_name)
                        .ok_or_else(|| jit_err(format!("undefined enum '{}' in JIT", enum_name)))?
                        .clone();
                    let tag = *layout.variant_tags.get(variant_name)
                        .ok_or_else(|| jit_err(format!(
                            "enum '{}' has no variant '{}'", enum_name, variant_name
                        )))?;
                    let ptr = self.call_alloc(builder, layout.total_size);
                    let tag_val = builder.ins().iconst(I64, tag);
                    self.store_at_offset(builder, ptr, 0, tag_val);
                    // Store the first argument as payload.
                    if let Some(data_expr) = args.first() {
                        let val = self.compile_expr(builder, data_expr)?;
                        self.store_at_offset(builder, ptr, SLOT_SIZE, val);
                    }
                    return Ok(ptr);
                }

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

                    // Handle len() builtin for arrays.
                    if name == "len" && args.len() == 1 {
                        let arr_ptr = self.compile_expr(builder, &args[0])?;
                        // Array layout: first slot is length.
                        return Ok(self.load_from_offset(builder, arr_ptr, 0));
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
                            "undefined function '{}' in JIT",
                            name
                        )))
                    }
                } else {
                    // Indirect call (e.g., closure call).
                    // Closure representation: pointer to [fn_ptr, env_ptr]
                    let closure_ptr = self.compile_expr(builder, callee)?;
                    let fn_ptr_val = self.load_from_offset(builder, closure_ptr, 0);
                    let env_ptr_val = self.load_from_offset(builder, closure_ptr, SLOT_SIZE);

                    // Build signature: env_ptr + args -> i64
                    let mut sig = self.module.make_signature();
                    sig.params.push(AbiParam::new(I64)); // env pointer
                    for _ in args {
                        sig.params.push(AbiParam::new(I64));
                    }
                    sig.returns.push(AbiParam::new(I64));

                    let sig_ref = builder.import_signature(sig);

                    let mut call_args = vec![env_ptr_val];
                    for arg in args {
                        call_args.push(self.compile_expr(builder, arg)?);
                    }

                    let call = builder.ins().call_indirect(sig_ref, fn_ptr_val, &call_args);
                    let results = builder.inst_results(call);
                    Ok(results[0])
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

            // ── Struct creation ──────────────────────────────────────
            Expr::StructInit(name, field_exprs) => {
                let layout = self.struct_layouts.get(name)
                    .ok_or_else(|| jit_err(format!("undefined struct '{}' in JIT", name)))?
                    .clone();

                // Allocate memory for the struct.
                let ptr = self.call_alloc(builder, layout.total_size);

                // Store each field value at its offset.
                for (fname, fexpr) in field_exprs {
                    let val = self.compile_expr(builder, fexpr)?;
                    if let Some(&offset) = layout.offsets.get(fname) {
                        self.store_at_offset(builder, ptr, offset, val);
                    } else {
                        return Err(jit_err(format!(
                            "struct '{}' has no field '{}'", name, fname
                        )));
                    }
                }

                // Return pointer as i64.
                Ok(ptr)
            }

            // ── Struct field access ─────────────────────────────────
            Expr::Field(obj_expr, field_name) => {
                let ptr = self.compile_expr(builder, obj_expr)?;

                // Try to resolve the struct type.
                if let Some(type_name) = self.resolve_expr_type(obj_expr) {
                    if let Some(layout) = self.struct_layouts.get(&type_name) {
                        if let Some(&offset) = layout.offsets.get(field_name) {
                            return Ok(self.load_from_offset(builder, ptr, offset));
                        }
                        return Err(jit_err(format!(
                            "struct '{}' has no field '{}'", type_name, field_name
                        )));
                    }
                }

                // Fallback: try all known struct layouts to find the field.
                for (_sname, layout) in self.struct_layouts.iter() {
                    if let Some(&offset) = layout.offsets.get(field_name) {
                        return Ok(self.load_from_offset(builder, ptr, offset));
                    }
                }

                Err(jit_err(format!("cannot resolve field '{}' in JIT", field_name)))
            }

            // ── Array literal ───────────────────────────────────────
            Expr::Array(items) => {
                let len = items.len() as i64;
                // Layout: [length, elem0, elem1, ...]
                let total_size = (len + 1) * SLOT_SIZE;
                let ptr = self.call_alloc(builder, total_size);

                // Store length at offset 0.
                let len_val = builder.ins().iconst(I64, len);
                self.store_at_offset(builder, ptr, 0, len_val);

                // Store each element.
                for (i, item) in items.iter().enumerate() {
                    let val = self.compile_expr(builder, item)?;
                    let offset = (i as i64 + 1) * SLOT_SIZE;
                    self.store_at_offset(builder, ptr, offset, val);
                }

                Ok(ptr)
            }

            // ── Array indexing ──────────────────────────────────────
            Expr::Index(arr_expr, idx_expr) => {
                let arr_ptr = self.compile_expr(builder, arr_expr)?;
                let idx = self.compile_expr(builder, idx_expr)?;

                // Bounds check: load length from offset 0.
                let length = self.load_from_offset(builder, arr_ptr, 0);
                let bc_ref = self
                    .module
                    .declare_func_in_func(self.bounds_check_id, &mut builder.func);
                builder.ins().call(bc_ref, &[idx, length]);

                // Load element: offset = (idx + 1) * 8.
                let one = builder.ins().iconst(I64, 1);
                let idx_plus_one = builder.ins().iadd(idx, one);
                let slot = builder.ins().iconst(I64, SLOT_SIZE);
                let byte_offset = builder.ins().imul(idx_plus_one, slot);
                let elem_ptr = builder.ins().iadd(arr_ptr, byte_offset);
                Ok(builder.ins().load(I64, MemFlags::trusted(), elem_ptr, 0))
            }

            // ── Enum variant creation ───────────────────────────────
            Expr::EnumPath(enum_name, variant_name, data_expr) => {
                let layout = self.enum_layouts.get(enum_name)
                    .ok_or_else(|| jit_err(format!("undefined enum '{}' in JIT", enum_name)))?
                    .clone();

                let tag = *layout.variant_tags.get(variant_name)
                    .ok_or_else(|| jit_err(format!(
                        "enum '{}' has no variant '{}'", enum_name, variant_name
                    )))?;

                // Allocate: 8-byte tag + 8-byte payload = 16 bytes.
                let ptr = self.call_alloc(builder, layout.total_size);

                // Store tag at offset 0.
                let tag_val = builder.ins().iconst(I64, tag);
                self.store_at_offset(builder, ptr, 0, tag_val);

                // Store payload at offset 8 (if present).
                if let Some(data) = data_expr {
                    let val = self.compile_expr(builder, data)?;
                    self.store_at_offset(builder, ptr, SLOT_SIZE, val);
                }

                Ok(ptr)
            }

            // ── Match expression (enum tag switch) ──────────────────
            Expr::Match(scrutinee, arms) => {
                let scrut_val = self.compile_expr(builder, scrutinee)?;

                // Store scrutinee pointer in a variable for cross-block use.
                let scrut_var = self.declare_var("__match_scrut__", builder);
                builder.def_var(scrut_var, scrut_val);

                // Load the tag from the enum pointer (offset 0).
                let tag_var = self.declare_var("__match_tag__", builder);
                let tag = self.load_from_offset(builder, scrut_val, 0);
                builder.def_var(tag_var, tag);

                let merge_bb = builder.create_block();
                let merge_param = builder.append_block_param(merge_bb, I64);

                // Classify arms into tagged (enum pattern) and wildcard.
                let mut tagged_arms: Vec<(usize, i64)> = Vec::new(); // (arm_index, tag_value)
                let mut wildcard_arm: Option<usize> = None;

                for (i, arm) in arms.iter().enumerate() {
                    match &arm.pattern {
                        Expr::EnumPath(ename, vname, _) => {
                            if let Some(layout) = self.enum_layouts.get(ename) {
                                if let Some(&expected_tag) = layout.variant_tags.get(vname) {
                                    tagged_arms.push((i, expected_tag));
                                    continue;
                                }
                            }
                            wildcard_arm = Some(i);
                        }
                        Expr::IntLit(n) => {
                            tagged_arms.push((i, *n));
                        }
                        _ => {
                            wildcard_arm = Some(i);
                        }
                    }
                }

                // Create arm body blocks.
                let mut arm_blocks: Vec<cranelift::prelude::Block> = Vec::new();
                for _ in arms {
                    arm_blocks.push(builder.create_block());
                }

                // Default block: used when no arm matches.
                let default_bb = builder.create_block();

                // The fallthrough target: wildcard arm or default.
                let fallthrough = if let Some(wa) = wildcard_arm {
                    arm_blocks[wa]
                } else {
                    default_bb
                };

                // Build sequential if-else dispatch chain.
                // Each check: compare tag, branch to arm block or next check.
                // We create intermediate blocks for the chain.
                let num_checks = tagged_arms.len();
                let mut check_bbs: Vec<cranelift::prelude::Block> = Vec::new();
                for _ in 0..num_checks {
                    check_bbs.push(builder.create_block());
                }

                // Jump from current block to first check (or fallthrough if no checks).
                if num_checks == 0 {
                    builder.ins().jump(fallthrough, &[]);
                } else {
                    builder.ins().jump(check_bbs[0], &[]);
                }

                // Emit each check block.
                for (ci, &(arm_idx, expected_tag)) in tagged_arms.iter().enumerate() {
                    builder.seal_block(check_bbs[ci]);
                    builder.switch_to_block(check_bbs[ci]);

                    let cur_tag = builder.use_var(tag_var);
                    let tag_const = builder.ins().iconst(I64, expected_tag);
                    let cmp = builder.ins().icmp(IntCC::Equal, cur_tag, tag_const);

                    let next = if ci + 1 < num_checks {
                        check_bbs[ci + 1]
                    } else {
                        fallthrough
                    };
                    builder.ins().brif(cmp, arm_blocks[arm_idx], &[], next, &[]);
                }

                // Default block: return 0.
                builder.seal_block(default_bb);
                builder.switch_to_block(default_bb);
                let zero = builder.ins().iconst(I64, 0);
                builder.ins().jump(merge_bb, &[zero.into()]);

                // Compile each arm body.
                for (i, arm) in arms.iter().enumerate() {
                    builder.seal_block(arm_blocks[i]);
                    builder.switch_to_block(arm_blocks[i]);

                    // Bind payload variable if pattern has a binding.
                    if let Expr::EnumPath(_, _, Some(binding_expr)) = &arm.pattern {
                        if let Expr::Ident(bind_name) = binding_expr.as_ref() {
                            // Load payload inside the arm block using the saved pointer.
                            let scrut_ptr = builder.use_var(scrut_var);
                            let payload = self.load_from_offset(builder, scrut_ptr, SLOT_SIZE);
                            let var = self.declare_var(bind_name, builder);
                            builder.def_var(var, payload);
                        }
                    }

                    let arm_val = self.compile_block(builder, &arm.body)?;
                    if !self.is_block_terminated(builder) {
                        builder.ins().jump(merge_bb, &[arm_val.into()]);
                    }
                }

                builder.seal_block(merge_bb);
                builder.switch_to_block(merge_bb);
                Ok(merge_param)
            }

            // ── Lambda / closure ────────────────────────────────────
            Expr::Lambda(_params, _body) => {
                // Look up the pre-compiled lambda by counter index.
                let idx = self.lambda_counter;
                self.lambda_counter += 1;

                if idx >= self.lambdas.len() {
                    return Err(jit_err(format!(
                        "lambda index {} out of range (only {} pre-compiled)",
                        idx, self.lambdas.len()
                    )));
                }

                let lambda_info = self.lambdas[idx].clone();

                // Allocate closure struct: [fn_ptr (8 bytes), env_ptr (8 bytes)]
                let closure_ptr = self.call_alloc(builder, 2 * SLOT_SIZE);

                // Get function pointer for the lambda.
                let func_ref = self.module.declare_func_in_func(
                    lambda_info.func_id,
                    &mut builder.func,
                );
                let fn_ptr = builder.ins().func_addr(I64, func_ref);
                self.store_at_offset(builder, closure_ptr, 0, fn_ptr);

                // Allocate environment: one i64 slot per captured variable.
                let num_captures = lambda_info.captures.len() as i64;
                if num_captures > 0 {
                    let env_size = num_captures * SLOT_SIZE;
                    let env_ptr = self.call_alloc(builder, env_size);

                    // Store each captured variable's current value into the env.
                    for (i, cap_name) in lambda_info.captures.iter().enumerate() {
                        if let Some(&var) = self.vars.get(cap_name) {
                            let val = builder.use_var(var);
                            self.store_at_offset(builder, env_ptr, i as i64 * SLOT_SIZE, val);
                        } else {
                            // Variable not found -- store 0.
                            let zero = builder.ins().iconst(I64, 0);
                            self.store_at_offset(builder, env_ptr, i as i64 * SLOT_SIZE, zero);
                        }
                    }

                    self.store_at_offset(builder, closure_ptr, SLOT_SIZE, env_ptr);
                } else {
                    // No captures -- env_ptr = 0.
                    let null_env = builder.ins().iconst(I64, 0);
                    self.store_at_offset(builder, closure_ptr, SLOT_SIZE, null_env);
                }

                // Return the closure pointer.
                Ok(closure_ptr)
            }

            // Unsupported expressions return 0.
            _ => Ok(builder.ins().iconst(I64, 0)),
        }
    }

    /// Compile a method call: obj.method(args).
    fn compile_method_call(
        &mut self,
        builder: &mut FunctionBuilder,
        obj_expr: &Expr,
        method_name: &str,
        args: &[Expr],
    ) -> Result<cranelift::prelude::Value, HexaError> {
        let obj_val = self.compile_expr(builder, obj_expr)?;

        // Try to resolve the object's type.
        if let Some(type_name) = self.resolve_expr_type(obj_expr) {
            // Look for mangled method name: Type_method
            let mangled = format!("{}_{}", type_name, method_name);
            if let Some(info) = self.functions.get(&mangled) {
                // Call with self as first argument.
                let mut arg_vals = vec![obj_val];
                for arg in args {
                    arg_vals.push(self.compile_expr(builder, arg)?);
                }
                let func_ref = self
                    .module
                    .declare_func_in_func(info.id, &mut builder.func);
                let call = builder.ins().call(func_ref, &arg_vals);
                let results = builder.inst_results(call);
                return Ok(results[0]);
            }
        }

        // Fallback: try all impl methods across all types.
        for (type_name, _methods) in self.impl_methods.iter() {
            let mangled = format!("{}_{}", type_name, method_name);
            if let Some(info) = self.functions.get(&mangled) {
                let mut arg_vals = vec![obj_val];
                for arg in args {
                    arg_vals.push(self.compile_expr(builder, arg)?);
                }
                let func_ref = self
                    .module
                    .declare_func_in_func(info.id, &mut builder.func);
                let call = builder.ins().call(func_ref, &arg_vals);
                let results = builder.inst_results(call);
                return Ok(results[0]);
            }
        }

        Err(jit_err(format!(
            "undefined method '{}' in JIT", method_name
        )))
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

    // ── New tests: struct support ───────────────────────────────

    #[test]
    fn test_jit_struct_creation_and_field_access() {
        let src = r#"
struct Point {
    x: int,
    y: int
}
let p = Point { x: 10, y: 20 }
p.x + p.y
"#;
        assert_eq!(jit_run(src), 30);
    }

    #[test]
    fn test_jit_struct_in_function() {
        let src = r#"
struct Vec2 {
    x: int,
    y: int
}
fn dot(a, b) {
    return a + b
}
let v = Vec2 { x: 3, y: 4 }
dot(v.x, v.y)
"#;
        assert_eq!(jit_run(src), 7);
    }

    #[test]
    fn test_jit_struct_method() {
        let src = r#"
struct Point {
    x: int,
    y: int
}
impl Point {
    fn sum(self) {
        return self.x + self.y
    }
}
let p = Point { x: 15, y: 27 }
p.sum()
"#;
        assert_eq!(jit_run(src), 42);
    }

    #[test]
    fn test_jit_struct_three_fields() {
        let src = r#"
struct Triple {
    a: int,
    b: int,
    c: int
}
let t = Triple { a: 100, b: 200, c: 300 }
t.a + t.b + t.c
"#;
        assert_eq!(jit_run(src), 600);
    }

    // ── New tests: array support ────────────────────────────────

    #[test]
    fn test_jit_array_creation_and_index() {
        let src = r#"
let arr = [10, 20, 30, 40, 50]
arr[0] + arr[2] + arr[4]
"#;
        assert_eq!(jit_run(src), 90);
    }

    #[test]
    fn test_jit_array_in_function() {
        let src = r#"
fn sum3(a, b, c) { return a + b + c }
let arr = [7, 8, 9]
sum3(arr[0], arr[1], arr[2])
"#;
        assert_eq!(jit_run(src), 24);
    }

    #[test]
    fn test_jit_array_sum_loop() {
        let src = r#"
let arr = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
let total = 0
let i = 0
while i < 10 {
    total = total + arr[i]
    i = i + 1
}
total
"#;
        assert_eq!(jit_run(src), 55);
    }

    // ── New tests: enum support ─────────────────────────────────

    #[test]
    fn test_jit_enum_creation() {
        let src = r#"
enum Color {
    Red,
    Green,
    Blue
}
let c = Color::Green
c
"#;
        // Green is tag 1, returns the pointer.
        // Since we return the pointer, it will be non-zero.
        let result = jit_run(src);
        assert!(result != 0, "enum pointer should be non-zero");
    }

    #[test]
    fn test_jit_enum_match() {
        let src = r#"
enum Shape {
    Circle(int),
    Square(int)
}
let s = Shape::Circle(5)
match s {
    Shape::Circle(r) -> r * r,
    Shape::Square(side) -> side
}
"#;
        assert_eq!(jit_run(src), 25);
    }

    #[test]
    fn test_jit_enum_match_second_variant() {
        let src = r#"
enum Op {
    Add(int),
    Mul(int)
}
let op = Op::Mul(7)
match op {
    Op::Add(v) -> v + 100,
    Op::Mul(v) -> v * 6
}
"#;
        assert_eq!(jit_run(src), 42);
    }

    // ── New tests: can_jit ──────────────────────────────────────

    #[test]
    fn test_can_jit_simple() {
        let src = "2 + 3";
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        assert!(can_jit(&stmts));
    }

    #[test]
    fn test_can_jit_struct() {
        let src = r#"
struct Point { x: int, y: int }
let p = Point { x: 1, y: 2 }
p.x
"#;
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        assert!(can_jit(&stmts));
    }

    #[test]
    fn test_jit_enum_match_simple_no_binding() {
        // Match without using payload binding — just return constants.
        let src = r#"
enum Dir {
    Up,
    Down
}
let d = Dir::Down
match d {
    Dir::Up -> 1,
    Dir::Down -> 2
}
"#;
        assert_eq!(jit_run(src), 2);
    }
}

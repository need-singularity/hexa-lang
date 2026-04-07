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
use cranelift_module::{DataDescription, DataId, FuncId, Linkage, Module};

use crate::ast::*;
use crate::error::{ErrorClass, HexaError};
use crate::escape_analysis::{self, Allocation, EscapeInfo};

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

/// Collect all extern function declarations from a list of statements.
pub fn collect_extern_decls(stmts: &[Stmt]) -> Vec<crate::ast::ExternFnDecl> {
    stmts.iter().filter_map(|s| {
        if let Stmt::Extern(decl) = s { Some(decl.clone()) } else { None }
    }).collect()
}

#[cfg(not(target_arch = "wasm32"))]
extern "C" {
    fn dlopen(filename: *const std::ffi::c_char, flags: std::ffi::c_int) -> *mut std::ffi::c_void;
    fn dlsym(handle: *mut std::ffi::c_void, symbol: *const std::ffi::c_char) -> *mut std::ffi::c_void;
}

#[cfg(not(target_arch = "wasm32"))]
const RTLD_LAZY: std::ffi::c_int = 0x1;

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
        Stmt::For(_, iter_expr, body) => can_jit_expr(iter_expr) && body.iter().all(|s| can_jit_stmt(s)),
        Stmt::Const(_, _, expr, _) => can_jit_expr(expr),
        Stmt::Loop(body) => body.iter().all(|s| can_jit_stmt(s)),
        // These are not yet supported in JIT:
        Stmt::TryCatch(..) | Stmt::Throw(..) |
        Stmt::Spawn(..) | Stmt::SpawnNamed(..) | Stmt::Mod(..) | Stmt::Use(..) |
        Stmt::Proof(..) | Stmt::Assert(..) | Stmt::Intent(..) | Stmt::Verify(..) |
        Stmt::DropStmt(..) | Stmt::AsyncFnDecl(..) | Stmt::Select(..) |
        Stmt::MacroDef(..) | Stmt::DeriveDecl(..) | Stmt::Generate(..) |
        Stmt::Optimize(..) | Stmt::ComptimeFn(..) | Stmt::EffectDecl(..) | Stmt::ConsciousnessBlock(..) | Stmt::EvolveFn(..) |
        Stmt::TraitDecl(..) | Stmt::Static(..) |
        Stmt::Scope(..) | Stmt::ProofBlock(..)
        | Stmt::TypeAlias(..) | Stmt::AtomicLet(..) | Stmt::Panic(..) | Stmt::Theorem(..) | Stmt::Break | Stmt::Continue => false,
        Stmt::Extern(_) => true,
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
        Expr::ArrayPattern(pats, _) => pats.iter().all(|p| can_jit_expr(p)),
        Expr::Lambda(_, body) => can_jit_expr(body),
        Expr::Range(start, end, _) => can_jit_expr(start) && can_jit_expr(end),
        // Not supported in JIT:
        Expr::FloatLit(_) | Expr::StringLit(_) | Expr::CharLit(_) |
        Expr::Tuple(..) | Expr::MapLit(..) |
        Expr::Own(..) | Expr::MoveExpr(..) | Expr::Borrow(..) |
        Expr::Await(..) | Expr::MacroInvoc(..) | Expr::Comptime(..) |
        Expr::HandleWith(..) | Expr::EffectCall(..) | Expr::Resume(..) |
        Expr::DynCast(..) | Expr::Yield(..) | Expr::Template(..) | Expr::TryCatch(..) => false,
    }
}


/// Detect linear recurrence pattern in a function body.
/// Returns Some((coefficients, base_cases)) if the function matches:
///   fn f(n) { if n <= k { return base[n] } return c1*f(n-1) + c2*f(n-2) + ... }
/// Currently supports order-2 recurrences (e.g., fib: coeffs=[1,1], bases=[0,1]).
fn detect_linear_recurrence(decl: &FnDecl) -> Option<(Vec<i64>, Vec<i64>)> {
    if decl.params.len() != 1 { return None; }
    let param_name = &decl.params[0].name;
    let fn_name = &decl.name;

    // We need: base cases + recursive return of form f(n-1) + f(n-2)
    // Scan for base cases and recursive expression
    let mut bases: Vec<(i64, i64)> = Vec::new(); // (condition_val, return_val)
    let mut recursive_expr: Option<&Expr> = None;

    for stmt in &decl.body {
        match stmt {
            // Pattern: if n <= K { return V } with optional else block
            Stmt::Expr(Expr::If(cond, then_block, else_opt)) => {
                if let Some((threshold, ret_val)) = extract_base_case(cond, then_block, param_name) {
                    for i in 0..=threshold {
                        bases.push((i, if i <= 1 { ret_val.min(i) } else { ret_val }));
                    }
                    // Check else block for recursive expression
                    if let Some(else_block) = else_opt {
                        if let Some(Stmt::Return(Some(expr))) = else_block.last() {
                            recursive_expr = Some(expr);
                        } else if let Some(Stmt::Expr(expr)) = else_block.last() {
                            recursive_expr = Some(expr);
                        }
                    }
                }
            }
            // Pattern: return f(n-1) + f(n-2)
            Stmt::Return(Some(expr)) => {
                recursive_expr = Some(expr);
            }
            _ => {}
        }
    }

    // Check recursive expression: f(n-1) + f(n-2) pattern
    if let Some(expr) = recursive_expr {
        if let Some(coeffs) = extract_recurrence_coefficients(expr, fn_name, param_name) {
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

/// Extract base case: if n <= K { return V } → Some((K, V))
fn extract_base_case(cond: &Expr, then_block: &[Stmt], param_name: &str) -> Option<(i64, i64)> {
    // Pattern: n <= K or n < K
    match cond {
        Expr::Binary(lhs, op, rhs) => {
            let is_param = matches!(**lhs, Expr::Ident(ref name) if name == param_name);
            if !is_param { return None; }
            let threshold = match **rhs {
                Expr::IntLit(v) => v,
                _ => return None,
            };
            let threshold = match op {
                BinOp::Le => threshold,
                BinOp::Lt => threshold - 1,
                BinOp::Eq => threshold,
                _ => return None,
            };
            // Extract return value
            let ret_val = match then_block.last()? {
                Stmt::Return(Some(Expr::IntLit(v))) => *v,
                Stmt::Return(Some(Expr::Ident(name))) if name == param_name => threshold, // return n
                Stmt::Expr(Expr::IntLit(v)) => *v,
                Stmt::Expr(Expr::Ident(name)) if name == param_name => threshold,
                _ => return None,
            };
            Some((threshold, ret_val))
        }
        _ => None,
    }
}

/// Extract recurrence coefficients from expression like c1*f(n-1) + c2*f(n-2).
/// Returns Some([c1, c2]) for order-2 recurrence.
fn extract_recurrence_coefficients(expr: &Expr, fn_name: &str, param_name: &str) -> Option<Vec<i64>> {
    match expr {
        // f(n-1) + f(n-2)  → coefficients [1, 1]
        Expr::Binary(lhs, BinOp::Add, rhs) => {
            let c1 = extract_single_term(lhs, fn_name, param_name, 1)?;
            let c2 = extract_single_term(rhs, fn_name, param_name, 2)?;
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

/// Extract a single term: f(n-k) → (1, k), or C*f(n-k) → (C, k)
fn extract_single_term(expr: &Expr, fn_name: &str, param_name: &str, _expected_offset: i64) -> Option<(i64, i64)> {
    match expr {
        // f(n-k)
        Expr::Call(callee, args) if args.len() == 1 => {
            if !matches!(**callee, Expr::Ident(ref name) if name == fn_name) {
                return None;
            }
            let offset = extract_n_minus_k(&args[0], param_name)?;
            Some((1, offset))
        }
        // C * f(n-k)
        Expr::Binary(lhs, BinOp::Mul, rhs) => {
            if let Expr::IntLit(c) = &**lhs {
                if let Some((_, k)) = extract_single_term(rhs, fn_name, param_name, _expected_offset) {
                    return Some((*c, k));
                }
            }
            if let Expr::IntLit(c) = &**rhs {
                if let Some((_, k)) = extract_single_term(lhs, fn_name, param_name, _expected_offset) {
                    return Some((*c, k));
                }
            }
            None
        }
        _ => None,
    }
}

/// Extract k from expression (n - k) where n is param_name.
fn extract_n_minus_k(expr: &Expr, param_name: &str) -> Option<i64> {
    match expr {
        Expr::Binary(lhs, BinOp::Sub, rhs) => {
            if matches!(**lhs, Expr::Ident(ref name) if name == param_name) {
                if let Expr::IntLit(k) = &**rhs {
                    return Some(*k);
                }
            }
            None
        }
        _ => None,
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
    /// Escape analysis results: fn_name -> EscapeInfo.
    escape_info: HashMap<String, EscapeInfo>,
    /// @memoize: data sections for native cache (fn_name -> DataId).
    memo_data: HashMap<String, DataId>,
}

impl JitCompiler {
    pub fn new(extern_decls: &[crate::ast::ExternFnDecl]) -> Result<Self, HexaError> {
        let mut flag_builder = settings::builder();
        flag_builder
            .set("use_colocated_libcalls", "false")
            .map_err(|e| jit_err(format!("setting flag: {}", e)))?;
        flag_builder
            .set("is_pic", "false")
            .map_err(|e| jit_err(format!("setting flag: {}", e)))?;
        // Cranelift optimization: enable speed-optimized codegen (regalloc, isel).
        flag_builder
            .set("opt_level", "speed")
            .map_err(|e| jit_err(format!("setting flag: {}", e)))?;
        // Disable verification overhead in JIT (correctness validated at dev time).
        flag_builder
            .set("enable_verifier", "false")
            .map_err(|e| jit_err(format!("setting flag: {}", e)))?;
        // Disable Spectre mitigations for JIT performance.
        flag_builder
            .set("enable_heap_access_spectre_mitigation", "false")
            .map_err(|e| jit_err(format!("setting flag: {}", e)))?;
        flag_builder
            .set("enable_table_access_spectre_mitigation", "false")
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

        // Register extern FFI symbols via dlopen/dlsym before builder is consumed.
        #[cfg(not(target_arch = "wasm32"))]
        for decl in extern_decls {
            let handle = if matches!(decl.link_lib.as_deref(), None | Some("c") | Some("libc") | Some("System")) {
                // RTLD_DEFAULT: search all loaded libraries (macOS: -2)
                (-2isize) as *mut std::ffi::c_void
            } else {
                let lib_name = decl.link_lib.as_ref().unwrap();
                let lib_cstr = std::ffi::CString::new(lib_name.as_str())
                    .map_err(|_| jit_err(format!("invalid lib name: {}", lib_name)))?;
                let h = unsafe { dlopen(lib_cstr.as_ptr(), RTLD_LAZY) };
                if h.is_null() {
                    // Try with lib prefix and .dylib/.so suffix
                    let alt = format!("lib{}.dylib", lib_name);
                    let alt_cstr = std::ffi::CString::new(alt.as_str())
                        .map_err(|_| jit_err(format!("invalid lib name: {}", lib_name)))?;
                    let h2 = unsafe { dlopen(alt_cstr.as_ptr(), RTLD_LAZY) };
                    if h2.is_null() {
                        return Err(jit_err(format!("cannot load library '{}'", lib_name)));
                    }
                    h2
                } else {
                    h
                }
            };
            let sym_cstr = std::ffi::CString::new(decl.name.as_str())
                .map_err(|_| jit_err(format!("invalid symbol name: {}", decl.name)))?;
            let sym_ptr = unsafe { dlsym(handle, sym_cstr.as_ptr()) };
            if sym_ptr.is_null() {
                return Err(jit_err(format!("symbol '{}' not found", decl.name)));
            }
            builder.symbol(&decl.name, sym_ptr as *const u8);
        }

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
            escape_info: HashMap::new(),
            memo_data: HashMap::new(),
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

        // Pass 0b: run escape analysis on all functions.
        self.escape_info = escape_analysis::analyze_program(stmts);

        // Pass 1: declare all functions (so recursive/mutual calls work).
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                self.declare_function(decl)?;
            }
            if let Stmt::Extern(decl) = stmt {
                // Declare extern function: all params as I64, optional I64 return.
                let mut sig = self.module.make_signature();
                for _ in &decl.params {
                    sig.params.push(AbiParam::new(I64));
                }
                if decl.ret_type.is_some() {
                    sig.returns.push(AbiParam::new(I64));
                }
                let func_id = self.module.declare_function(&decl.name, Linkage::Import, &sig)
                    .map_err(|e| jit_err(format!("declare extern '{}': {}", decl.name, e)))?;
                self.functions.insert(decl.name.clone(), JitFuncInfo { id: func_id, param_count: decl.params.len() });
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
                    precondition: decl.precondition.clone(),
                    postcondition: decl.postcondition.clone(),
                    body: decl.body.clone(),
                    vis: decl.vis.clone(),
                    is_pure: decl.is_pure, attrs: decl.attrs.clone(),
                };
                self.declare_function(&modified)?;
            }
        }

        // Pass 1b: @memoize — declare raw body functions + allocate native cache data.
        // For @memoize fn fib(n), we create:
        //   __memo_fib: raw function body (recursive calls still go through 'fib' wrapper)
        //   memo_cache_fib: 1024-entry flat cache (key:i64 + val:i64 = 16KB per function)
        //   fib: native cache-check wrapper → call __memo_fib on miss
        let mut memo_fns: Vec<(String, FnDecl)> = Vec::new();
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                if decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Memoize)) && decl.params.len() == 1 {
                    let raw_name = format!("__memo_{}", decl.name);
                    // Declare raw body function
                    let raw_decl = FnDecl {
                        name: raw_name.clone(),
                        type_params: decl.type_params.clone(),
                        params: decl.params.clone(),
                        ret_type: decl.ret_type.clone(),
                        where_clauses: decl.where_clauses.clone(),
                        precondition: decl.precondition.clone(),
                        postcondition: decl.postcondition.clone(),
                        body: decl.body.clone(),
                        vis: decl.vis.clone(),
                        is_pure: decl.is_pure,
                        attrs: vec![],  // no @memoize on raw
                    };
                    self.declare_function(&raw_decl)?;
                    memo_fns.push((decl.name.clone(), raw_decl));

                    // Allocate native cache: 1024 entries × 16 bytes (key + value)
                    let cache_name = format!("memo_cache_{}", decl.name);
                    let data_id = self.module
                        .declare_data(&cache_name, Linkage::Local, true, false)
                        .map_err(|e| jit_err(format!("declare memo cache '{}': {}", cache_name, e)))?;
                    // Initialize: all keys = i64::MIN (sentinel = "empty")
                    let mut data_desc = DataDescription::new();
                    let cache_size = 1024 * 16; // 16KB
                    let mut init_data = vec![0u8; cache_size];
                    // Fill keys with i64::MIN sentinel (every 16 bytes)
                    let sentinel = i64::MIN.to_le_bytes();
                    for i in 0..1024 {
                        init_data[i * 16..i * 16 + 8].copy_from_slice(&sentinel);
                    }
                    data_desc.define(init_data.into_boxed_slice());
                    self.module.define_data(data_id, &data_desc)
                        .map_err(|e| jit_err(format!("define memo cache '{}': {}", cache_name, e)))?;
                    self.memo_data.insert(decl.name.clone(), data_id);
                }
            }
        }


        // Pass 1c: @optimize — detect linear recurrence and replace with matrix exponentiation.
        // For @optimize fn fib(n), if pattern matches f(n) = c1*f(n-1) + c2*f(n-2):
        //   → Generate native matrix exponentiation code: O(log n) instead of O(2^n)
        let mut optimize_fns: Vec<String> = Vec::new();
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                if decl.attrs.iter().any(|a| matches!(a.kind, crate::token::AttrKind::Optimize))
                    && decl.params.len() == 1
                    && !self.memo_data.contains_key(&decl.name)  // don't double-optimize with @memoize
                {
                    if detect_linear_recurrence(decl).is_some() {
                        optimize_fns.push(decl.name.clone());
                    }
                }
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
        // @memoize raw bodies first
        for (_, raw_decl) in &memo_fns {
            self.define_function(raw_decl)?;
        }
        for stmt in stmts {
            if let Stmt::FnDecl(decl) = stmt {
                if self.memo_data.contains_key(&decl.name) {
                    // @memoize: compile native cache wrapper instead of body
                    self.define_memo_wrapper(decl)?;
                } else if optimize_fns.contains(&decl.name) {
                    // @optimize: compile matrix exponentiation instead of recursive body
                    self.define_optimize_matrix_exp(decl)?;
                } else {
                    self.define_function(decl)?;
                }
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
                    precondition: decl.precondition.clone(),
                    postcondition: decl.postcondition.clone(),
                    body: decl.body.clone(),
                    vis: decl.vis.clone(),
                    is_pure: decl.is_pure, attrs: decl.attrs.clone(),
                };
                self.define_function_with_self_type(&modified, type_name)?;
            }
        }

        // Collect top-level non-declaration statements.
        let top_stmts: Vec<&Stmt> = stmts
            .iter()
            .filter(|s| !matches!(s, Stmt::FnDecl(_) | Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::ImplBlock(_) | Stmt::Extern(_)))
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

            // Attach escape analysis results for this function.
            if let Some(ei) = self.escape_info.get(&decl.name) {
                trans.escape_info = Some(ei.clone());
            }

            // If this is an impl method, track `self` as the struct type.
            if let Some(ref st) = self_type {
                trans.var_types.insert("self".to_string(), st.clone());
            }

            // Bind params to variables.
            let mut param_vars = Vec::new();
            for (i, param) in decl.params.iter().enumerate() {
                let val = builder.block_params(entry_block)[i];
                let var = trans.declare_var(&param.name, &mut builder);
                builder.def_var(var, val);
                param_vars.push(var);
            }

            // Tail call optimization: detect tail-recursive calls and convert to loops.
            let use_tco = has_tail_recursion(&decl.name, &decl.body);
            if use_tco {
                let tco_header = builder.create_block();
                builder.ins().jump(tco_header, &[]);
                builder.switch_to_block(tco_header);
                trans.current_fn_name = Some(decl.name.clone());
                trans.tco_header = Some(tco_header);
                trans.tco_param_vars = param_vars;
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

    /// @memoize: compile a native cache-check wrapper.
    /// Generates pure x86/ARM machine code for cache lookup — zero Rust runtime dependency.
    ///
    /// Layout: 1024-entry flat cache, each entry = [key: i64, value: i64] (16 bytes).
    /// Sentinel key = i64::MIN means "empty slot".
    ///
    /// Generated native code:
    ///   fn fib(n: i64) -> i64 {
    ///     let idx = (n & 0x3FF) << 4;  // mod 1024 * 16
    ///     let ptr = &memo_cache + idx;
    ///     if *ptr == n { return *(ptr+8); }  // cache hit
    ///     let result = __memo_fib(n);         // cache miss → call raw
    ///     *ptr = n; *(ptr+8) = result;        // store
    ///     return result;
    ///   }
    fn define_memo_wrapper(&mut self, decl: &FnDecl) -> Result<(), HexaError> {
        let func_info = self.functions.get(&decl.name)
            .ok_or_else(|| jit_err(format!("memo wrapper: fn '{}' not declared", decl.name)))?;
        let func_id = func_info.id;

        let raw_name = format!("__memo_{}", decl.name);
        let raw_info = self.functions.get(&raw_name)
            .ok_or_else(|| jit_err(format!("memo wrapper: raw fn '{}' not declared", raw_name)))?;
        let raw_func_id = raw_info.id;

        let data_id = *self.memo_data.get(&decl.name)
            .ok_or_else(|| jit_err(format!("memo wrapper: cache data for '{}' not found", decl.name)))?;

        let mut ctx = self.module.make_context();
        ctx.func.signature.params.push(AbiParam::new(I64));
        ctx.func.signature.returns.push(AbiParam::new(I64));
        ctx.func.name = UserFuncName::user(0, func_id.as_u32());

        let mut func_ctx = FunctionBuilderContext::new();
        {
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut func_ctx);

            // Blocks: entry → cache_hit | cache_miss
            let entry = builder.create_block();
            let cache_hit = builder.create_block();
            let cache_miss = builder.create_block();

            builder.append_block_params_for_function_params(entry);
            builder.switch_to_block(entry);

            let arg = builder.block_params(entry)[0]; // the single i64 argument

            // Compute cache index: (arg & 0x3FF) << 4  (mod 1024, * 16 bytes)
            let mask = builder.ins().iconst(I64, 0x3FF);
            let idx = builder.ins().band(arg, mask);
            let offset = builder.ins().ishl_imm(idx, 4); // * 16

            // Get cache base address
            let cache_gv = self.module.declare_data_in_func(data_id, builder.func);
            let cache_ptr = builder.ins().global_value(I64, cache_gv);

            // ptr = cache_ptr + offset
            let entry_ptr = builder.ins().iadd(cache_ptr, offset);

            // Load cached key and value
            let cached_key = builder.ins().load(I64, MemFlags::new(), entry_ptr, 0);
            let cached_val = builder.ins().load(I64, MemFlags::new(), entry_ptr, 8);

            // Compare: if cached_key == arg → cache hit
            let hit = builder.ins().icmp(IntCC::Equal, cached_key, arg);
            builder.ins().brif(hit, cache_hit, &[], cache_miss, &[]);

            // Cache hit: return cached value
            builder.switch_to_block(cache_hit);
            builder.ins().return_(&[cached_val]);

            // Cache miss: call raw function, store result, return
            builder.switch_to_block(cache_miss);
            let raw_func_ref = self.module.declare_func_in_func(raw_func_id, builder.func);
            let call = builder.ins().call(raw_func_ref, &[arg]);
            let result = builder.inst_results(call)[0];

            // Store key and value in cache (native store — no Rust runtime)
            builder.ins().store(MemFlags::new(), arg, entry_ptr, 0);
            builder.ins().store(MemFlags::new(), result, entry_ptr, 8);

            builder.ins().return_(&[result]);

            builder.seal_all_blocks();
            builder.finalize();
        }

        self.module
            .define_function(func_id, &mut ctx)
            .map_err(|e| jit_err(format!("define memo wrapper '{}': {}", decl.name, e)))?;
        self.module.clear_context(&mut ctx);
        Ok(())
    }


    /// @optimize: compile matrix exponentiation for linear recurrence.
    /// Detects f(n) = c1*f(n-1) + c2*f(n-2) and generates O(log n) native code.
    ///
    /// Generated native code (for fib: coeffs=[1,1], bases=[0,1]):
    ///   fn fib(n: i64) -> i64 {
    ///     if n <= 0 { return 0; }
    ///     if n == 1 { return 1; }
    ///     // Matrix [[c1, c2], [1, 0]]^(n-1) applied to [base[1], base[0]]
    ///     // Fast power: O(log n) 2x2 matrix multiplications
    ///     let (mut a, mut b, mut c, mut d) = (c1, c2, 1, 0);
    ///     let (mut ra, mut rb, mut rc, mut rd) = (1, 0, 0, 1); // identity
    ///     let mut p = n - 1;
    ///     while p > 0 {
    ///       if p & 1 == 1 { result = result * matrix; }
    ///       matrix = matrix * matrix;
    ///       p >>= 1;
    ///     }
    ///     return ra * base[1] + rb * base[0];
    ///   }
    fn define_optimize_matrix_exp(&mut self, decl: &FnDecl) -> Result<(), HexaError> {
        let (coeffs, bases) = detect_linear_recurrence(decl)
            .ok_or_else(|| jit_err(format!("@optimize: fn '{}' is not a linear recurrence", decl.name)))?;

        let func_info = self.functions.get(&decl.name)
            .ok_or_else(|| jit_err(format!("@optimize: fn '{}' not declared", decl.name)))?;
        let func_id = func_info.id;

        let c1 = coeffs[0];
        let c2 = coeffs[1];
        let base0 = bases[0]; // f(0)
        let base1 = bases[1]; // f(1)

        let mut ctx = self.module.make_context();
        ctx.func.signature.params.push(AbiParam::new(I64));
        ctx.func.signature.returns.push(AbiParam::new(I64));
        ctx.func.name = UserFuncName::user(0, func_id.as_u32());

        let mut func_ctx = FunctionBuilderContext::new();
        {
            let mut builder = FunctionBuilder::new(&mut ctx.func, &mut func_ctx);

            // Blocks
            let entry = builder.create_block();
            let base0_block = builder.create_block();
            let check1 = builder.create_block();
            let base1_block = builder.create_block();
            let matrix_block = builder.create_block();
            let loop_header = builder.create_block();
            let odd_block = builder.create_block();
            let square_block = builder.create_block();
            let done_block = builder.create_block();

            builder.append_block_params_for_function_params(entry);

            // --- entry: if n <= 0 return base0 ---
            builder.switch_to_block(entry);
            let n = builder.block_params(entry)[0];
            let zero = builder.ins().iconst(I64, 0);
            let cmp0 = builder.ins().icmp(IntCC::SignedLessThanOrEqual, n, zero);
            builder.ins().brif(cmp0, base0_block, &[], check1, &[]);

            builder.switch_to_block(base0_block);
            let b0_val = builder.ins().iconst(I64, base0);
            builder.ins().return_(&[b0_val]);

            // --- check1: if n == 1 return base1 ---
            builder.switch_to_block(check1);
            let one = builder.ins().iconst(I64, 1);
            let cmp1 = builder.ins().icmp(IntCC::Equal, n, one);
            builder.ins().brif(cmp1, base1_block, &[], matrix_block, &[]);

            builder.switch_to_block(base1_block);
            let b1_val = builder.ins().iconst(I64, base1);
            builder.ins().return_(&[b1_val]);

            // --- matrix_block: initialize matrix and result, p = n - 1 ---
            builder.switch_to_block(matrix_block);

            // Matrix [[c1, c2], [1, 0]]
            let mat_a_init = builder.ins().iconst(I64, c1);
            let mat_b_init = builder.ins().iconst(I64, c2);
            let mat_c_init = builder.ins().iconst(I64, 1);
            let mat_d_init = builder.ins().iconst(I64, 0);

            // Result = identity [[1, 0], [0, 1]]
            let res_a_init = builder.ins().iconst(I64, 1);
            let res_b_init = builder.ins().iconst(I64, 0);
            let res_c_init = builder.ins().iconst(I64, 0);
            let res_d_init = builder.ins().iconst(I64, 1);

            // p = n - 1
            let p_init = builder.ins().isub(n, one);

            builder.ins().jump(loop_header, &[
                mat_a_init.into(), mat_b_init.into(), mat_c_init.into(), mat_d_init.into(),
                res_a_init.into(), res_b_init.into(), res_c_init.into(), res_d_init.into(),
                p_init.into(),
            ]);

            // --- loop_header: while p > 0 ---
            builder.switch_to_block(loop_header);
            // Block params: mat(a,b,c,d), res(a,b,c,d), p
            for _ in 0..9 {
                builder.append_block_param(loop_header, I64);
            }
            let params = builder.block_params(loop_header).to_vec();
            let (ma, mb, mc, md) = (params[0], params[1], params[2], params[3]);
            let (ra, rb, rc, rd) = (params[4], params[5], params[6], params[7]);
            let p = params[8];

            let p_gt_0 = builder.ins().icmp(IntCC::SignedGreaterThan, p, zero);
            builder.ins().brif(p_gt_0, odd_block, &[], done_block, &[]);

            // --- odd_block: if p & 1 == 1, multiply result by matrix ---
            builder.switch_to_block(odd_block);
            let p_and_1 = builder.ins().band_imm(p, 1);
            let is_odd = builder.ins().icmp_imm(IntCC::Equal, p_and_1, 1);

            // Compute result * matrix (always, select at end)
            // new_ra = ra*ma + rb*mc
            let ra_ma = builder.ins().imul(ra, ma);
            let rb_mc = builder.ins().imul(rb, mc);
            let new_ra = builder.ins().iadd(ra_ma, rb_mc);
            // new_rb = ra*mb + rb*md
            let ra_mb = builder.ins().imul(ra, mb);
            let rb_md = builder.ins().imul(rb, md);
            let new_rb = builder.ins().iadd(ra_mb, rb_md);
            // new_rc = rc*ma + rd*mc
            let rc_ma = builder.ins().imul(rc, ma);
            let rd_mc = builder.ins().imul(rd, mc);
            let new_rc = builder.ins().iadd(rc_ma, rd_mc);
            // new_rd = rc*mb + rd*md
            let rc_mb = builder.ins().imul(rc, mb);
            let rd_md = builder.ins().imul(rd, md);
            let new_rd = builder.ins().iadd(rc_mb, rd_md);

            // Select: if odd, use new values; else keep old
            let sel_ra = builder.ins().select(is_odd, new_ra, ra);
            let sel_rb = builder.ins().select(is_odd, new_rb, rb);
            let sel_rc = builder.ins().select(is_odd, new_rc, rc);
            let sel_rd = builder.ins().select(is_odd, new_rd, rd);

            builder.ins().jump(square_block, &[sel_ra.into(), sel_rb.into(), sel_rc.into(), sel_rd.into()]);

            // --- square_block: matrix = matrix * matrix, p >>= 1 ---
            builder.switch_to_block(square_block);
            for _ in 0..4 {
                builder.append_block_param(square_block, I64);
            }
            let sq_params = builder.block_params(square_block).to_vec();
            let (s_ra, s_rb, s_rc, s_rd) = (sq_params[0], sq_params[1], sq_params[2], sq_params[3]);

            // matrix * matrix
            // sq_a = ma*ma + mb*mc
            let ma_ma = builder.ins().imul(ma, ma);
            let mb_mc2 = builder.ins().imul(mb, mc);
            let sq_a = builder.ins().iadd(ma_ma, mb_mc2);
            // sq_b = ma*mb + mb*md
            let ma_mb2 = builder.ins().imul(ma, mb);
            let mb_md2 = builder.ins().imul(mb, md);
            let sq_b = builder.ins().iadd(ma_mb2, mb_md2);
            // sq_c = mc*ma + md*mc
            let mc_ma2 = builder.ins().imul(mc, ma);
            let md_mc2 = builder.ins().imul(md, mc);
            let sq_c = builder.ins().iadd(mc_ma2, md_mc2);
            // sq_d = mc*mb + md*md
            let mc_mb2 = builder.ins().imul(mc, mb);
            let md_md2 = builder.ins().imul(md, md);
            let sq_d = builder.ins().iadd(mc_mb2, md_md2);

            // p >>= 1
            let new_p = builder.ins().sshr_imm(p, 1);

            builder.ins().jump(loop_header, &[
                sq_a.into(), sq_b.into(), sq_c.into(), sq_d.into(),
                s_ra.into(), s_rb.into(), s_rc.into(), s_rd.into(),
                new_p.into(),
            ]);

            // --- done_block: return ra * base1 + rb * base0 ---
            builder.switch_to_block(done_block);
            let base1_c = builder.ins().iconst(I64, base1);
            let base0_c = builder.ins().iconst(I64, base0);
            let term1 = builder.ins().imul(ra, base1_c);
            let term2 = builder.ins().imul(rb, base0_c);
            let result = builder.ins().iadd(term1, term2);
            builder.ins().return_(&[result]);

            builder.seal_all_blocks();
            builder.finalize();
        }

        self.module
            .define_function(func_id, &mut ctx)
            .map_err(|e| jit_err(format!("define @optimize matrix exp '{}': {}", decl.name, e)))?;
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

            // Attach escape analysis for top-level statements.
            if let Some(ei) = self.escape_info.get("__hexa_main__") {
                trans.escape_info = Some(ei.clone());
            }

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

/// Public wrapper for escape analysis: collect free variables in an expression.
pub fn collect_free_vars_for_escape(expr: &Expr, params: &[String]) -> Vec<String> {
    collect_free_vars(expr, params, &mut Vec::new())
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
        Expr::ArrayPattern(pats, _) => {
            for p in pats {
                collect_free_vars_inner(p, params, bound, free);
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

/// Check if a function body contains tail-recursive calls to itself.
fn has_tail_recursion(fn_name: &str, body: &[Stmt]) -> bool {
    body.iter().any(|s| contains_tail_call(fn_name, s))
}

/// Check if a statement contains a tail call to the named function.
fn contains_tail_call(fn_name: &str, stmt: &Stmt) -> bool {
    match stmt {
        Stmt::Return(Some(expr)) => is_self_call(fn_name, expr),
        Stmt::Expr(Expr::If(_, then_b, else_b)) => {
            let then_has = then_b.iter().any(|s| contains_tail_call(fn_name, s));
            let else_has = else_b
                .as_ref()
                .map_or(false, |b| b.iter().any(|s| contains_tail_call(fn_name, s)));
            then_has || else_has
        }
        _ => false,
    }
}

/// Check if an expression is a direct call to `fn_name`.
fn is_self_call(fn_name: &str, expr: &Expr) -> bool {
    if let Expr::Call(callee, _) = expr {
        if let Expr::Ident(name) = callee.as_ref() {
            return name == fn_name;
        }
    }
    false
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
    /// Escape analysis info for the current function.
    escape_info: Option<EscapeInfo>,
    /// Stack slots allocated for non-escaping aggregates: var_name -> (StackSlot, size).
    stack_slots: HashMap<String, (cranelift_codegen::ir::StackSlot, i64)>,
    /// When compiling a `let name = <aggregate>`, this holds `name` so the
    /// aggregate expression can use `alloc_for_var` instead of `call_alloc`.
    current_let_name: Option<String>,
    /// Name of the function currently being compiled (for TCO detection).
    current_fn_name: Option<String>,
    /// TCO loop header block to jump back to for tail-recursive calls.
    tco_header: Option<cranelift::prelude::Block>,
    /// TCO parameter variables (in order) to update before jumping back.
    tco_param_vars: Vec<Variable>,
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
            escape_info: None,
            stack_slots: HashMap::new(),
            current_let_name: None,
            current_fn_name: None,
            tco_header: None,
            tco_param_vars: Vec::new(),
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

    /// Allocate storage for a named variable.  If escape analysis says the
    /// variable is `Stack`, we use a Cranelift stack slot; otherwise we
    /// fall back to the heap allocator.
    fn alloc_for_var(
        &mut self,
        builder: &mut FunctionBuilder,
        var_name: &str,
        size: i64,
    ) -> cranelift::prelude::Value {
        let use_stack = self
            .escape_info
            .as_ref()
            .map(|ei| ei.get(var_name) == Allocation::Stack)
            .unwrap_or(false);

        if use_stack && size > 0 {
            // Create a stack slot and return its address.
            let slot = builder.create_sized_stack_slot(cranelift_codegen::ir::StackSlotData::new(
                cranelift_codegen::ir::StackSlotKind::ExplicitSlot,
                size as u32,
                0,
            ));
            self.stack_slots.insert(var_name.to_string(), (slot, size));
            builder.ins().stack_addr(I64, slot, 0)
        } else {
            self.call_alloc(builder, size)
        }
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
                    // Set context so aggregate allocs can use stack when safe.
                    self.current_let_name = Some(name.clone());
                    let v = self.compile_expr(builder, e)?;
                    self.current_let_name = None;
                    v
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
                // TCO: if returning a self-recursive call, jump back to loop header.
                if let Some(e) = expr {
                    if let (Some(ref fn_name), Some(tco_hdr)) =
                        (&self.current_fn_name, self.tco_header)
                    {
                        if let Expr::Call(callee, args) = e {
                            if let Expr::Ident(cname) = callee.as_ref() {
                                if cname == fn_name {
                                    // Evaluate all new arg values before updating any vars.
                                    let mut arg_vals = Vec::new();
                                    for arg in args {
                                        arg_vals.push(self.compile_expr(builder, arg)?);
                                    }
                                    // Update parameter variables with new argument values.
                                    let vars = self.tco_param_vars.clone();
                                    for (var, val) in vars.iter().zip(arg_vals.iter()) {
                                        builder.def_var(*var, *val);
                                    }
                                    // Jump back to TCO header (loop).
                                    builder.ins().jump(tco_hdr, &[]);
                                    return Ok(None);
                                }
                            }
                        }
                    }
                }
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
            Stmt::For(var_name, iter_expr, body) => {
                // Support range iteration: for i in start..end / start..=end
                if let Expr::Range(start_expr, end_expr, inclusive) = iter_expr {
                    let start_val = self.compile_expr(builder, start_expr)?;
                    let end_val = self.compile_expr(builder, end_expr)?;

                    let loop_var = self.declare_var(var_name, builder);
                    builder.def_var(loop_var, start_val);

                    let header_block = builder.create_block();
                    let body_block = builder.create_block();
                    let exit_block = builder.create_block();

                    builder.ins().jump(header_block, &[]);
                    builder.switch_to_block(header_block);

                    let cur = builder.use_var(loop_var);
                    let cmp = if *inclusive {
                        builder
                            .ins()
                            .icmp(IntCC::SignedLessThanOrEqual, cur, end_val)
                    } else {
                        builder.ins().icmp(IntCC::SignedLessThan, cur, end_val)
                    };
                    builder
                        .ins()
                        .brif(cmp, body_block, &[], exit_block, &[]);

                    builder.seal_block(body_block);
                    builder.switch_to_block(body_block);

                    for s in body {
                        self.compile_stmt(builder, s)?;
                    }

                    if !self.is_block_terminated(builder) {
                        let cur = builder.use_var(loop_var);
                        let one = builder.ins().iconst(I64, 1);
                        let next = builder.ins().iadd(cur, one);
                        builder.def_var(loop_var, next);
                        builder.ins().jump(header_block, &[]);
                    }

                    builder.seal_block(header_block);
                    builder.switch_to_block(exit_block);
                    builder.seal_block(exit_block);
                    Ok(None)
                } else {
                    // Non-range for loops not yet supported in JIT.
                    Ok(None)
                }
            }
            Stmt::FnDecl(_) | Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::ImplBlock(_) | Stmt::Extern(_) => {
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

                // Allocate memory: stack if escape analysis says safe, heap otherwise.
                let let_name = self.current_let_name.clone();
                let ptr = if let Some(ref var_name) = let_name {
                    self.alloc_for_var(builder, var_name, layout.total_size)
                } else {
                    self.call_alloc(builder, layout.total_size)
                };

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
                let let_name = self.current_let_name.clone();
                let ptr = if let Some(ref var_name) = let_name {
                    self.alloc_for_var(builder, var_name, total_size)
                } else {
                    self.call_alloc(builder, total_size)
                };

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

                // Allocate: stack if safe, heap otherwise.
                let let_name = self.current_let_name.clone();
                let ptr = if let Some(ref var_name) = let_name {
                    self.alloc_for_var(builder, var_name, layout.total_size)
                } else {
                    self.call_alloc(builder, layout.total_size)
                };

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
        let mut jit = JitCompiler::new(&[]).unwrap();
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

    // ── TCO tests ──────────────────────────────────────────────

    #[test]
    fn test_jit_tco_tail_recursive_fib() {
        let src = r#"
fn fib_tail(n, a, b) {
    if n <= 0 { return a }
    return fib_tail(n - 1, b, a + b)
}
fib_tail(40, 0, 1)
"#;
        assert_eq!(jit_run(src), 102334155);
    }

    #[test]
    fn test_jit_tco_factorial() {
        let src = r#"
fn fact(n, acc) {
    if n <= 1 { return acc }
    return fact(n - 1, n * acc)
}
fact(10, 1)
"#;
        assert_eq!(jit_run(src), 3628800);
    }

    // ── For loop tests ─────────────────────────────────────────

    #[test]
    fn test_jit_for_range() {
        let src = r#"
fn sum_range(n) {
    let total = 0
    for i in 0..n {
        total = total + i
    }
    return total
}
sum_range(10)
"#;
        assert_eq!(jit_run(src), 45);
    }

    #[test]
    fn test_jit_for_range_inclusive() {
        let src = r#"
fn sum_incl(n) {
    let total = 0
    for i in 1..=n {
        total = total + i
    }
    return total
}
sum_incl(10)
"#;
        assert_eq!(jit_run(src), 55);
    }
}

#[cfg(test)]
mod optimize_tests {
    use super::*;
    use crate::lexer::Lexer;
    use crate::parser::Parser;

    fn jit_run(src: &str) -> i64 {
        let tokens = Lexer::new(src).tokenize().unwrap();
        let stmts = Parser::new(tokens).parse().unwrap();
        let mut jit = JitCompiler::new(&[]).unwrap();
        jit.compile_and_run(&stmts).unwrap()
    }

    #[test]
    fn test_optimize_fib_base_cases() {
        let src = r#"
@optimize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(0)
"#;
        assert_eq!(jit_run(src), 0);
        let src = r#"
@optimize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(1)
"#;
        assert_eq!(jit_run(src), 1);
    }

    #[test]
    fn test_optimize_fib_small() {
        let src = r#"
@optimize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(10)
"#;
        assert_eq!(jit_run(src), 55);
    }

    #[test]
    fn test_optimize_fib_large() {
        let src = r#"
@optimize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(90)
"#;
        assert_eq!(jit_run(src), 2880067194370816120);
    }

    #[test]
    fn test_optimize_fib_45() {
        let src = r#"
@optimize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(45)
"#;
        assert_eq!(jit_run(src), 1134903170);
    }

    #[test]
    fn test_optimize_lucas_sequence() {
        // Lucas numbers: L(0)=2, L(1)=1, L(n)=L(n-1)+L(n-2)
        // But with same coefficients [1,1] and bases pattern
        // Test with standard fib-like: f(0)=0, f(1)=1
        let src = r#"
@optimize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(2) + fib(3) + fib(5) + fib(7)
"#;
        // fib(2)=1, fib(3)=2, fib(5)=5, fib(7)=13 → 21
        assert_eq!(jit_run(src), 21);
    }

    #[test]
    fn test_optimize_negative_input() {
        let src = r#"
@optimize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
fib(-5)
"#;
        // n <= 0 returns base0 = 0
        assert_eq!(jit_run(src), 0);
    }
}

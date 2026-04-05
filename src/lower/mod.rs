//! AST → HEXA-IR Lowering — converts the full AST to IR.
//!
//! Pipeline stage 4/6: Lex → Parse → Sema → **Lower** → Opt → Codegen

pub mod expr;
pub mod stmt;
pub mod pattern;

use std::collections::HashMap;
use crate::ast;
use crate::ir::{self, IrModule, IrFunction, IrBuilder, IrType, ValueId, BlockId, FuncId, Operand};

/// Single variable binding entry in the shadow stack.
#[derive(Debug, Clone)]
struct ScopeEntry {
    val: ValueId,
    ty: IrType,
}

/// Lowering context — tracks scope, types, and declarations.
/// NOTE: module is kept separate to avoid borrow conflicts with IrBuilder.
///
/// Scope lookup is O(1) via a flat HashMap keyed by name. Each name maps
/// to a shadow stack (Vec<ScopeEntry>); the top is the current binding.
/// `scope_frames[d]` lists names defined at depth `d` for O(k) pop.
pub struct LowerCtx {
    /// Flat name → shadow stack. Top of each stack is the active binding.
    flat_lookup: HashMap<String, Vec<ScopeEntry>>,
    /// Per-scope list of names defined at that depth (for pop_scope unwind).
    scope_frames: Vec<Vec<String>>,
    /// Function name → FuncId.
    pub func_map: HashMap<String, FuncId>,
    /// Struct name → field layout.
    pub struct_map: HashMap<String, Vec<(String, IrType)>>,
    /// Enum name → variant layout.
    pub enum_map: HashMap<String, Vec<(String, Option<IrType>)>>,
    /// Break/continue target blocks for loops.
    pub loop_stack: Vec<(BlockId, BlockId)>,
}

impl LowerCtx {
    pub fn new() -> Self {
        Self {
            flat_lookup: HashMap::new(),
            scope_frames: vec![Vec::new()],
            func_map: HashMap::new(),
            struct_map: HashMap::new(),
            enum_map: HashMap::new(),
            loop_stack: Vec::new(),
        }
    }

    pub fn push_scope(&mut self) {
        self.scope_frames.push(Vec::new());
    }

    pub fn pop_scope(&mut self) {
        if let Some(names) = self.scope_frames.pop() {
            for name in names {
                if let Some(stack) = self.flat_lookup.get_mut(&name) {
                    stack.pop();
                    if stack.is_empty() {
                        self.flat_lookup.remove(&name);
                    }
                }
            }
        }
    }

    pub fn define_var(&mut self, name: &str, val: ValueId, ty: IrType) {
        // Track name in current frame so pop_scope can unwind it.
        if let Some(frame) = self.scope_frames.last_mut() {
            frame.push(name.to_string());
        }
        self.flat_lookup
            .entry(name.to_string())
            .or_insert_with(Vec::new)
            .push(ScopeEntry { val, ty });
    }

    /// O(1) variable lookup. Returns owned (ValueId, IrType) for API
    /// parity with the previous implementation; IrType clone is only
    /// needed for callers that wrap the type (e.g. IrType::Ptr(Box::new(..))).
    pub fn lookup_var(&self, name: &str) -> Option<(ValueId, IrType)> {
        self.flat_lookup
            .get(name)
            .and_then(|stack| stack.last())
            .map(|e| (e.val, e.ty.clone()))
    }

    /// O(1) lookup returning only the ValueId (no IrType clone).
    /// Prefer this when the type isn't needed at the call site.
    #[inline]
    pub fn lookup_var_id(&self, name: &str) -> Option<ValueId> {
        self.flat_lookup
            .get(name)
            .and_then(|stack| stack.last())
            .map(|e| e.val)
    }

    pub fn resolve_type(&self, type_str: &str) -> IrType {
        match type_str {
            "i8" => IrType::I8,
            "i16" => IrType::I16,
            "i32" => IrType::I32,
            "i64" | "int" => IrType::I64,
            "f32" => IrType::F32,
            "f64" | "float" => IrType::F64,
            "bool" => IrType::Bool,
            "byte" => IrType::Byte,
            "str" | "string" | "String" => IrType::Str,
            "void" | "()" => IrType::Void,
            name => {
                if self.struct_map.contains_key(name) {
                    IrType::Struct(ir::types::StructType {
                        name: name.to_string(),
                        fields: self.struct_map[name].clone(),
                    })
                } else if self.enum_map.contains_key(name) {
                    IrType::Enum(ir::types::EnumType {
                        name: name.to_string(),
                        variants: self.enum_map[name].clone(),
                    })
                } else {
                    IrType::I64
                }
            }
        }
    }

    pub fn resolve_type_opt(&self, t: &Option<String>) -> IrType {
        match t {
            Some(s) => self.resolve_type(s),
            None => IrType::I64,
        }
    }
}

/// Lower an entire program (list of statements) to an IR module.
pub fn lower_program(stmts: &[ast::Stmt], module_name: &str) -> IrModule {
    let mut module = IrModule::new(module_name);
    let mut ctx = LowerCtx::new();

    // First pass: register all function/struct/enum declarations
    for s in stmts {
        stmt::register_decl(&mut ctx, &mut module, s);
    }

    // Second pass: lower top-level statements into a "main" function
    // If an explicit fn main() was declared, skip auto-generating one
    let has_explicit_main = ctx.func_map.contains_key("main");
    if !has_explicit_main {
        let main_id = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(main_id).unwrap();
            let mut builder = IrBuilder::new(func);

            for s in stmts {
                stmt::lower_stmt(&mut ctx, &mut builder, s);
            }

            // Ensure main returns 0
            if !builder.is_current_block_terminated() {
                let zero = builder.const_i64(0);
                builder.ret(Some(zero));
            }
        }
    }

    // Third pass: lower function bodies
    let func_names: Vec<(String, FuncId)> = ctx.func_map.iter()
        .map(|(k, v)| (k.clone(), *v))
        .collect();
    for (name, fid) in func_names {
        if let Some(decl) = find_fn_decl(stmts, &name) {
            let params: Vec<(String, IrType)> = decl.params.iter()
                .map(|p| (p.name.clone(), ctx.resolve_type_opt(&p.typ)))
                .collect();
            let func = module.function_mut(fid).unwrap();
            let mut builder = IrBuilder::new(func);

            ctx.push_scope();
            // Bind parameters: alloca + explicit Store of param value.
            // Load(Param(i)) materializes the i-th argument register as an SSA value.
            // P2 mem2reg promotes Store→Load to Copy, eliminating stack round-trips.
            for (i, (pname, pty)) in params.iter().enumerate() {
                let ptr = builder.alloc(pty.clone());
                let param_val = builder.load_param(i, pty.clone());
                builder.store(ptr, param_val);
                ctx.define_var(pname, ptr, IrType::Ptr(Box::new(pty.clone())));
            }
            let mut last = builder.const_i64(0);
            for s in &decl.body {
                last = stmt::lower_stmt_val(&mut ctx, &mut builder, s);
            }
            if !builder.is_current_block_terminated() {
                builder.ret(Some(last));
            }
            ctx.pop_scope();
        }
    }

    module
}

fn find_fn_decl<'a>(stmts: &'a [ast::Stmt], name: &str) -> Option<&'a ast::FnDecl> {
    for s in stmts {
        match s {
            ast::Stmt::FnDecl(d) if d.name == name => return Some(d),
            ast::Stmt::AsyncFnDecl(d) if d.name == name => return Some(d),
            ast::Stmt::ComptimeFn(d) if d.name == name => return Some(d),
            _ => {}
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_lower_empty_program() {
        let module = lower_program(&[], "test");
        assert_eq!(module.name, "test");
        assert!(module.find_function("main").is_some());
    }

    #[test]
    fn test_scope_push_pop() {
        let mut ctx = LowerCtx::new();
        ctx.define_var("x", ir::ValueId(0), IrType::I64);
        assert!(ctx.lookup_var("x").is_some());

        ctx.push_scope();
        ctx.define_var("y", ir::ValueId(1), IrType::I64);
        assert!(ctx.lookup_var("x").is_some());
        assert!(ctx.lookup_var("y").is_some());

        ctx.pop_scope();
        assert!(ctx.lookup_var("x").is_some());
        assert!(ctx.lookup_var("y").is_none());
    }

    #[test]
    fn test_scope_shadowing() {
        let mut ctx = LowerCtx::new();
        ctx.define_var("x", ir::ValueId(10), IrType::I64);
        assert_eq!(ctx.lookup_var("x").unwrap().0, ir::ValueId(10));

        ctx.push_scope();
        ctx.define_var("x", ir::ValueId(20), IrType::I32);
        // Inner shadows outer
        assert_eq!(ctx.lookup_var("x").unwrap().0, ir::ValueId(20));
        assert_eq!(ctx.lookup_var("x").unwrap().1, IrType::I32);

        ctx.push_scope();
        ctx.define_var("x", ir::ValueId(30), IrType::Bool);
        assert_eq!(ctx.lookup_var("x").unwrap().0, ir::ValueId(30));

        ctx.pop_scope();
        // Restored to middle scope
        assert_eq!(ctx.lookup_var("x").unwrap().0, ir::ValueId(20));

        ctx.pop_scope();
        // Restored to outer
        assert_eq!(ctx.lookup_var("x").unwrap().0, ir::ValueId(10));
        assert_eq!(ctx.lookup_var_id("x"), Some(ir::ValueId(10)));
    }

    #[test]
    fn test_resolve_types() {
        let ctx = LowerCtx::new();
        assert_eq!(ctx.resolve_type("i64"), IrType::I64);
        assert_eq!(ctx.resolve_type("int"), IrType::I64);
        assert_eq!(ctx.resolve_type("f64"), IrType::F64);
        assert_eq!(ctx.resolve_type("bool"), IrType::Bool);
        assert_eq!(ctx.resolve_type("str"), IrType::Str);
        assert_eq!(ctx.resolve_type("void"), IrType::Void);
    }
}

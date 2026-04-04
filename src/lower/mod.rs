//! AST → HEXA-IR Lowering — converts the full AST to IR.
//!
//! Pipeline stage 4/6: Lex → Parse → Sema → **Lower** → Opt → Codegen

pub mod expr;
pub mod stmt;
pub mod pattern;

use std::collections::HashMap;
use crate::ast;
use crate::ir::{self, IrModule, IrFunction, IrBuilder, IrType, ValueId, BlockId, FuncId, Operand};

/// Lowering context — tracks scope, types, and declarations.
/// NOTE: module is kept separate to avoid borrow conflicts with IrBuilder.
pub struct LowerCtx {
    /// Variable name → (ValueId, IrType) in current scope.
    pub scopes: Vec<HashMap<String, (ValueId, IrType)>>,
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
            scopes: vec![HashMap::new()],
            func_map: HashMap::new(),
            struct_map: HashMap::new(),
            enum_map: HashMap::new(),
            loop_stack: Vec::new(),
        }
    }

    pub fn push_scope(&mut self) {
        self.scopes.push(HashMap::new());
    }

    pub fn pop_scope(&mut self) {
        self.scopes.pop();
    }

    pub fn define_var(&mut self, name: &str, val: ValueId, ty: IrType) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(name.to_string(), (val, ty));
        }
    }

    pub fn lookup_var(&self, name: &str) -> Option<(ValueId, IrType)> {
        for scope in self.scopes.iter().rev() {
            if let Some(v) = scope.get(name) {
                return Some(v.clone());
            }
        }
        None
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
            if !builder.is_last_block_terminated() {
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
            // Bind parameters: alloca only (no store — codegen handles arg→alloca)
            // Convention: first N allocas in entry block = parameter slots
            for (_i, (pname, pty)) in params.iter().enumerate() {
                let ptr = builder.alloc(pty.clone());
                // No store here — codegen's prologue stores x0,x1,... to these slots
                ctx.define_var(pname, ptr, IrType::Ptr(Box::new(pty.clone())));
            }
            let mut last = builder.const_i64(0);
            for s in &decl.body {
                last = stmt::lower_stmt_val(&mut ctx, &mut builder, s);
            }
            if !builder.is_last_block_terminated() {
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

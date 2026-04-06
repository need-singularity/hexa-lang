//! Statement lowering — AST Stmt → HEXA-IR instructions.

use crate::ast::{Stmt, Expr};
use crate::ir::{IrBuilder, IrModule, IrType, ValueId};
use super::LowerCtx;
use super::expr::lower_expr;

/// Register type/function declarations (first pass — no codegen).
pub fn register_decl(ctx: &mut LowerCtx, module: &mut IrModule, stmt: &Stmt) {
    match stmt {
        Stmt::FnDecl(d) | Stmt::AsyncFnDecl(d) | Stmt::ComptimeFn(d) | Stmt::EvolveFn(d) => {
            let params: Vec<(String, IrType)> = d.params.iter()
                .map(|p| (p.name.clone(), ctx.resolve_type_opt(&p.typ)))
                .collect();
            let ret = d.ret_type.as_ref()
                .map(|t| ctx.resolve_type(t))
                .unwrap_or(IrType::Void);
            let fid = module.add_function(d.name.clone(), params, ret);
            ctx.func_map.insert(d.name.clone(), fid);
        }
        Stmt::StructDecl(d) => {
            let fields: Vec<(String, IrType)> = d.fields.iter()
                .map(|(name, ty, _vis)| (name.clone(), ctx.resolve_type(ty)))
                .collect();
            ctx.struct_map.insert(d.name.clone(), fields);
        }
        Stmt::EnumDecl(d) => {
            let variants: Vec<(String, Option<IrType>)> = d.variants.iter()
                .map(|(name, types)| {
                    let payload = types.as_ref().and_then(|ts| {
                        ts.first().map(|t| ctx.resolve_type(t))
                    });
                    (name.clone(), payload)
                })
                .collect();
            ctx.enum_map.insert(d.name.clone(), variants);
        }
        Stmt::Mod(_name, body) => {
            for s in body {
                register_decl(ctx, module, s);
            }
        }
        _ => {}
    }
}

/// Lower a statement (no return value needed).
pub fn lower_stmt(ctx: &mut LowerCtx, builder: &mut IrBuilder, stmt: &Stmt) {
    lower_stmt_val(ctx, builder, stmt);
}

/// Lower a statement, returning the last computed value.
pub fn lower_stmt_val(ctx: &mut LowerCtx, builder: &mut IrBuilder, stmt: &Stmt) -> ValueId {
    match stmt {
        Stmt::Let(name, typ, init, _vis) => {
            let ty = ctx.resolve_type_opt(typ);
            // Allocate stack slot for mutable variable
            let ptr = builder.alloc(ty.clone());
            let val = match init {
                Some(e) => lower_expr(ctx, builder, e),
                None => builder.const_i64(0),
            };
            builder.store(ptr, val);
            ctx.define_var(name, ptr, IrType::Ptr(Box::new(ty)));
            val
        }

        Stmt::Const(name, typ, init, _vis) | Stmt::Static(name, typ, init, _vis) => {
            let ty = ctx.resolve_type_opt(typ);
            let ptr = builder.alloc(ty.clone());
            let val = lower_expr(ctx, builder, init);
            builder.store(ptr, val);
            ctx.define_var(name, ptr, IrType::Ptr(Box::new(ty)));
            val
        }

        Stmt::Assign(lhs, rhs) => {
            let val = lower_expr(ctx, builder, rhs);
            if let Expr::Ident(name) = lhs {
                if let Some(ptr) = ctx.lookup_var_id(name) {
                    builder.store(ptr, val);
                }
            } else if let Expr::Field(obj, field) = lhs {
                let obj_val = lower_expr(ctx, builder, obj);
                let idx = super::expr::resolve_field_idx_pub(ctx, obj, field);
                builder.struct_store_field(obj_val, idx, val);
            } else if let Expr::Index(arr, idx_expr) = lhs {
                let arr_val = lower_expr(ctx, builder, arr);
                let idx_val = lower_expr(ctx, builder, idx_expr);
                builder.array_store(arr_val, idx_val, val);
            }
            val
        }

        Stmt::Expr(e) => lower_expr(ctx, builder, e),

        Stmt::Return(val) => {
            let v = val.as_ref().map(|e| lower_expr(ctx, builder, e));
            builder.ret(v)
        }

        Stmt::While(cond, body) => {
            let cond_bb = builder.create_block("while.cond");
            let body_bb = builder.create_block("while.body");
            let exit_bb = builder.create_block("while.exit");

            builder.jump(cond_bb);
            builder.switch_to(cond_bb);
            let cond_val = lower_expr(ctx, builder, cond);
            builder.branch(cond_val, body_bb, exit_bb);

            builder.switch_to(body_bb);
            ctx.push_scope();
            ctx.loop_stack.push((cond_bb, exit_bb));
            for s in body { lower_stmt(ctx, builder, s); }
            ctx.loop_stack.pop();
            ctx.pop_scope();
            if !builder.func.block(builder.current_block).map_or(false, |b| b.is_terminated()) {
                builder.jump(cond_bb);
            }

            builder.switch_to(exit_bb);
            builder.const_i64(0)
        }

        Stmt::For(var, iter_expr, body) => {
            let range = lower_expr(ctx, builder, iter_expr);
            let start = builder.struct_field(range, 0, IrType::I64);
            let end = builder.struct_field(range, 1, IrType::I64);

            let cond_bb = builder.create_block("for.cond");
            let body_bb = builder.create_block("for.body");
            let inc_bb = builder.create_block("for.inc");
            let exit_bb = builder.create_block("for.exit");

            ctx.define_var(var, start, IrType::I64);
            builder.jump(cond_bb);

            builder.switch_to(cond_bb);
            let i = ctx.lookup_var_id(var).unwrap_or(start);
            let cond_val = builder.emit_cmp_lt(i, end);
            builder.branch(cond_val, body_bb, exit_bb);

            builder.switch_to(body_bb);
            ctx.push_scope();
            ctx.loop_stack.push((inc_bb, exit_bb));
            for s in body { lower_stmt(ctx, builder, s); }
            ctx.loop_stack.pop();
            ctx.pop_scope();
            if !builder.func.block(builder.current_block).map_or(false, |b| b.is_terminated()) {
                builder.jump(inc_bb);
            }

            builder.switch_to(inc_bb);
            let cur = ctx.lookup_var_id(var).unwrap_or(start);
            let one = builder.const_i64(1);
            let next = builder.add(cur, one, IrType::I64);
            ctx.define_var(var, next, IrType::I64);
            builder.jump(cond_bb);

            builder.switch_to(exit_bb);
            builder.const_i64(0)
        }

        Stmt::Loop(body) => {
            let body_bb = builder.create_block("loop.body");
            let exit_bb = builder.create_block("loop.exit");

            builder.jump(body_bb);
            builder.switch_to(body_bb);
            ctx.push_scope();
            ctx.loop_stack.push((body_bb, exit_bb));
            for s in body { lower_stmt(ctx, builder, s); }
            ctx.loop_stack.pop();
            ctx.pop_scope();
            if !builder.func.block(builder.current_block).map_or(false, |b| b.is_terminated()) {
                builder.jump(body_bb);
            }

            builder.switch_to(exit_bb);
            builder.const_i64(0)
        }

        Stmt::FnDecl(_) | Stmt::AsyncFnDecl(_) | Stmt::ComptimeFn(_) | Stmt::EvolveFn(_) => {
            builder.const_i64(0)
        }

        Stmt::StructDecl(_) | Stmt::EnumDecl(_) | Stmt::TraitDecl(_) => {
            builder.const_i64(0)
        }

        Stmt::ImplBlock(_ib) => {
            // Methods registered in register_decl or third pass
            builder.const_i64(0)
        }

        Stmt::TryCatch(try_body, _err_var, catch_body) => {
            let try_bb = builder.create_block("try.body");
            let catch_bb = builder.create_block("catch.body");
            let merge_bb = builder.create_block("try.merge");

            builder.jump(try_bb);
            builder.switch_to(try_bb);
            for s in try_body { lower_stmt(ctx, builder, s); }
            if !builder.func.block(builder.current_block).map_or(false, |b| b.is_terminated()) {
                builder.jump(merge_bb);
            }

            builder.switch_to(catch_bb);
            for s in catch_body { lower_stmt(ctx, builder, s); }
            if !builder.func.block(builder.current_block).map_or(false, |b| b.is_terminated()) {
                builder.jump(merge_bb);
            }

            builder.switch_to(merge_bb);
            builder.const_i64(0)
        }

        Stmt::Assert(e) => {
            let val = lower_expr(ctx, builder, e);
            builder.ir_assert(val)
        }

        Stmt::Proof(_name, body) => {
            for s in body { lower_stmt(ctx, builder, s); }
            builder.const_i64(0)
        }
        Stmt::ProofBlock(_, _) | Stmt::Theorem(_, _)
        | Stmt::Break | Stmt::Continue => builder.const_i64(0),

        Stmt::Mod(_name, body) => {
            for s in body { lower_stmt(ctx, builder, s); }
            builder.const_i64(0)
        }
        Stmt::Use(_) => builder.const_i64(0),

        Stmt::Spawn(body) | Stmt::Scope(body) => {
            for s in body { lower_stmt(ctx, builder, s); }
            builder.const_i64(0)
        }
        Stmt::SpawnNamed(_, body) => {
            for s in body { lower_stmt(ctx, builder, s); }
            builder.const_i64(0)
        }
        Stmt::Select(arms, _) => {
            for arm in arms {
                for s in &arm.body { lower_stmt(ctx, builder, s); }
            }
            builder.const_i64(0)
        }

        Stmt::DropStmt(name) => {
            if let Some(val) = ctx.lookup_var_id(name) {
                builder.free(val)
            } else {
                builder.const_i64(0)
            }
        }

        Stmt::TypeAlias(_, _, _) => builder.const_i64(0),

        Stmt::AtomicLet(name, typ, init, _vis) => {
            let ty = ctx.resolve_type_opt(typ);
            let val = match init {
                Some(e) => lower_expr(ctx, builder, e),
                None => builder.const_i64(0),
            };
            ctx.define_var(name, val, ty);
            val
        }

        Stmt::Panic(e) => {
            let _val = lower_expr(ctx, builder, e);
            let f = builder.const_bool(false);
            builder.ir_assert(f)
        }

        Stmt::Throw(e) => lower_expr(ctx, builder, e),

        Stmt::MacroDef(_) | Stmt::DeriveDecl(_, _) | Stmt::Generate(_) | Stmt::Optimize(_) => {
            builder.const_i64(0)
        }

        Stmt::EffectDecl(_) => builder.const_i64(0),

        Stmt::ConsciousnessBlock(_name, body) => {
            for s in body { lower_stmt(ctx, builder, s); }
            builder.const_i64(0)
        }

        Stmt::Intent(_, _) | Stmt::Verify(_, _) => builder.const_i64(0),
    }
}

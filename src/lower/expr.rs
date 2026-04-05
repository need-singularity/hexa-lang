//! Expression lowering — AST Expr → HEXA-IR instructions.

use crate::ast::{self, BinOp, UnaryOp, Expr};
use crate::ir::{self, IrBuilder, IrType, ValueId};
use super::LowerCtx;

/// Lower an expression, returning the ValueId of the result.
pub fn lower_expr(ctx: &mut LowerCtx, builder: &mut IrBuilder, expr: &Expr) -> ValueId {
    match expr {
        Expr::IntLit(n) => builder.const_i64(*n),
        Expr::FloatLit(f) => builder.const_f64(*f),
        Expr::BoolLit(b) => builder.const_bool(*b),
        Expr::StringLit(_s) => {
            // String interning deferred — use index as placeholder
            builder.const_i64(0)
        }
        Expr::CharLit(c) => builder.const_i64(*c as i64),

        Expr::Ident(name) => {
            if let Some((ptr, ty)) = ctx.lookup_var(name) {
                if let IrType::Ptr(inner) = ty {
                    builder.load(ptr, *inner)
                } else {
                    ptr
                }
            } else {
                builder.const_i64(0)
            }
        }

        Expr::Binary(lhs, op, rhs) => {
            lower_binary(ctx, builder, lhs, op, rhs)
        }

        Expr::Unary(op, val) => {
            let v = lower_expr(ctx, builder, val);
            match op {
                UnaryOp::Neg => builder.neg(v, IrType::I64),
                UnaryOp::Not => {
                    let zero = builder.const_i64(0);
                    builder.emit_cmp_eq(v, zero)
                }
                UnaryOp::BitNot => {
                    let neg1 = builder.const_i64(-1);
                    builder.emit_xor(v, neg1)
                }
            }
        }

        Expr::Call(callee, args) => {
            let arg_vals: Vec<ValueId> = args.iter()
                .map(|a| lower_expr(ctx, builder, a))
                .collect();

            let call_result = match callee.as_ref() {
                Expr::Ident(name) => {
                    if let Some(&fid) = ctx.func_map.get(name.as_str()) {
                        builder.call(fid, arg_vals, IrType::I64)
                    } else {
                        // Unknown function (builtins like println) — no-op, return arg or 0
                        if !arg_vals.is_empty() { arg_vals[0] } else { builder.const_i64(0) }
                    }
                }
                _ => {
                    let callee_val = lower_expr(ctx, builder, callee);
                    builder.call_indirect(callee_val, arg_vals, IrType::I64)
                }
            };
            call_result
        }

        Expr::If(cond, then_body, else_body) => {
            lower_if_expr(ctx, builder, cond, then_body, else_body)
        }

        Expr::Block(stmts) => {
            let mut last = builder.const_i64(0);
            ctx.push_scope();
            for s in stmts {
                last = super::stmt::lower_stmt_val(ctx, builder, s);
            }
            ctx.pop_scope();
            last
        }

        Expr::Array(items) => {
            let elem_vals: Vec<ValueId> = items.iter()
                .map(|e| lower_expr(ctx, builder, e))
                .collect();
            let len = elem_vals.len();
            let arr_ty = IrType::Array(Box::new(IrType::I64), Some(len));
            let arr = builder.alloc(arr_ty);
            for (i, v) in elem_vals.into_iter().enumerate() {
                let idx = builder.const_i64(i as i64);
                builder.array_store(arr, idx, v);
            }
            arr
        }

        Expr::Index(arr, idx) => {
            let arr_val = lower_expr(ctx, builder, arr);
            let idx_val = lower_expr(ctx, builder, idx);
            builder.array_index(arr_val, idx_val, IrType::I64)
        }

        Expr::StructInit(name, fields) => {
            let field_layout = ctx.struct_map.get(name).cloned().unwrap_or_default();
            let struct_ty = IrType::Struct(ir::types::StructType {
                name: name.clone(),
                fields: field_layout.clone(),
            });
            let obj = builder.alloc(struct_ty);
            for (fname, fexpr) in fields {
                let val = lower_expr(ctx, builder, fexpr);
                if let Some(idx) = field_layout.iter().position(|(n, _)| n == fname) {
                    builder.struct_store_field(obj, idx as u32, val);
                }
            }
            obj
        }

        Expr::Field(obj, field_name) => {
            let obj_val = lower_expr(ctx, builder, obj);
            let field_idx = resolve_field_idx_inner(ctx, field_name);
            builder.struct_field(obj_val, field_idx, IrType::I64)
        }

        Expr::EnumPath(_enum_name, variant, data) => {
            let variants = ctx.enum_map.get(_enum_name).cloned().unwrap_or_default();
            let tag = variants.iter().position(|(n, _)| n == variant).unwrap_or(0);
            let enum_ty = IrType::Enum(ir::types::EnumType {
                name: _enum_name.clone(),
                variants: variants.clone(),
            });
            let obj = builder.alloc(enum_ty);
            let tag_val = builder.const_i64(tag as i64);
            builder.struct_store_field(obj, 0, tag_val);
            if let Some(payload) = data {
                let pval = lower_expr(ctx, builder, payload);
                builder.struct_store_field(obj, 1, pval);
            }
            obj
        }

        Expr::Match(scrutinee, arms) => {
            super::pattern::lower_match(ctx, builder, scrutinee, arms)
        }

        Expr::Range(start, end, _inclusive) => {
            let s = lower_expr(ctx, builder, start);
            let e = lower_expr(ctx, builder, end);
            let range = builder.alloc(IrType::Struct(ir::types::StructType {
                name: "Range".into(),
                fields: vec![("start".into(), IrType::I64), ("end".into(), IrType::I64)],
            }));
            builder.struct_store_field(range, 0, s);
            builder.struct_store_field(range, 1, e);
            range
        }

        Expr::Lambda(_params, _body) => {
            builder.const_i64(0) // closure lowering deferred
        }

        Expr::Tuple(items) => {
            let vals: Vec<ValueId> = items.iter()
                .map(|e| lower_expr(ctx, builder, e))
                .collect();
            let fields: Vec<(String, IrType)> = vals.iter().enumerate()
                .map(|(i, _)| (format!("_{}", i), IrType::I64))
                .collect();
            let tuple_ty = IrType::Struct(ir::types::StructType {
                name: format!("Tuple{}", vals.len()),
                fields,
            });
            let obj = builder.alloc(tuple_ty);
            for (i, v) in vals.into_iter().enumerate() {
                builder.struct_store_field(obj, i as u32, v);
            }
            obj
        }

        Expr::MapLit(pairs) => {
            let len = pairs.len();
            let pair_ty = IrType::Array(Box::new(IrType::I64), Some(len * 2));
            let map = builder.alloc(pair_ty);
            for (i, (k, v)) in pairs.iter().enumerate() {
                let kv = lower_expr(ctx, builder, k);
                let vv = lower_expr(ctx, builder, v);
                let ki = builder.const_i64((i * 2) as i64);
                let vi = builder.const_i64((i * 2 + 1) as i64);
                builder.array_store(map, ki, kv);
                builder.array_store(map, vi, vv);
            }
            map
        }

        Expr::Own(e) | Expr::MoveExpr(e) => {
            let val = lower_expr(ctx, builder, e);
            let ty = builder.func.value_types.get(&val).cloned().unwrap_or(IrType::I64);
            builder.ownership_transfer(val, ty)
        }

        Expr::Borrow(e) => {
            let val = lower_expr(ctx, builder, e);
            let ty = builder.func.value_types.get(&val).cloned().unwrap_or(IrType::I64);
            builder.copy_val(val, IrType::Ptr(Box::new(ty)))
        }

        Expr::Await(e) => lower_expr(ctx, builder, e),
        Expr::Comptime(e) => lower_expr(ctx, builder, e),
        Expr::MacroInvoc(_) => builder.const_i64(0),
        Expr::HandleWith(body, _) => lower_expr(ctx, builder, body),
        Expr::EffectCall(_, _, args) => {
            if let Some(first) = args.first() { lower_expr(ctx, builder, first) }
            else { builder.const_i64(0) }
        }
        Expr::Resume(e) => lower_expr(ctx, builder, e),
        Expr::DynCast(_, e) => lower_expr(ctx, builder, e),
        Expr::Yield(e) => lower_expr(ctx, builder, e),
        Expr::Wildcard => builder.const_i64(0),
    }
}

fn lower_binary(ctx: &mut LowerCtx, builder: &mut IrBuilder, lhs: &Expr, op: &BinOp, rhs: &Expr) -> ValueId {
    // If rhs contains a Call, evaluate rhs first and spill to alloca,
    // then evaluate lhs. This prevents register clobber from bl.
    let (l, r) = if contains_call(rhs) {
        let rv = lower_expr(ctx, builder, rhs);
        let tmp = builder.alloc(IrType::I64);
        builder.store(tmp, rv);
        let lv = lower_expr(ctx, builder, lhs);
        let rv_reloaded = builder.load(tmp, IrType::I64);
        (lv, rv_reloaded)
    } else {
        let lv = lower_expr(ctx, builder, lhs);
        let rv = lower_expr(ctx, builder, rhs);
        (lv, rv)
    };

    match op {
        BinOp::Add => builder.add(l, r, IrType::I64),
        BinOp::Sub => builder.sub(l, r, IrType::I64),
        BinOp::Mul => builder.mul(l, r, IrType::I64),
        BinOp::Div => builder.div(l, r, IrType::I64),
        BinOp::Rem => builder.modulo(l, r, IrType::I64),
        BinOp::Pow => builder.mul(l, r, IrType::I64),
        BinOp::Eq => builder.emit_cmp_eq(l, r),
        BinOp::Ne => builder.emit_cmp_ne(l, r),
        BinOp::Lt => builder.emit_cmp_lt(l, r),
        BinOp::Gt => builder.emit_cmp_gt(l, r),
        BinOp::Le => builder.emit_cmp_le(l, r),
        BinOp::Ge => builder.emit_cmp_ge(l, r),
        BinOp::And => builder.emit_and(l, r),
        BinOp::Or => builder.emit_or(l, r),
        BinOp::Xor | BinOp::BitXor => builder.emit_xor(l, r),
        BinOp::BitAnd => builder.emit_bitand(l, r),
        BinOp::BitOr => builder.emit_bitor(l, r),
        BinOp::Assign | BinOp::DeclAssign | BinOp::Range | BinOp::Arrow => l,
    }
}

fn lower_if_expr(
    ctx: &mut LowerCtx,
    builder: &mut IrBuilder,
    cond: &Expr,
    then_body: &ast::Block,
    else_body: &Option<ast::Block>,
) -> ValueId {
    let cond_val = lower_expr(ctx, builder, cond);
    let then_bb = builder.create_block("if.then");
    let else_bb = builder.create_block("if.else");
    let merge_bb = builder.create_block("if.merge");

    builder.branch(cond_val, then_bb, else_bb);

    builder.switch_to(then_bb);
    ctx.push_scope();
    let mut then_val = builder.const_i64(0);
    for s in then_body {
        then_val = super::stmt::lower_stmt_val(ctx, builder, s);
    }
    ctx.pop_scope();
    let then_exit = builder.current_block;
    if !builder.func.block(then_exit).map_or(false, |b| b.is_terminated()) {
        builder.jump(merge_bb);
    }

    builder.switch_to(else_bb);
    ctx.push_scope();
    let mut else_val = builder.const_i64(0);
    if let Some(else_stmts) = else_body {
        for s in else_stmts {
            else_val = super::stmt::lower_stmt_val(ctx, builder, s);
        }
    }
    ctx.pop_scope();
    let else_exit = builder.current_block;
    if !builder.func.block(else_exit).map_or(false, |b| b.is_terminated()) {
        builder.jump(merge_bb);
    }

    builder.switch_to(merge_bb);
    builder.phi(vec![(then_exit, then_val), (else_exit, else_val)], IrType::I64)
}

/// Check if an expression contains a function call (which clobbers registers).
fn contains_call(expr: &Expr) -> bool {
    match expr {
        Expr::Call(_, _) => true,
        Expr::Binary(l, _, r) => contains_call(l) || contains_call(r),
        Expr::Unary(_, e) => contains_call(e),
        Expr::If(c, _, _) => contains_call(c),
        _ => false,
    }
}

fn resolve_field_idx_inner(ctx: &LowerCtx, field_name: &str) -> u32 {
    for (_name, fields) in &ctx.struct_map {
        if let Some(idx) = fields.iter().position(|(n, _)| n == field_name) {
            return idx as u32;
        }
    }
    0
}

/// Public version for use from stmt.rs.
pub fn resolve_field_idx_pub(ctx: &LowerCtx, _obj: &Expr, field_name: &str) -> u32 {
    resolve_field_idx_inner(ctx, field_name)
}

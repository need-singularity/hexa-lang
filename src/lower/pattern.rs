//! Pattern matching lowering — Match expressions → IR branch trees.

use crate::ast::{self, Expr, MatchArm};
use crate::ir::{IrBuilder, IrType, ValueId, BlockId};
use super::LowerCtx;
use super::expr::lower_expr;

/// Lower a match expression into a chain of conditional branches.
pub fn lower_match(
    ctx: &mut LowerCtx,
    builder: &mut IrBuilder,
    scrutinee: &Expr,
    arms: &[MatchArm],
) -> ValueId {
    let scrut_val = lower_expr(ctx, builder, scrutinee);
    let merge_bb = builder.create_block("match.merge");

    let mut arm_results: Vec<(BlockId, ValueId)> = Vec::new();
    let num_arms = arms.len();

    for (i, arm) in arms.iter().enumerate() {
        let arm_bb = builder.create_block(&format!("match.arm.{}", i));
        let next_bb = if i + 1 < num_arms {
            builder.create_block(&format!("match.test.{}", i + 1))
        } else {
            merge_bb
        };

        // Test pattern
        let matches = lower_pattern_test(ctx, builder, scrut_val, &arm.pattern);

        // Check guard if present
        let cond = if let Some(guard) = &arm.guard {
            let guard_val = lower_expr(ctx, builder, guard);
            builder.emit_and(matches, guard_val)
        } else {
            matches
        };

        builder.branch(cond, arm_bb, next_bb);

        // Arm body
        builder.switch_to(arm_bb);
        ctx.push_scope();
        bind_pattern(ctx, builder, scrut_val, &arm.pattern);
        let mut arm_val = builder.const_i64(0);
        for s in &arm.body {
            arm_val = super::stmt::lower_stmt_val(ctx, builder, s);
        }
        ctx.pop_scope();
        let exit_bb = builder.current_block;
        if !builder.func.block(exit_bb).map_or(false, |b| b.is_terminated()) {
            builder.jump(merge_bb);
        }
        arm_results.push((exit_bb, arm_val));

        // Continue to next test
        if i + 1 < num_arms {
            builder.switch_to(next_bb);
        }
    }

    // Merge
    builder.switch_to(merge_bb);
    if arm_results.is_empty() {
        builder.const_i64(0)
    } else if arm_results.len() == 1 {
        arm_results[0].1
    } else {
        builder.phi(arm_results, IrType::I64)
    }
}

/// Emit IR to test if scrutinee matches a pattern. Returns a bool ValueId.
fn lower_pattern_test(
    ctx: &mut LowerCtx,
    builder: &mut IrBuilder,
    scrut: ValueId,
    pattern: &Expr,
) -> ValueId {
    match pattern {
        // Wildcard always matches
        Expr::Wildcard => builder.const_bool(true),

        // Identifier binding always matches (binds in bind_pattern)
        Expr::Ident(_) => builder.const_bool(true),

        // Literal comparison
        Expr::IntLit(n) => {
            let lit = builder.const_i64(*n);
            builder.emit_cmp_eq(scrut, lit)
        }
        Expr::BoolLit(b) => {
            let lit = builder.const_bool(*b);
            builder.emit_cmp_eq(scrut, lit)
        }
        Expr::StringLit(_s) => {
            // String comparison deferred — treat as match for now
            builder.const_bool(true)
        }

        // Enum variant: check tag
        Expr::EnumPath(_, variant, _) => {
            // Extract tag from scrutinee (field 0)
            let tag = builder.struct_field(scrut, 0, IrType::I64);
            // Find expected tag value
            let expected = builder.const_i64(0); // simplified
            builder.emit_cmp_eq(tag, expected)
        }

        // Struct pattern: check all fields
        Expr::StructInit(_, fields) => {
            let mut result = builder.const_bool(true);
            for (i, (_, field_pat)) in fields.iter().enumerate() {
                let field_val = builder.struct_field(scrut, i as u32, IrType::I64);
                let field_match = lower_pattern_test(ctx, builder, field_val, field_pat);
                result = builder.emit_and(result, field_match);
            }
            result
        }

        // Tuple pattern
        Expr::Tuple(items) => {
            let mut result = builder.const_bool(true);
            for (i, item) in items.iter().enumerate() {
                let elem = builder.struct_field(scrut, i as u32, IrType::I64);
                let elem_match = lower_pattern_test(ctx, builder, elem, item);
                result = builder.emit_and(result, elem_match);
            }
            result
        }

        // Range pattern
        Expr::Range(start, end, inclusive) => {
            let s = lower_expr(ctx, builder, start);
            let e = lower_expr(ctx, builder, end);
            let ge = builder.emit_cmp_ge(scrut, s);
            let le = if *inclusive {
                builder.emit_cmp_le(scrut, e)
            } else {
                builder.emit_cmp_lt(scrut, e)
            };
            builder.emit_and(ge, le)
        }

        _ => builder.const_bool(true),
    }
}

/// Bind pattern variables into the current scope.
fn bind_pattern(
    ctx: &mut LowerCtx,
    builder: &mut IrBuilder,
    scrut: ValueId,
    pattern: &Expr,
) {
    match pattern {
        Expr::Ident(name) if name != "_" => {
            ctx.define_var(name, scrut, IrType::I64);
        }
        Expr::EnumPath(_, _, Some(inner)) => {
            let payload = builder.struct_field(scrut, 1, IrType::I64);
            bind_pattern(ctx, builder, payload, inner);
        }
        Expr::Tuple(items) => {
            for (i, item) in items.iter().enumerate() {
                let elem = builder.struct_field(scrut, i as u32, IrType::I64);
                bind_pattern(ctx, builder, elem, item);
            }
        }
        Expr::StructInit(_, fields) => {
            for (i, (_, field_pat)) in fields.iter().enumerate() {
                let field_val = builder.struct_field(scrut, i as u32, IrType::I64);
                bind_pattern(ctx, builder, field_val, field_pat);
            }
        }
        _ => {}
    }
}

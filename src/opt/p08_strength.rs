//! P8: Strength Reduction — replace expensive ops with cheaper equivalents.

use crate::ir::{IrModule, OpCode, Operand};
use super::{Pass, PassResult};

pub struct StrengthReductionPass;

impl Pass for StrengthReductionPass {
    fn name(&self) -> &'static str { "strength_reduction" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut reduced = 0usize;

        for func in &mut module.functions {
            for block in &mut func.blocks {
                for instr in &mut block.instructions {
                    match (&instr.op, instr.operands.as_slice()) {
                        // ── Multiply reductions ──

                        // x * 0 → 0
                        (OpCode::Mul, [Operand::Value(_), Operand::ImmI64(0)])
                        | (OpCode::Mul, [Operand::ImmI64(0), Operand::Value(_)]) => {
                            instr.op = OpCode::Load;
                            instr.operands = vec![Operand::ImmI64(0)];
                            changed = true;
                            reduced += 1;
                        }
                        // x * 1 → x
                        (OpCode::Mul, [val @ Operand::Value(_), Operand::ImmI64(1)])
                        | (OpCode::Mul, [Operand::ImmI64(1), val @ Operand::Value(_)]) => {
                            instr.op = OpCode::Copy;
                            let v = val.clone();
                            instr.operands = vec![v];
                            changed = true;
                            reduced += 1;
                        }
                        // x * 2 → x + x (shift left by 1)
                        (OpCode::Mul, [val @ Operand::Value(_), Operand::ImmI64(2)])
                        | (OpCode::Mul, [Operand::ImmI64(2), val @ Operand::Value(_)]) => {
                            instr.op = OpCode::Add;
                            let v = val.clone();
                            instr.operands = vec![v.clone(), v];
                            changed = true;
                            reduced += 1;
                        }
                        // x * power_of_2 (>2) → codegen hint for shift
                        (OpCode::Mul, [Operand::Value(_), Operand::ImmI64(n)])
                            if (*n as u64).is_power_of_two() && *n > 2 =>
                        {
                            // Keep as mul; codegen will emit shift.
                            // Marked as reduced for statistics but op unchanged.
                        }

                        // ── Division reductions ──

                        // x / 1 → x
                        (OpCode::Div, [val @ Operand::Value(_), Operand::ImmI64(1)]) => {
                            instr.op = OpCode::Copy;
                            let v = val.clone();
                            instr.operands = vec![v];
                            changed = true;
                            reduced += 1;
                        }
                        // x / x → 1 (when both operands are the same SSA value)
                        (OpCode::Div, [Operand::Value(a), Operand::Value(b)]) if a == b => {
                            instr.op = OpCode::Load;
                            instr.operands = vec![Operand::ImmI64(1)];
                            changed = true;
                            reduced += 1;
                        }
                        // x / power_of_2 → codegen hint for shift right
                        (OpCode::Div, [Operand::Value(_), Operand::ImmI64(n)])
                            if *n > 1 && (*n as u64).is_power_of_two() =>
                        {
                            // Keep as div; codegen will emit arithmetic shift right.
                        }

                        // ── Modulo reductions ──

                        // x % 1 → 0
                        (OpCode::Mod, [_, Operand::ImmI64(1)]) => {
                            instr.op = OpCode::Load;
                            instr.operands = vec![Operand::ImmI64(0)];
                            changed = true;
                            reduced += 1;
                        }
                        // x % x → 0 (when both operands are the same SSA value)
                        (OpCode::Mod, [Operand::Value(a), Operand::Value(b)]) if a == b => {
                            instr.op = OpCode::Load;
                            instr.operands = vec![Operand::ImmI64(0)];
                            changed = true;
                            reduced += 1;
                        }

                        // ── Addition reductions ──

                        // x + 0 → x (belt-and-suspenders with const_fold)
                        (OpCode::Add, [val @ Operand::Value(_), Operand::ImmI64(0)])
                        | (OpCode::Add, [Operand::ImmI64(0), val @ Operand::Value(_)]) => {
                            instr.op = OpCode::Copy;
                            let v = val.clone();
                            instr.operands = vec![v];
                            changed = true;
                            reduced += 1;
                        }

                        // ── Subtraction reductions ──

                        // x - 0 → x
                        (OpCode::Sub, [val @ Operand::Value(_), Operand::ImmI64(0)]) => {
                            instr.op = OpCode::Copy;
                            let v = val.clone();
                            instr.operands = vec![v];
                            changed = true;
                            reduced += 1;
                        }
                        // x - x → 0 (same SSA value)
                        (OpCode::Sub, [Operand::Value(a), Operand::Value(b)]) if a == b => {
                            instr.op = OpCode::Load;
                            instr.operands = vec![Operand::ImmI64(0)];
                            changed = true;
                            reduced += 1;
                        }

                        // ── Negation reductions ──
                        // neg(neg(x)) requires two-instruction pattern;
                        // handled via a second sweep below.

                        _ => {}
                    }
                }
            }

            // Second sweep: neg(neg(x)) → x
            // Find Neg instructions whose operand is another Neg's result.
            // Collect replacements first, then apply.
            let mut neg_replacements: Vec<(crate::ir::ValueId, Operand)> = Vec::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    if instr.op == OpCode::Neg {
                        if let Some(Operand::Value(inner_id)) = instr.operands.first() {
                            // Find the instruction that defines inner_id
                            for b2 in &func.blocks {
                                for i2 in &b2.instructions {
                                    if i2.result == *inner_id && i2.op == OpCode::Neg {
                                        // neg(neg(x)) → x
                                        if let Some(orig) = i2.operands.first() {
                                            neg_replacements.push((instr.result, orig.clone()));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            for (result_id, orig_operand) in neg_replacements {
                for block in &mut func.blocks {
                    for instr in &mut block.instructions {
                        if instr.result == result_id {
                            instr.op = OpCode::Copy;
                            instr.operands = vec![orig_operand.clone()];
                            changed = true;
                            reduced += 1;
                        }
                    }
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("reduced".into(), reduced)],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType, Operand, OpCode, ValueId};
    use crate::ir::instr::Instruction;

    /// Helper: create a module with one function, one block, given instructions.
    fn make_module(instructions: Vec<Instruction>) -> IrModule {
        let mut module = IrModule::new("test");
        let fid = module.add_function("f".into(), vec![], IrType::I64);
        let func = module.function_mut(fid).unwrap();
        let _bb = func.fresh_block("entry");
        func.blocks[0].instructions = instructions;
        module
    }

    fn instr(result: u32, op: OpCode, operands: Vec<Operand>) -> Instruction {
        Instruction {
            result: ValueId(result),
            op,
            operands,
            ty: IrType::I64,
        }
    }

    #[test]
    fn test_mul_2_to_add() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(42)]),
            instr(1, OpCode::Mul, vec![Operand::Value(ValueId(0)), Operand::ImmI64(2)]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let f = &module.functions[0];
        let i = &f.blocks[0].instructions[1];
        assert_eq!(i.op, OpCode::Add);
        assert!(matches!(&i.operands[0], Operand::Value(ValueId(0))));
        assert!(matches!(&i.operands[1], Operand::Value(ValueId(0))));
    }

    #[test]
    fn test_div_1_to_copy() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(10)]),
            instr(1, OpCode::Div, vec![Operand::Value(ValueId(0)), Operand::ImmI64(1)]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let i = &module.functions[0].blocks[0].instructions[1];
        assert_eq!(i.op, OpCode::Copy);
        assert!(matches!(&i.operands[0], Operand::Value(ValueId(0))));
    }

    #[test]
    fn test_mod_1_to_zero() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(10)]),
            instr(1, OpCode::Mod, vec![Operand::Value(ValueId(0)), Operand::ImmI64(1)]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let i = &module.functions[0].blocks[0].instructions[1];
        assert_eq!(i.op, OpCode::Load);
        assert!(matches!(&i.operands[0], Operand::ImmI64(0)));
    }

    #[test]
    fn test_div_x_by_x_to_one() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(7)]),
            instr(1, OpCode::Div, vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(0))]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let i = &module.functions[0].blocks[0].instructions[1];
        assert_eq!(i.op, OpCode::Load);
        assert!(matches!(&i.operands[0], Operand::ImmI64(1)));
    }

    #[test]
    fn test_mod_x_by_x_to_zero() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(7)]),
            instr(1, OpCode::Mod, vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(0))]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let i = &module.functions[0].blocks[0].instructions[1];
        assert_eq!(i.op, OpCode::Load);
        assert!(matches!(&i.operands[0], Operand::ImmI64(0)));
    }

    #[test]
    fn test_mul_0_to_zero() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(42)]),
            instr(1, OpCode::Mul, vec![Operand::Value(ValueId(0)), Operand::ImmI64(0)]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let i = &module.functions[0].blocks[0].instructions[1];
        assert_eq!(i.op, OpCode::Load);
        assert!(matches!(&i.operands[0], Operand::ImmI64(0)));
    }

    #[test]
    fn test_add_0_to_copy() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(42)]),
            instr(1, OpCode::Add, vec![Operand::Value(ValueId(0)), Operand::ImmI64(0)]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let i = &module.functions[0].blocks[0].instructions[1];
        assert_eq!(i.op, OpCode::Copy);
    }

    #[test]
    fn test_neg_neg_to_copy() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(5)]),
            instr(1, OpCode::Neg, vec![Operand::Value(ValueId(0))]),
            instr(2, OpCode::Neg, vec![Operand::Value(ValueId(1))]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let i = &module.functions[0].blocks[0].instructions[2];
        assert_eq!(i.op, OpCode::Copy);
        assert!(matches!(&i.operands[0], Operand::Value(ValueId(0))));
    }

    #[test]
    fn test_stats_count() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(10)]),
            instr(1, OpCode::Mul, vec![Operand::Value(ValueId(0)), Operand::ImmI64(2)]),
            instr(2, OpCode::Div, vec![Operand::Value(ValueId(1)), Operand::ImmI64(1)]),
        ]);
        let pass = StrengthReductionPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let reduced = result.stats.iter().find(|(k, _)| k == "reduced").unwrap().1;
        assert_eq!(reduced, 2);
    }
}

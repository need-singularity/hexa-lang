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
                        // x * 2 → x + x (shift left by 1)
                        (OpCode::Mul, [val @ Operand::Value(_), Operand::ImmI64(2)])
                        | (OpCode::Mul, [Operand::ImmI64(2), val @ Operand::Value(_)]) => {
                            instr.op = OpCode::Add;
                            let v = val.clone();
                            instr.operands = vec![v.clone(), v];
                            changed = true;
                            reduced += 1;
                        }
                        // x * power_of_2 → conceptual shift (represented as repeated add)
                        (OpCode::Mul, [Operand::Value(_), Operand::ImmI64(n)]) if (*n as u64).is_power_of_two() && *n > 2 => {
                            // Keep as mul for now; codegen will emit shift
                            // Mark for codegen hint
                        }
                        // x / 1 → x
                        (OpCode::Div, [val @ Operand::Value(_), Operand::ImmI64(1)]) => {
                            instr.op = OpCode::Copy;
                            let v = val.clone();
                            instr.operands = vec![v];
                            changed = true;
                            reduced += 1;
                        }
                        // x % 1 → 0
                        (OpCode::Mod, [_, Operand::ImmI64(1)]) => {
                            instr.op = OpCode::Load;
                            instr.operands = vec![Operand::ImmI64(0)];
                            changed = true;
                            reduced += 1;
                        }
                        // x - x → 0 (same value)
                        (OpCode::Sub, [Operand::Value(a), Operand::Value(b)]) if a == b => {
                            instr.op = OpCode::Load;
                            instr.operands = vec![Operand::ImmI64(0)];
                            changed = true;
                            reduced += 1;
                        }
                        _ => {}
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

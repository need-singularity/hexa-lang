//! P4: Constant Folding — evaluate constant expressions at compile time.

use crate::ir::{IrModule, OpCode, Operand, IrType};
use super::{Pass, PassResult};

pub struct ConstFoldPass;

impl Pass for ConstFoldPass {
    fn name(&self) -> &'static str { "const_fold" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut folded = 0usize;

        for func in &mut module.functions {
            for block in &mut func.blocks {
                for instr in &mut block.instructions {
                    if let Some(result) = try_fold(instr.op, &instr.operands) {
                        instr.op = OpCode::Load;
                        instr.operands = vec![result];
                        changed = true;
                        folded += 1;
                    }
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("folded".into(), folded)],
        }
    }
}

/// Try to fold a constant expression.
fn try_fold(op: OpCode, operands: &[Operand]) -> Option<Operand> {
    match (op, operands) {
        // Integer arithmetic
        (OpCode::Add, [Operand::ImmI64(a), Operand::ImmI64(b)]) => Some(Operand::ImmI64(a + b)),
        (OpCode::Sub, [Operand::ImmI64(a), Operand::ImmI64(b)]) => Some(Operand::ImmI64(a - b)),
        (OpCode::Mul, [Operand::ImmI64(a), Operand::ImmI64(b)]) => Some(Operand::ImmI64(a * b)),
        (OpCode::Div, [Operand::ImmI64(a), Operand::ImmI64(b)]) if *b != 0 => Some(Operand::ImmI64(a / b)),
        (OpCode::Mod, [Operand::ImmI64(a), Operand::ImmI64(b)]) if *b != 0 => Some(Operand::ImmI64(a % b)),
        (OpCode::Neg, [Operand::ImmI64(a)]) => Some(Operand::ImmI64(-a)),

        // Float arithmetic
        (OpCode::Add, [Operand::ImmF64(a), Operand::ImmF64(b)]) => Some(Operand::ImmF64(a + b)),
        (OpCode::Sub, [Operand::ImmF64(a), Operand::ImmF64(b)]) => Some(Operand::ImmF64(a - b)),
        (OpCode::Mul, [Operand::ImmF64(a), Operand::ImmF64(b)]) => Some(Operand::ImmF64(a * b)),
        (OpCode::Div, [Operand::ImmF64(a), Operand::ImmF64(b)]) if *b != 0.0 => Some(Operand::ImmF64(a / b)),

        // Identity operations: x + 0 = x, x * 1 = x, x - 0 = x
        (OpCode::Add, [val @ Operand::Value(_), Operand::ImmI64(0)])
        | (OpCode::Add, [Operand::ImmI64(0), val @ Operand::Value(_)])
        | (OpCode::Sub, [val @ Operand::Value(_), Operand::ImmI64(0)]) => Some(val.clone()),

        (OpCode::Mul, [val @ Operand::Value(_), Operand::ImmI64(1)])
        | (OpCode::Mul, [Operand::ImmI64(1), val @ Operand::Value(_)]) => Some(val.clone()),

        // x * 0 = 0
        (OpCode::Mul, [_, Operand::ImmI64(0)])
        | (OpCode::Mul, [Operand::ImmI64(0), _]) => Some(Operand::ImmI64(0)),

        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fold_add() {
        let result = try_fold(OpCode::Add, &[Operand::ImmI64(3), Operand::ImmI64(4)]);
        assert!(matches!(result, Some(Operand::ImmI64(7))));
    }

    #[test]
    fn test_fold_mul_zero() {
        let result = try_fold(OpCode::Mul, &[Operand::ImmI64(42), Operand::ImmI64(0)]);
        assert!(matches!(result, Some(Operand::ImmI64(0))));
    }

    #[test]
    fn test_no_fold_div_zero() {
        let result = try_fold(OpCode::Div, &[Operand::ImmI64(10), Operand::ImmI64(0)]);
        assert!(result.is_none());
    }
}

//! P4: Constant Folding — evaluate constant expressions at compile time.
//!
//! Enhanced with value tracking (propagates constants through SSA values),
//! float/boolean folding, and fixpoint iteration for cascading folds.

use std::collections::HashMap;
use crate::ir::{IrModule, OpCode, Operand, ValueId};
use super::{Pass, PassResult};

pub struct ConstFoldPass;

impl Pass for ConstFoldPass {
    fn name(&self) -> &'static str { "const_fold" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut total_folded = 0usize;
        let mut changed_any = false;

        for func in &mut module.functions {
            // Fixpoint: keep folding until no more changes
            loop {
                let mut changed = false;
                let mut folded = 0usize;

                // Phase 1: Build value->constant map from Load instructions
                let mut known: HashMap<ValueId, Operand> = HashMap::new();
                for block in &func.blocks {
                    for instr in &block.instructions {
                        if instr.op == OpCode::Load && instr.operands.len() == 1 {
                            match &instr.operands[0] {
                                op @ Operand::ImmI64(_)
                                | op @ Operand::ImmF64(_)
                                | op @ Operand::ImmBool(_) => {
                                    known.insert(instr.result, op.clone());
                                }
                                _ => {}
                            }
                        }
                    }
                }

                // Phase 2: Try to fold each instruction
                for block in &mut func.blocks {
                    for instr in &mut block.instructions {
                        // Skip Load (already tracked) and non-foldable ops
                        if instr.op == OpCode::Load
                            || instr.op.has_side_effect()
                            || instr.op.is_terminator()
                            || instr.op.is_proof()
                        {
                            continue;
                        }

                        // Resolve operands: replace Value refs with known constants
                        let resolved: Vec<Operand> = instr
                            .operands
                            .iter()
                            .map(|op| match op {
                                Operand::Value(v) => {
                                    known.get(v).cloned().unwrap_or_else(|| op.clone())
                                }
                                other => other.clone(),
                            })
                            .collect();

                        if let Some(result) = try_fold(instr.op, &resolved) {
                            // Record the newly known constant
                            known.insert(instr.result, result.clone());
                            instr.op = OpCode::Load;
                            instr.operands = vec![result];
                            changed = true;
                            folded += 1;
                        }
                    }
                }

                total_folded += folded;
                if changed {
                    changed_any = true;
                } else {
                    break; // fixpoint reached
                }
            }
        }

        PassResult {
            changed: changed_any,
            stats: vec![("folded".into(), total_folded)],
        }
    }
}

/// Try to fold a constant expression from resolved operands.
fn try_fold(op: OpCode, operands: &[Operand]) -> Option<Operand> {
    match (op, operands) {
        // -- Integer arithmetic --
        (OpCode::Add, [Operand::ImmI64(a), Operand::ImmI64(b)]) => Some(Operand::ImmI64(a.wrapping_add(*b))),
        (OpCode::Sub, [Operand::ImmI64(a), Operand::ImmI64(b)]) => Some(Operand::ImmI64(a.wrapping_sub(*b))),
        (OpCode::Mul, [Operand::ImmI64(a), Operand::ImmI64(b)]) => Some(Operand::ImmI64(a.wrapping_mul(*b))),
        (OpCode::Div, [Operand::ImmI64(a), Operand::ImmI64(b)]) if *b != 0 => Some(Operand::ImmI64(a / b)),
        (OpCode::Mod, [Operand::ImmI64(a), Operand::ImmI64(b)]) if *b != 0 => Some(Operand::ImmI64(a % b)),
        (OpCode::Neg, [Operand::ImmI64(a)]) => Some(Operand::ImmI64(a.wrapping_neg())),

        // -- Float arithmetic --
        (OpCode::Add, [Operand::ImmF64(a), Operand::ImmF64(b)]) => Some(Operand::ImmF64(a + b)),
        (OpCode::Sub, [Operand::ImmF64(a), Operand::ImmF64(b)]) => Some(Operand::ImmF64(a - b)),
        (OpCode::Mul, [Operand::ImmF64(a), Operand::ImmF64(b)]) => Some(Operand::ImmF64(a * b)),
        (OpCode::Div, [Operand::ImmF64(a), Operand::ImmF64(b)]) if *b != 0.0 => Some(Operand::ImmF64(a / b)),
        (OpCode::Neg, [Operand::ImmF64(a)]) => Some(Operand::ImmF64(-a)),

        // -- Identity operations: x + 0 = x, x * 1 = x, x - 0 = x --
        (OpCode::Add, [val @ Operand::Value(_), Operand::ImmI64(0)])
        | (OpCode::Add, [Operand::ImmI64(0), val @ Operand::Value(_)])
        | (OpCode::Sub, [val @ Operand::Value(_), Operand::ImmI64(0)]) => Some(val.clone()),

        (OpCode::Mul, [val @ Operand::Value(_), Operand::ImmI64(1)])
        | (OpCode::Mul, [Operand::ImmI64(1), val @ Operand::Value(_)]) => Some(val.clone()),

        // Float identities
        (OpCode::Add, [val @ Operand::Value(_), Operand::ImmF64(f)])
        | (OpCode::Add, [Operand::ImmF64(f), val @ Operand::Value(_)]) if *f == 0.0 => Some(val.clone()),
        (OpCode::Sub, [val @ Operand::Value(_), Operand::ImmF64(f)]) if *f == 0.0 => Some(val.clone()),
        (OpCode::Mul, [val @ Operand::Value(_), Operand::ImmF64(f)])
        | (OpCode::Mul, [Operand::ImmF64(f), val @ Operand::Value(_)]) if *f == 1.0 => Some(val.clone()),

        // -- x * 0 = 0 --
        (OpCode::Mul, [_, Operand::ImmI64(0)])
        | (OpCode::Mul, [Operand::ImmI64(0), _]) => Some(Operand::ImmI64(0)),

        (OpCode::Mul, [_, Operand::ImmF64(f)])
        | (OpCode::Mul, [Operand::ImmF64(f), _]) if *f == 0.0 => Some(Operand::ImmF64(0.0)),

        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrFunction, FuncId, BasicBlock, BlockId, Instruction};

    // -- Unit tests for try_fold --

    #[test]
    fn test_fold_add_i64() {
        assert!(matches!(
            try_fold(OpCode::Add, &[Operand::ImmI64(3), Operand::ImmI64(4)]),
            Some(Operand::ImmI64(7))
        ));
    }

    #[test]
    fn test_fold_sub_i64() {
        assert!(matches!(
            try_fold(OpCode::Sub, &[Operand::ImmI64(10), Operand::ImmI64(3)]),
            Some(Operand::ImmI64(7))
        ));
    }

    #[test]
    fn test_fold_mul_zero() {
        assert!(matches!(
            try_fold(OpCode::Mul, &[Operand::ImmI64(42), Operand::ImmI64(0)]),
            Some(Operand::ImmI64(0))
        ));
    }

    #[test]
    fn test_no_fold_div_zero() {
        assert!(try_fold(OpCode::Div, &[Operand::ImmI64(10), Operand::ImmI64(0)]).is_none());
    }

    #[test]
    fn test_fold_add_f64() {
        let result = try_fold(OpCode::Add, &[Operand::ImmF64(1.5), Operand::ImmF64(2.5)]);
        match result {
            Some(Operand::ImmF64(v)) => assert!((v - 4.0).abs() < 1e-10),
            _ => panic!("expected ImmF64(4.0)"),
        }
    }

    #[test]
    fn test_fold_neg_f64() {
        let result = try_fold(OpCode::Neg, &[Operand::ImmF64(3.14)]);
        match result {
            Some(Operand::ImmF64(v)) => assert!((v + 3.14).abs() < 1e-10),
            _ => panic!("expected ImmF64(-3.14)"),
        }
    }

    #[test]
    fn test_fold_mul_f64_zero() {
        let result = try_fold(OpCode::Mul, &[Operand::ImmF64(42.0), Operand::ImmF64(0.0)]);
        match result {
            Some(Operand::ImmF64(v)) => assert!((v - 0.0).abs() < 1e-10),
            _ => panic!("expected ImmF64(0.0)"),
        }
    }

    // -- Integration tests: full pass with value tracking --

    /// Helper: build a minimal module with one function and one block.
    fn build_test_module(instructions: Vec<Instruction>) -> IrModule {
        let mut module = IrModule::new("test");
        let fid = module.add_function("main".into(), vec![], IrType::I64);
        let func = module.function_mut(fid).unwrap();
        let _bb = func.fresh_block("entry");
        func.blocks[0].instructions = instructions;
        module
    }

    #[test]
    fn test_fold_3_plus_4_through_pass() {
        // %0 = load 3
        // %1 = load 4
        // %2 = add %0, %1   -> should become: %2 = load 7
        let instrs = vec![
            Instruction {
                result: ValueId(0),
                op: OpCode::Load,
                operands: vec![Operand::ImmI64(3)],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(1),
                op: OpCode::Load,
                operands: vec![Operand::ImmI64(4)],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = ConstFoldPass.run(&mut module);

        assert!(result.changed);
        let instr = &module.functions[0].blocks[0].instructions[2];
        assert_eq!(instr.op, OpCode::Load);
        assert!(matches!(instr.operands[0], Operand::ImmI64(7)));
    }

    #[test]
    fn test_cascading_fold() {
        // %0 = load 1
        // %1 = load 2
        // %2 = add %0, %1    -> load 3  (first iteration)
        // %3 = load 10
        // %4 = mul %2, %3    -> load 30 (second iteration, after %2 is known)
        let instrs = vec![
            Instruction {
                result: ValueId(0),
                op: OpCode::Load,
                operands: vec![Operand::ImmI64(1)],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(1),
                op: OpCode::Load,
                operands: vec![Operand::ImmI64(2)],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(3),
                op: OpCode::Load,
                operands: vec![Operand::ImmI64(10)],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(4),
                op: OpCode::Mul,
                operands: vec![Operand::Value(ValueId(2)), Operand::Value(ValueId(3))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = ConstFoldPass.run(&mut module);

        assert!(result.changed);
        // %4 should now be load 30
        let instr = &module.functions[0].blocks[0].instructions[4];
        assert_eq!(instr.op, OpCode::Load);
        assert!(matches!(instr.operands[0], Operand::ImmI64(30)));
    }

    #[test]
    fn test_cascading_three_levels() {
        // a=1, b=2, c=a+b (=3), d=c+c (=6)
        let instrs = vec![
            Instruction {
                result: ValueId(0),
                op: OpCode::Load,
                operands: vec![Operand::ImmI64(1)],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(1),
                op: OpCode::Load,
                operands: vec![Operand::ImmI64(2)],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(3),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(2)), Operand::Value(ValueId(2))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = ConstFoldPass.run(&mut module);

        assert!(result.changed);
        let instr = &module.functions[0].blocks[0].instructions[3];
        assert_eq!(instr.op, OpCode::Load);
        assert!(matches!(instr.operands[0], Operand::ImmI64(6)));
    }

    #[test]
    fn test_float_fold_through_pass() {
        // %0 = load 1.5
        // %1 = load 2.5
        // %2 = add %0, %1 -> load 4.0
        let instrs = vec![
            Instruction {
                result: ValueId(0),
                op: OpCode::Load,
                operands: vec![Operand::ImmF64(1.5)],
                ty: IrType::F64,
            },
            Instruction {
                result: ValueId(1),
                op: OpCode::Load,
                operands: vec![Operand::ImmF64(2.5)],
                ty: IrType::F64,
            },
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::F64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = ConstFoldPass.run(&mut module);

        assert!(result.changed);
        let instr = &module.functions[0].blocks[0].instructions[2];
        assert_eq!(instr.op, OpCode::Load);
        match &instr.operands[0] {
            Operand::ImmF64(v) => assert!((v - 4.0).abs() < 1e-10),
            other => panic!("expected ImmF64, got {:?}", other),
        }
    }

    #[test]
    fn test_no_fold_unknown_value() {
        // %0 is a function parameter (not loaded from immediate)
        // %1 = load 5
        // %2 = add %0, %1 -> cannot fold (unknown %0, and 5 != 0 so no identity)
        let instrs = vec![
            Instruction {
                result: ValueId(1),
                op: OpCode::Load,
                operands: vec![Operand::ImmI64(5)],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = ConstFoldPass.run(&mut module);

        assert!(!result.changed);
    }
}

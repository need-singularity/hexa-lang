//! P7: Common Subexpression Elimination — deduplicate identical computations.
//!
//! Enhanced with:
//! - Cross-block CSE (global, across all blocks in a function)
//! - Operand canonicalization for commutative ops (Add, Mul)
//! - Dead instruction removal after replacement

use std::collections::HashMap;
use crate::ir::{IrModule, OpCode, Operand, ValueId};
use super::{Pass, PassResult};

pub struct CsePass;

impl Pass for CsePass {
    fn name(&self) -> &'static str { "cse" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut eliminated = 0usize;

        for func in &mut module.functions {
            // Global (cross-block) expression table: (opcode, operands) -> first result ValueId
            let mut seen: HashMap<ExprKey, ValueId> = HashMap::new();
            let mut replacements: HashMap<ValueId, ValueId> = HashMap::new();

            for block in &func.blocks {
                for instr in &block.instructions {
                    // Skip side-effecting, terminator, and proof instructions
                    if instr.op.has_side_effect() || instr.op.is_terminator() || instr.op.is_proof() {
                        continue;
                    }

                    // Skip Phi -- phi nodes are block-dependent by definition
                    if instr.op == OpCode::Phi {
                        continue;
                    }

                    let key = ExprKey {
                        op: instr.op,
                        operands: canonicalize_operands(instr.op, &instr.operands),
                    };

                    if let Some(&existing) = seen.get(&key) {
                        replacements.insert(instr.result, existing);
                        changed = true;
                        eliminated += 1;
                    } else {
                        seen.insert(key, instr.result);
                    }
                }
            }

            // Apply replacements: rewrite all operand references
            if !replacements.is_empty() {
                for block in &mut func.blocks {
                    for instr in &mut block.instructions {
                        for op in &mut instr.operands {
                            if let Operand::Value(v) = op {
                                if let Some(&replacement) = replacements.get(v) {
                                    *v = replacement;
                                }
                            }
                        }
                    }
                }

                // Dead instruction removal: convert replaced instructions into
                // Load of the replacement value. A subsequent DCE pass can clean
                // these up fully.
                for block in &mut func.blocks {
                    for instr in &mut block.instructions {
                        if let Some(&replacement) = replacements.get(&instr.result) {
                            instr.op = OpCode::Load;
                            instr.operands = vec![Operand::Value(replacement)];
                        }
                    }
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("eliminated".into(), eliminated)],
        }
    }
}

/// Whether an opcode is commutative (a op b == b op a).
fn is_commutative(op: OpCode) -> bool {
    matches!(op, OpCode::Add | OpCode::Mul)
}

/// Canonicalize operands: for commutative ops, sort operand keys so (a+b) and (b+a) hash the same.
fn canonicalize_operands(op: OpCode, operands: &[Operand]) -> Vec<OpKey> {
    let mut keys: Vec<OpKey> = operands.iter().map(operand_key).collect();
    if is_commutative(op) && keys.len() == 2 {
        if keys[0] > keys[1] {
            keys.swap(0, 1);
        }
    }
    keys
}

#[derive(Hash, Eq, PartialEq)]
struct ExprKey {
    op: OpCode,
    operands: Vec<OpKey>,
}

#[derive(Hash, Eq, PartialEq, Clone, PartialOrd, Ord)]
enum OpKey {
    ImmBool(bool),
    ImmI64(i64),
    ImmF64Bits(u64),
    Value(u32),
    Block(u32),
    Other,
}

fn operand_key(op: &Operand) -> OpKey {
    match op {
        Operand::Value(v) => OpKey::Value(v.0),
        Operand::ImmI64(n) => OpKey::ImmI64(*n),
        Operand::ImmBool(b) => OpKey::ImmBool(*b),
        Operand::ImmF64(f) => OpKey::ImmF64Bits(f.to_bits()),
        Operand::Block(b) => OpKey::Block(b.0),
        _ => OpKey::Other,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType, Instruction, BlockId};

    /// Helper: build a module with one function containing given blocks of instructions.
    fn build_module_with_blocks(blocks: Vec<Vec<Instruction>>) -> IrModule {
        let mut module = IrModule::new("test");
        let fid = module.add_function("main".into(), vec![], IrType::I64);
        let func = module.function_mut(fid).unwrap();
        for (i, instrs) in blocks.into_iter().enumerate() {
            let _bb = func.fresh_block(format!("bb{}", i));
            func.blocks[i].instructions = instrs;
        }
        module
    }

    fn build_test_module(instructions: Vec<Instruction>) -> IrModule {
        build_module_with_blocks(vec![instructions])
    }

    #[test]
    fn test_cse_same_block() {
        // %0 and %1 are params (unknown)
        // %2 = add %0, %1
        // %3 = add %0, %1   -> should be replaced with %2
        // %4 = mul %2, %3   -> operand %3 rewired to %2
        let instrs = vec![
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(3),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
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
        let result = CsePass.run(&mut module);

        assert!(result.changed);
        assert_eq!(result.stats[0].1, 1); // 1 eliminated

        // %4's second operand should now be %2 (rewritten from %3)
        let instr4 = &module.functions[0].blocks[0].instructions[2];
        assert!(matches!(instr4.operands[1], Operand::Value(ValueId(2))));
    }

    #[test]
    fn test_cse_commutative_canonicalization() {
        // %2 = add %0, %1
        // %3 = add %1, %0   -> same as %2 due to commutativity, should be eliminated
        let instrs = vec![
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(3),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(1)), Operand::Value(ValueId(0))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = CsePass.run(&mut module);

        assert!(result.changed);
        assert_eq!(result.stats[0].1, 1); // 1 eliminated

        // %3 should be turned into a Load of %2 (dead instruction)
        let instr3 = &module.functions[0].blocks[0].instructions[1];
        assert_eq!(instr3.op, OpCode::Load);
        assert!(matches!(instr3.operands[0], Operand::Value(ValueId(2))));
    }

    #[test]
    fn test_cse_non_commutative_different() {
        // %2 = sub %0, %1
        // %3 = sub %1, %0   -> NOT the same (sub is not commutative)
        let instrs = vec![
            Instruction {
                result: ValueId(2),
                op: OpCode::Sub,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(3),
                op: OpCode::Sub,
                operands: vec![Operand::Value(ValueId(1)), Operand::Value(ValueId(0))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = CsePass.run(&mut module);

        assert!(!result.changed); // no elimination -- sub is not commutative
    }

    #[test]
    fn test_cse_cross_block() {
        // Block 0: %2 = add %0, %1
        // Block 1: %3 = add %0, %1  -> should be eliminated (cross-block CSE)
        let block0 = vec![
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(10),
                op: OpCode::Jump,
                operands: vec![Operand::Block(BlockId(1))],
                ty: IrType::Void,
            },
        ];
        let block1 = vec![
            Instruction {
                result: ValueId(3),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_module_with_blocks(vec![block0, block1]);
        let result = CsePass.run(&mut module);

        assert!(result.changed);
        assert_eq!(result.stats[0].1, 1);

        // %3 in block1 should be replaced
        let instr3 = &module.functions[0].blocks[1].instructions[0];
        assert_eq!(instr3.op, OpCode::Load);
        assert!(matches!(instr3.operands[0], Operand::Value(ValueId(2))));
    }

    #[test]
    fn test_cse_skips_side_effects() {
        // Two identical Call instructions should NOT be eliminated
        let instrs = vec![
            Instruction {
                result: ValueId(2),
                op: OpCode::Call,
                operands: vec![Operand::Value(ValueId(0))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(3),
                op: OpCode::Call,
                operands: vec![Operand::Value(ValueId(0))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = CsePass.run(&mut module);

        assert!(!result.changed);
    }

    #[test]
    fn test_cse_mul_commutative() {
        // %2 = mul %0, %1
        // %3 = mul %1, %0   -> same as %2
        let instrs = vec![
            Instruction {
                result: ValueId(2),
                op: OpCode::Mul,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(3),
                op: OpCode::Mul,
                operands: vec![Operand::Value(ValueId(1)), Operand::Value(ValueId(0))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = CsePass.run(&mut module);

        assert!(result.changed);
        assert_eq!(result.stats[0].1, 1);
    }

    #[test]
    fn test_cse_no_duplicates() {
        // All different expressions -- nothing to eliminate
        let instrs = vec![
            Instruction {
                result: ValueId(2),
                op: OpCode::Add,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(3),
                op: OpCode::Sub,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
            Instruction {
                result: ValueId(4),
                op: OpCode::Mul,
                operands: vec![Operand::Value(ValueId(0)), Operand::Value(ValueId(1))],
                ty: IrType::I64,
            },
        ];
        let mut module = build_test_module(instrs);
        let result = CsePass.run(&mut module);

        assert!(!result.changed);
    }
}

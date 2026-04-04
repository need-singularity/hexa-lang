//! P7: Common Subexpression Elimination — deduplicate identical computations.

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
            // Hash (opcode, operands) → first result ValueId
            let mut seen: HashMap<ExprKey, ValueId> = HashMap::new();
            let mut replacements: HashMap<ValueId, ValueId> = HashMap::new();

            for block in &func.blocks {
                for instr in &block.instructions {
                    // Skip side-effecting and terminator instructions
                    if instr.op.has_side_effect() || instr.op.is_terminator() || instr.op.is_proof() {
                        continue;
                    }

                    let key = ExprKey {
                        op: instr.op,
                        operands: instr.operands.iter().map(operand_key).collect(),
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

            // Apply replacements
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
            }
        }

        PassResult {
            changed,
            stats: vec![("eliminated".into(), eliminated)],
        }
    }
}

#[derive(Hash, Eq, PartialEq)]
struct ExprKey {
    op: OpCode,
    operands: Vec<OpKey>,
}

#[derive(Hash, Eq, PartialEq)]
enum OpKey {
    Value(u32),
    ImmI64(i64),
    ImmBool(bool),
    ImmF64Bits(u64),
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

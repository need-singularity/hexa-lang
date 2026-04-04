//! P3: Dead Store Elimination — remove stores whose values are never loaded.

use std::collections::HashSet;
use crate::ir::{IrModule, OpCode, Operand, ValueId};
use super::{Pass, PassResult};

pub struct DeadStoreElimPass;

impl Pass for DeadStoreElimPass {
    fn name(&self) -> &'static str { "dead_store_elim" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut eliminated = 0usize;

        for func in &mut module.functions {
            // Collect all used values
            let mut used: HashSet<ValueId> = HashSet::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    for op in &instr.operands {
                        if let Operand::Value(v) = op {
                            used.insert(*v);
                        }
                        if let Operand::PhiEntry(_, v) = op {
                            used.insert(*v);
                        }
                    }
                }
            }

            // Remove stores to values that are never loaded
            for block in &mut func.blocks {
                block.instructions.retain(|instr| {
                    if instr.op == OpCode::Store && !instr.op.is_terminator() {
                        // Check if the store target is ever loaded
                        if let Some(Operand::Value(ptr)) = instr.operands.first() {
                            if !used.contains(ptr) {
                                changed = true;
                                eliminated += 1;
                                return false;
                            }
                        }
                    }
                    true
                });
            }
        }

        PassResult {
            changed,
            stats: vec![("eliminated".into(), eliminated)],
        }
    }
}

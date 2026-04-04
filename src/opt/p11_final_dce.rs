//! P11: Final Dead Code Elimination — remove all unused instructions.

use std::collections::HashSet;
use crate::ir::{IrModule, OpCode, Operand, ValueId};
use super::{Pass, PassResult};

pub struct FinalDcePass;

impl Pass for FinalDcePass {
    fn name(&self) -> &'static str { "final_dce" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut eliminated = 0usize;

        for func in &mut module.functions {
            // Collect all used values (values referenced as operands)
            let mut used: HashSet<ValueId> = HashSet::new();

            // Mark phase: terminators and side-effecting instructions are roots
            let mut worklist: Vec<ValueId> = Vec::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    if instr.op.is_terminator() || instr.op.has_side_effect() || instr.op.is_proof() {
                        // Mark all operand values as used
                        for op in &instr.operands {
                            match op {
                                Operand::Value(v) => { worklist.push(*v); }
                                Operand::PhiEntry(_, v) => { worklist.push(*v); }
                                _ => {}
                            }
                        }
                        used.insert(instr.result);
                    }
                }
            }

            // Propagate: mark all transitive dependencies as used
            while let Some(v) = worklist.pop() {
                if used.insert(v) {
                    // Find the instruction that defines v and mark its operands
                    for block in &func.blocks {
                        for instr in &block.instructions {
                            if instr.result == v {
                                for op in &instr.operands {
                                    match op {
                                        Operand::Value(v2) => worklist.push(*v2),
                                        Operand::PhiEntry(_, v2) => worklist.push(*v2),
                                        _ => {}
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Sweep: remove unused instructions
            for block in &mut func.blocks {
                let before = block.instructions.len();
                block.instructions.retain(|instr| {
                    used.contains(&instr.result)
                        || instr.op.is_terminator()
                        || instr.op.has_side_effect()
                        || instr.op.is_proof()
                });
                let removed = before - block.instructions.len();
                if removed > 0 {
                    changed = true;
                    eliminated += removed;
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("eliminated".into(), eliminated)],
        }
    }
}

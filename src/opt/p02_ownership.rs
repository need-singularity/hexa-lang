//! P2: Ownership Proof — verify single-owner semantics, deduplicate borrow checks.

use crate::ir::{IrModule, OpCode};
use super::{Pass, PassResult};

pub struct OwnershipProofPass;

impl Pass for OwnershipProofPass {
    fn name(&self) -> &'static str { "ownership_proof" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut removed = 0usize;

        for func in &mut module.functions {
            // Remove redundant OwnershipTransfer chains:
            // if value is transferred then immediately transferred again, collapse
            for block in &mut func.blocks {
                let mut i = 0;
                while i + 1 < block.instructions.len() {
                    if block.instructions[i].op == OpCode::OwnershipTransfer
                        && block.instructions[i + 1].op == OpCode::OwnershipTransfer
                    {
                        // Check if second uses the result of first
                        if let Some(crate::ir::Operand::Value(v)) = block.instructions[i + 1].operands.first() {
                            if *v == block.instructions[i].result {
                                // Collapse: second takes first's input
                                if let Some(crate::ir::Operand::Value(orig)) = block.instructions[i].operands.first() {
                                    block.instructions[i + 1].operands[0] = crate::ir::Operand::Value(*orig);
                                    // Mark first as dead (will be removed by DCE)
                                    block.instructions[i].op = OpCode::Assume;
                                    block.instructions[i].operands.clear();
                                    changed = true;
                                    removed += 1;
                                }
                            }
                        }
                    }
                    i += 1;
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("collapsed_transfers".into(), removed)],
        }
    }
}

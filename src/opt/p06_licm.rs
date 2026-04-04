//! P6: Loop Invariant Code Motion — hoist loop-invariant computations.

use std::collections::HashSet;
use crate::ir::{IrModule, OpCode, Operand, ValueId, BlockId};
use super::{Pass, PassResult};

pub struct LicmPass;

impl Pass for LicmPass {
    fn name(&self) -> &'static str { "licm" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut hoisted = 0usize;

        for func in &mut module.functions {
            // Detect natural loops via back edges
            func.rebuild_cfg();
            let loops = detect_loops(func);

            for (header, body_blocks) in &loops {
                // Find values defined inside the loop
                let mut loop_defs: HashSet<ValueId> = HashSet::new();
                for &bid in body_blocks {
                    if let Some(block) = func.block(bid) {
                        for instr in &block.instructions {
                            loop_defs.insert(instr.result);
                        }
                    }
                }

                // An instruction is loop-invariant if:
                // 1. No side effects
                // 2. All operands are defined outside the loop or are constants
                // 3. Not a terminator
                // (Actual hoisting deferred to blowup round 2)
            }
        }

        PassResult {
            changed,
            stats: vec![("hoisted".into(), hoisted)],
        }
    }
}

/// Detect natural loops. Returns (header_block, set_of_body_blocks).
fn detect_loops(func: &crate::ir::IrFunction) -> Vec<(BlockId, HashSet<BlockId>)> {
    let mut loops = Vec::new();

    // Find back edges: edge (B -> H) where H dominates B
    for block in &func.blocks {
        for &succ in &block.succs {
            // Simple heuristic: if successor has a lower block ID, it's likely a back edge
            if succ.0 < block.id.0 {
                let mut body = HashSet::new();
                body.insert(succ);
                body.insert(block.id);
                // Walk predecessors to find full loop body
                collect_loop_body(func, block.id, succ, &mut body);
                loops.push((succ, body));
            }
        }
    }

    loops
}

fn collect_loop_body(
    func: &crate::ir::IrFunction,
    node: BlockId,
    header: BlockId,
    body: &mut HashSet<BlockId>,
) {
    if let Some(block) = func.block(node) {
        for &pred in &block.preds {
            if !body.contains(&pred) {
                body.insert(pred);
                if pred != header {
                    collect_loop_body(func, pred, header, body);
                }
            }
        }
    }
}

//! P9: Code Sinking — move computations closer to their use sites.

use std::collections::{HashMap, HashSet};
use crate::ir::{IrModule, Operand, ValueId, BlockId};
use super::{Pass, PassResult};

pub struct CodeSinkingPass;

impl Pass for CodeSinkingPass {
    fn name(&self) -> &'static str { "code_sinking" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        #[allow(unused_variables, unused_mut)]
        let mut changed = false;
        #[allow(unused_variables, unused_mut)]
        let mut sunk = 0usize;

        // Code sinking moves instructions from a dominator block into a
        // successor block when the instruction is only used in that successor.
        // Full implementation requires dominator tree — skeleton for blowup round.

        for func in &mut module.functions {
            func.rebuild_cfg();

            // Collect use sites: value → set of blocks that use it
            let mut use_sites: HashMap<ValueId, HashSet<BlockId>> = HashMap::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    for op in &instr.operands {
                        if let Operand::Value(v) = op {
                            use_sites.entry(*v).or_default().insert(block.id);
                        }
                    }
                }
            }

            // If a value is defined in block A but only used in block B (single successor),
            // it's a sinking candidate
        }

        PassResult {
            changed,
            stats: vec![("sunk".into(), sunk)],
        }
    }
}

//! P10: Copy Coalescing — eliminate unnecessary copy/move instructions.

use std::collections::HashMap;
use crate::ir::{IrModule, OpCode, Operand, ValueId};
use super::{Pass, PassResult};

pub struct CoalescePass;

impl Pass for CoalescePass {
    fn name(&self) -> &'static str { "coalesce" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut coalesced = 0usize;

        for func in &mut module.functions {
            let mut alias: HashMap<ValueId, ValueId> = HashMap::new();

            // Find copy/move chains: %b = copy %a → replace all uses of %b with %a
            for block in &func.blocks {
                for instr in &block.instructions {
                    if matches!(instr.op, OpCode::Copy | OpCode::Move) {
                        if let Some(Operand::Value(src)) = instr.operands.first() {
                            // Resolve transitive aliases
                            let mut root = *src;
                            while let Some(&next) = alias.get(&root) {
                                root = next;
                            }
                            alias.insert(instr.result, root);
                            coalesced += 1;
                        }
                    }
                }
            }

            // Apply aliases
            if !alias.is_empty() {
                changed = true;
                for block in &mut func.blocks {
                    for instr in &mut block.instructions {
                        for op in &mut instr.operands {
                            if let Operand::Value(v) = op {
                                let mut root = *v;
                                while let Some(&next) = alias.get(&root) {
                                    root = next;
                                }
                                if root != *v {
                                    *v = root;
                                }
                            }
                        }
                    }
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("coalesced".into(), coalesced)],
        }
    }
}

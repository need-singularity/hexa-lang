//! P3: Dead Store Elimination + Store-Load forwarding.
//!
//! 1. Remove stores to values that are never loaded
//! 2. Forward store→load: if Store(ptr, val) immediately followed by Load(ptr), replace Load with Copy(val)

use std::collections::HashSet;
use crate::ir::{IrModule, OpCode, Operand, ValueId};
use super::{Pass, PassResult};

pub struct DeadStoreElimPass;

impl Pass for DeadStoreElimPass {
    fn name(&self) -> &'static str { "dead_store_elim" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut eliminated = 0usize;
        let mut forwarded = 0usize;

        for func in &mut module.functions {
            // Phase 1: Store-Load forwarding within each block
            // Track last stored value for each alloca pointer. When we see Load(ptr),
            // if we know the last Store(ptr, val), replace Load with Copy(val).
            for block in &mut func.blocks {
                use std::collections::HashMap;
                let mut last_stored: HashMap<ValueId, ValueId> = HashMap::new();
                let mut replacements: Vec<(usize, ValueId)> = Vec::new();

                for (idx, instr) in block.instructions.iter().enumerate() {
                    if instr.op == OpCode::Store {
                        if let (Some(Operand::Value(ptr)), Some(Operand::Value(val))) =
                            (instr.operands.get(0), instr.operands.get(1))
                        {
                            last_stored.insert(*ptr, *val);
                        }
                    }
                    if instr.op == OpCode::Load {
                        if let Some(Operand::Value(ptr)) = instr.operands.first() {
                            if let Some(&stored_val) = last_stored.get(ptr) {
                                replacements.push((idx, stored_val));
                            }
                        }
                    }
                    // If a Call happens, invalidate all stores (callee may modify memory)
                    if instr.op == OpCode::Call {
                        last_stored.clear();
                    }
                }

                for (idx, val) in replacements {
                    block.instructions[idx].op = OpCode::Copy;
                    block.instructions[idx].operands = vec![Operand::Value(val)];
                    changed = true;
                    forwarded += 1;
                }
            }

            // Phase 2: Dead store elimination
            let mut used: HashSet<ValueId> = HashSet::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    for op in &instr.operands {
                        if let Operand::Value(v) = op { used.insert(*v); }
                        if let Operand::PhiEntry(_, v) = op { used.insert(*v); }
                    }
                }
            }

            for block in &mut func.blocks {
                block.instructions.retain(|instr| {
                    if instr.op == OpCode::Store && !instr.op.is_terminator() {
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
            stats: vec![
                ("eliminated".into(), eliminated),
                ("forwarded".into(), forwarded),
            ],
        }
    }
}

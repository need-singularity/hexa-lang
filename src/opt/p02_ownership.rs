//! P2: Ownership Proof + mem2reg — promote alloca to registers.
//!
//! Phase 1: Same-block Store→Load forwarding
//! Phase 2: Cross-block forwarding for allocas that are only stored once per block

use std::collections::{HashMap, HashSet};
use crate::ir::{IrModule, OpCode, Operand, ValueId, IrType, BlockId};
use super::{Pass, PassResult};

pub struct OwnershipProofPass;

impl Pass for OwnershipProofPass {
    fn name(&self) -> &'static str { "ownership_proof" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut promoted = 0usize;

        for func in &mut module.functions {
            // Find alloca ValueIds
            let mut allocas: HashSet<ValueId> = HashSet::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    if instr.op == OpCode::Alloc {
                        allocas.insert(instr.result);
                    }
                }
            }
            if allocas.is_empty() { continue; }

            // Check promotability: alloca is only used in Store(ptr,...) and Load(ptr)
            let mut promotable: HashSet<ValueId> = allocas.clone();
            for block in &func.blocks {
                for instr in &block.instructions {
                    match instr.op {
                        OpCode::Store | OpCode::Load | OpCode::Alloc => {}
                        _ => {
                            for op in &instr.operands {
                                if let Operand::Value(v) = op {
                                    if allocas.contains(v) { promotable.remove(v); }
                                }
                            }
                        }
                    }
                    // Store value operand must not be an alloca ptr
                    if instr.op == OpCode::Store {
                        if let Some(Operand::Value(val)) = instr.operands.get(1) {
                            if allocas.contains(val) { promotable.remove(val); }
                        }
                    }
                }
            }
            if promotable.is_empty() { continue; }

            // Phase 1: Within each block, track Store→Load and replace
            for block in &mut func.blocks {
                let mut last_stored: HashMap<ValueId, ValueId> = HashMap::new();
                let mut replacements: Vec<(usize, ValueId)> = Vec::new();

                for (idx, instr) in block.instructions.iter().enumerate() {
                    if instr.op == OpCode::Store {
                        if let (Some(Operand::Value(ptr)), Some(Operand::Value(val))) =
                            (instr.operands.get(0), instr.operands.get(1))
                        {
                            if promotable.contains(ptr) {
                                last_stored.insert(*ptr, *val);
                            }
                        }
                    }
                    if instr.op == OpCode::Load {
                        if let Some(Operand::Value(ptr)) = instr.operands.first() {
                            if promotable.contains(ptr) {
                                if let Some(&stored_val) = last_stored.get(ptr) {
                                    replacements.push((idx, stored_val));
                                }
                            }
                        }
                    }
                    // Call invalidates all (callee may modify via pointer)
                    if instr.op == OpCode::Call {
                        last_stored.clear();
                    }
                }

                for (idx, val) in replacements {
                    block.instructions[idx].op = OpCode::Copy;
                    block.instructions[idx].operands = vec![Operand::Value(val)];
                    changed = true;
                    promoted += 1;
                }
            }

            // Phase 2: Cross-block — for blocks that end with a Jump back to a cond block,
            // if we know the last stored value at block exit, propagate to the load at cond entry.
            // This handles while loops: body stores → cond loads.
            //
            // Build: for each block, what's the last Store value for each alloca at block exit?
            let mut block_exit_store: HashMap<BlockId, HashMap<ValueId, ValueId>> = HashMap::new();

            for block in &func.blocks {
                let mut last: HashMap<ValueId, ValueId> = HashMap::new();
                for instr in &block.instructions {
                    if instr.op == OpCode::Store {
                        if let (Some(Operand::Value(ptr)), Some(Operand::Value(val))) =
                            (instr.operands.get(0), instr.operands.get(1))
                        {
                            if promotable.contains(ptr) {
                                last.insert(*ptr, *val);
                            }
                        }
                    }
                    if instr.op == OpCode::Call {
                        last.clear();
                    }
                }
                block_exit_store.insert(block.id, last);
            }

            // For each block, if ALL predecessors agree on the stored value for an alloca,
            // replace the Load at block entry with Copy.
            // (Simple version: if block has exactly 1 predecessor with a known store)
            func.rebuild_cfg();

            let pred_map: HashMap<BlockId, Vec<BlockId>> = func.blocks.iter()
                .map(|b| (b.id, b.preds.clone()))
                .collect();

            for block in &mut func.blocks {
                let preds = pred_map.get(&block.id).cloned().unwrap_or_default();
                if preds.is_empty() { continue; }

                // For each alloca, check if all preds agree
                for alloca_id in &promotable {
                    let mut values: Vec<ValueId> = Vec::new();
                    let mut all_known = true;
                    for pred_id in &preds {
                        if let Some(store_map) = block_exit_store.get(pred_id) {
                            if let Some(&val) = store_map.get(alloca_id) {
                                values.push(val);
                            } else {
                                all_known = false;
                                break;
                            }
                        } else {
                            all_known = false;
                            break;
                        }
                    }

                    if !all_known || values.is_empty() { continue; }

                    // If all preds store the same value, we can forward
                    if values.iter().all(|v| *v == values[0]) {
                        let forwarded_val = values[0];
                        // Replace Load(alloca_id) in this block with Copy(forwarded_val)
                        // Only the FIRST load (before any store to same alloca)
                        let mut found_store = false;
                        for instr in &mut block.instructions {
                            if instr.op == OpCode::Store {
                                if let Some(Operand::Value(ptr)) = instr.operands.get(0) {
                                    if ptr == alloca_id { found_store = true; }
                                }
                            }
                            if found_store { break; } // stop after first store
                            if instr.op == OpCode::Load {
                                if let Some(Operand::Value(ptr)) = instr.operands.first() {
                                    if ptr == alloca_id {
                                        instr.op = OpCode::Copy;
                                        instr.operands = vec![Operand::Value(forwarded_val)];
                                        changed = true;
                                        promoted += 1;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("promoted".into(), promoted)],
        }
    }
}

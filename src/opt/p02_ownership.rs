//! P2: mem2reg — promote alloca variables to SSA registers with phi insertion.
//!
//! Phase ordering (critical for correctness):
//!   Phase 2 runs FIRST: Cross-block phi insertion for while loops
//!     Pattern: entry stores init → loop header loads → body stores update → back-edge
//!     Transform: insert Phi(init_from_entry, update_from_body) at loop header
//!   Phase 1 runs SECOND: Same-block store→load forwarding (simple)
//!
//! Phase 2 must precede Phase 1 because same-block forwarding destroys the
//! Load/Store pattern that phi placement analysis depends on, causing phi
//! nodes to be missed and forcing extra optimizer iterations.

use std::collections::{HashMap, HashSet};
use crate::ir::{IrModule, IrFunction, OpCode, Operand, ValueId, IrType, BlockId};
use crate::ir::instr::Instruction;
use super::{Pass, PassResult};

pub struct OwnershipProofPass;

impl Pass for OwnershipProofPass {
    fn name(&self) -> &'static str { "ownership_proof" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut promoted = 0usize;

        for func in &mut module.functions {
            let r = promote_allocas(func);
            if r > 0 { changed = true; }
            promoted += r;
        }

        PassResult {
            changed,
            stats: vec![("promoted".into(), promoted)],
        }
    }
}

fn promote_allocas(func: &mut IrFunction) -> usize {
    let mut promoted = 0;

    // Find alloca ValueIds
    let mut allocas: HashSet<ValueId> = HashSet::new();
    for block in &func.blocks {
        for instr in &block.instructions {
            if instr.op == OpCode::Alloc {
                allocas.insert(instr.result);
            }
        }
    }
    if allocas.is_empty() { return 0; }

    // Check promotability: only used in Store(ptr,val) as ptr, and Load(ptr)
    let mut promotable: HashSet<ValueId> = allocas.clone();
    for block in &func.blocks {
        for instr in &block.instructions {
            match instr.op {
                OpCode::Store => {
                    // Store(ptr, val) — val must NOT be an alloca (address escape)
                    if let Some(Operand::Value(val)) = instr.operands.get(1) {
                        if allocas.contains(val) { promotable.remove(val); }
                    }
                }
                OpCode::Load | OpCode::Alloc => {}
                _ => {
                    for op in &instr.operands {
                        if let Operand::Value(v) = op {
                            if allocas.contains(v) { promotable.remove(v); }
                        }
                    }
                }
            }
        }
    }
    if promotable.is_empty() { return 0; }

    func.rebuild_cfg();

    // Phase 2 must run BEFORE Phase 1: cross-block phi insertion needs to
    // see the original Load/Store pattern before same-block forwarding
    // rewrites loads. Running Phase 1 first would destroy the cross-block
    // dataflow information that Phase 2 relies on for phi placement.

    // Detect back-edges: B→H where H dominates B (heuristic: H.id < B.id)
    let mut back_edges: Vec<(BlockId, BlockId)> = Vec::new(); // (body_end, header)
    for block in &func.blocks {
        for &succ in &block.succs {
            if succ.0 < block.id.0 {
                back_edges.push((block.id, succ));
            }
        }
    }

    for (body_end_id, header_id) in &back_edges {
        // Loops with calls are now supported: the register allocator handles
        // spilling around call sites. Phi promotion is still correct because
        // we track the last store value which includes post-call stores.
        // For each promotable alloca, check if:
        // 1. There's a Store to it in some block that jumps to header (preheader or body)
        // 2. There's a Load from it in header or body

        // Find preheader: predecessor of header that is NOT the back-edge source
        let header_preds: Vec<BlockId> = func.block(*header_id)
            .map(|b| b.preds.clone()).unwrap_or_default();
        let preheader_id = header_preds.iter()
            .find(|p| **p != *body_end_id)
            .copied();

        if preheader_id.is_none() { continue; }
        let preheader_id = preheader_id.unwrap();

        for &alloca_id in &promotable {
            // Only insert phi for allocas that are:
            // 1. Stored BEFORE the loop (in preheader) — has an init value
            // 2. Loaded in the header block — used in loop condition or body
            // 3. NOT defined (alloc) inside the loop body — those are loop-local
            let alloc_in_loop = func.blocks.iter()
                .filter(|b| b.id.0 > header_id.0 && b.id.0 <= body_end_id.0)
                .any(|b| b.instructions.iter().any(|i| i.op == OpCode::Alloc && i.result == alloca_id));
            if alloc_in_loop { continue; }

            let loaded_in_header_or_body = func.blocks.iter()
                .filter(|b| b.id.0 >= header_id.0 && b.id.0 <= body_end_id.0)
                .any(|b| b.instructions.iter().any(|i| {
                    i.op == OpCode::Load && matches!(i.operands.first(), Some(Operand::Value(v)) if *v == alloca_id)
                }));
            if !loaded_in_header_or_body { continue; }
            // Find last store value in preheader path (entry→...→preheader)
            let init_val = find_last_store_in_block(func, preheader_id, alloca_id);
            // Find last store value in body_end
            let update_val = find_last_store_in_block(func, *body_end_id, alloca_id);

            // Also check blocks between header and body_end
            let update_val = update_val.or_else(|| {
                // Search all blocks in loop body
                for block in &func.blocks {
                    if block.id.0 > header_id.0 && block.id.0 <= body_end_id.0 {
                        if let Some(v) = find_last_store_val(block, alloca_id) {
                            return Some(v);
                        }
                    }
                }
                None
            });

            if init_val.is_none() || update_val.is_none() { continue; }
            let init_val = init_val.unwrap();
            let update_val = update_val.unwrap();

            // Create Phi at header: phi(preheader→init, body_end→update)
            let phi_vid = func.fresh_value(IrType::I64);

            let phi_instr = Instruction {
                result: phi_vid,
                op: OpCode::Phi,
                operands: vec![
                    Operand::PhiEntry(preheader_id, init_val),
                    Operand::PhiEntry(*body_end_id, update_val),
                ],
                ty: IrType::I64,
            };

            // Insert phi at beginning of header block
            if let Some(header) = func.block_mut(*header_id) {
                header.instructions.insert(0, phi_instr);
            }

            // Replace all Load(alloca) in header and body blocks with Copy(phi_vid)
            // BUT in body, after a Store to this alloca, subsequent Loads should use
            // the stored value (already handled by Phase 1)
            for block in &mut func.blocks {
                if block.id.0 >= header_id.0 && block.id.0 <= body_end_id.0 {
                    let mut current_val = phi_vid; // Start with phi
                    for instr in &mut block.instructions {
                        // After Store(alloca, new_val), update current_val
                        if instr.op == OpCode::Store {
                            if let Some(Operand::Value(ptr)) = instr.operands.get(0) {
                                if *ptr == alloca_id {
                                    if let Some(Operand::Value(val)) = instr.operands.get(1) {
                                        current_val = *val;
                                    }
                                }
                            }
                        }
                        // Replace Load(alloca) with Copy(current_val)
                        if instr.op == OpCode::Load {
                            if let Some(Operand::Value(ptr)) = instr.operands.first() {
                                if *ptr == alloca_id {
                                    instr.op = OpCode::Copy;
                                    instr.operands = vec![Operand::Value(current_val)];
                                    promoted += 1;
                                }
                            }
                        }
                    }
                }
            }

            // Also replace Load(alloca) in exit block (after the loop)
            for block in &mut func.blocks {
                if block.id.0 > body_end_id.0 {
                    // After loop, the alloca's last value is update_val
                    for instr in &mut block.instructions {
                        if instr.op == OpCode::Load {
                            if let Some(Operand::Value(ptr)) = instr.operands.first() {
                                if *ptr == alloca_id {
                                    instr.op = OpCode::Copy;
                                    instr.operands = vec![Operand::Value(update_val)];
                                    promoted += 1;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Rebuild CFG after Phase 2 modifications before running Phase 1
    if promoted > 0 {
        func.rebuild_cfg();
    }

    // Phase 1: Same-block forwarding (runs after Phase 2 so phi nodes are
    // already placed and cross-block dataflow is captured)
    for block in &mut func.blocks {
        let mut last_stored: HashMap<ValueId, ValueId> = HashMap::new();
        let mut replacements: Vec<(usize, ValueId)> = Vec::new();

        for (idx, instr) in block.instructions.iter().enumerate() {
            if instr.op == OpCode::Store {
                if let (Some(Operand::Value(ptr)), Some(Operand::Value(val))) =
                    (instr.operands.get(0), instr.operands.get(1))
                {
                    if promotable.contains(ptr) { last_stored.insert(*ptr, *val); }
                }
            }
            if instr.op == OpCode::Load {
                if let Some(Operand::Value(ptr)) = instr.operands.first() {
                    if promotable.contains(ptr) {
                        if let Some(&val) = last_stored.get(ptr) {
                            replacements.push((idx, val));
                        }
                    }
                }
            }
            if instr.op == OpCode::Call { last_stored.clear(); }
        }

        for (idx, val) in replacements {
            block.instructions[idx].op = OpCode::Copy;
            block.instructions[idx].operands = vec![Operand::Value(val)];
            promoted += 1;
        }
    }

    promoted
}

/// Find the last Store(alloca_id, val) in a specific block, return the stored ValueId
fn find_last_store_in_block(func: &IrFunction, block_id: BlockId, alloca_id: ValueId) -> Option<ValueId> {
    func.block(block_id).and_then(|block| find_last_store_val(block, alloca_id))
}

fn find_last_store_val(block: &crate::ir::BasicBlock, alloca_id: ValueId) -> Option<ValueId> {
    let mut last = None;
    for instr in &block.instructions {
        if instr.op == OpCode::Store {
            if let (Some(Operand::Value(ptr)), Some(Operand::Value(val))) =
                (instr.operands.get(0), instr.operands.get(1))
            {
                if *ptr == alloca_id { last = Some(*val); }
            }
        }
    }
    last
}

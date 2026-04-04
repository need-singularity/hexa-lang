//! P6: Loop Invariant Code Motion — hoist constants and invariant computations
//! out of loops into the preheader (entry block).

use std::collections::{HashMap, HashSet};
use crate::ir::{IrModule, OpCode, Operand, ValueId, BlockId};
use crate::ir::instr::Instruction;
use super::{Pass, PassResult};

pub struct LicmPass;

impl Pass for LicmPass {
    fn name(&self) -> &'static str { "licm" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut hoisted = 0usize;
        // Disabled: LICM+coalesce interaction causes value corruption.
        // Using codegen-level const register pinning instead.
        return PassResult { changed: false, stats: vec![("hoisted".into(), 0)] };

        for func in &mut module.functions {
            func.rebuild_cfg();

            // Detect back edges: edge B→H where H.id < B.id (simple heuristic)
            let mut loops: Vec<(BlockId, HashSet<BlockId>)> = Vec::new();
            for block in &func.blocks {
                for &succ in &block.succs {
                    if succ.0 < block.id.0 {
                        let mut body = HashSet::new();
                        body.insert(succ);
                        body.insert(block.id);
                        // Walk preds to find full loop body
                        collect_body(func, block.id, succ, &mut body);
                        loops.push((succ, body));
                    }
                }
            }

            if loops.is_empty() { continue; }

            // For each loop, find hoistable instructions:
            // - Load(ImmI64/ImmF64/ImmBool) — constant loads
            // - Pure arithmetic where all operands are loop-invariant
            for (header, body_blocks) in &loops {
                let mut to_hoist: Vec<(BlockId, usize)> = Vec::new(); // (block, instr_idx)

                // Values defined outside the loop are invariant
                let mut invariant: HashSet<ValueId> = HashSet::new();
                for block in &func.blocks {
                    if !body_blocks.contains(&block.id) {
                        for instr in &block.instructions {
                            invariant.insert(instr.result);
                        }
                    }
                }

                // Iterative: find instructions whose operands are all invariant
                let mut found = true;
                while found {
                    found = false;
                    for block in &func.blocks {
                        if !body_blocks.contains(&block.id) { continue; }
                        for (idx, instr) in block.instructions.iter().enumerate() {
                            if invariant.contains(&instr.result) { continue; }

                            let hoistable = match instr.op {
                                // Only hoist constant loads (safe — no side effects)
                                OpCode::Load if is_const_load(instr) => true,
                                _ => false,
                            };

                            if hoistable {
                                invariant.insert(instr.result);
                                to_hoist.push((block.id, idx));
                                found = true;
                            }
                        }
                    }
                }

                if to_hoist.is_empty() { continue; }

                // Safe hoist: clone instruction to entry, replace original with Copy
                // This avoids ValueId collision — original ValueId stays in body as Copy
                let mut new_entry_instrs: Vec<Instruction> = Vec::new();
                let mut remap: HashMap<ValueId, ValueId> = HashMap::new();

                // Allocate new ValueIds for hoisted instructions
                let mut next_id = func.next_value_id();

                for (bid, idx) in &to_hoist {
                    if let Some(block) = func.blocks.iter().find(|b| b.id == *bid) {
                        if *idx < block.instructions.len() {
                            let orig = &block.instructions[*idx];
                            let new_vid = ValueId(next_id);
                            next_id += 1;

                            // Clone with new ValueId
                            let mut cloned = orig.clone();
                            cloned.result = new_vid;
                            // Remap operands that reference previously hoisted values
                            for op in &mut cloned.operands {
                                if let Operand::Value(v) = op {
                                    if let Some(&remapped) = remap.get(v) {
                                        *v = remapped;
                                    }
                                }
                            }

                            new_entry_instrs.push(cloned);
                            remap.insert(orig.result, new_vid);

                            func.value_types.insert(new_vid, orig.ty.clone());
                        }
                    }
                }

                // Replace originals in body with Copy(new_vid)
                for (bid, idx) in &to_hoist {
                    if let Some(block) = func.blocks.iter_mut().find(|b| b.id == *bid) {
                        if *idx < block.instructions.len() {
                            let orig_vid = block.instructions[*idx].result;
                            if let Some(&new_vid) = remap.get(&orig_vid) {
                                block.instructions[*idx].op = OpCode::Copy;
                                block.instructions[*idx].operands = vec![Operand::Value(new_vid)];
                            }
                        }
                    }
                }

                // Insert hoisted instructions before entry's terminator
                if let Some(entry) = func.blocks.first_mut() {
                    let insert_pos = if entry.instructions.is_empty() { 0 }
                    else {
                        let last = entry.instructions.len() - 1;
                        if entry.instructions[last].op.is_terminator() { last } else { entry.instructions.len() }
                    };
                    for (i, instr) in new_entry_instrs.into_iter().enumerate() {
                        entry.instructions.insert(insert_pos + i, instr);
                        hoisted += 1;
                        changed = true;
                    }
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("hoisted".into(), hoisted)],
        }
    }
}

fn is_const_load(instr: &Instruction) -> bool {
    if instr.op != OpCode::Load { return false; }
    matches!(instr.operands.first(),
        Some(Operand::ImmI64(_)) | Some(Operand::ImmF64(_)) | Some(Operand::ImmBool(_)))
}

fn collect_body(
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
                    collect_body(func, pred, header, body);
                }
            }
        }
    }
}

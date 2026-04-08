//! P11: Final Dead Code Elimination — remove all unused instructions.
//!
//! Mark-sweep DCE with phi cleanup, empty block elimination, and statistics.

use std::collections::HashSet;
use crate::ir::{IrModule, OpCode, Operand, ValueId};
use crate::ir::instr::BlockId;
use super::{Pass, PassResult};

pub struct FinalDcePass;

impl Pass for FinalDcePass {
    fn name(&self) -> &'static str { "final_dce" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut eliminated = 0usize;
        let mut simplified_phis = 0usize;
        let mut removed_blocks = 0usize;

        for func in &mut module.functions {
            // ── Phase 1: Phi cleanup ──
            // Replace phi nodes where all incoming values are identical.
            let mut phi_replacements: Vec<(ValueId, ValueId)> = Vec::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    if instr.op == OpCode::Phi {
                        let mut phi_values: Vec<ValueId> = Vec::new();
                        for op in &instr.operands {
                            if let Operand::PhiEntry(_, v) = op {
                                if !phi_values.contains(v) {
                                    phi_values.push(*v);
                                }
                            }
                        }
                        // If all phi entries converge to the same value, replace
                        if phi_values.len() == 1 {
                            phi_replacements.push((instr.result, phi_values[0]));
                        }
                    }
                }
            }

            // Apply phi replacements: replace the phi with a Copy
            for (phi_result, replacement_val) in &phi_replacements {
                for block in &mut func.blocks {
                    for instr in &mut block.instructions {
                        if instr.result == *phi_result && instr.op == OpCode::Phi {
                            instr.op = OpCode::Copy;
                            instr.operands = vec![Operand::Value(*replacement_val)];
                            changed = true;
                            simplified_phis += 1;
                        }
                    }
                }
            }

            // ── Phase 2: Mark-sweep DCE ──

            // Collect all used values (values referenced as operands)
            let mut used: HashSet<ValueId> = HashSet::new();

            // Mark phase: terminators, side-effecting, proof, and Return-value
            // instructions are roots.
            let mut worklist: Vec<ValueId> = Vec::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    let is_root = instr.op.is_terminator()
                        || instr.op.has_side_effect()
                        || instr.op.is_proof()
                        || instr.op == OpCode::Return;

                    if is_root {
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

            // ── Phase 3: Empty block elimination ──
            // If a block only contains a single unconditional Jump, redirect
            // predecessors to the target and remove the block.
            // Collect candidates first, then apply.
            let mut redirect_map: Vec<(BlockId, BlockId)> = Vec::new(); // (empty_block, target)
            for block in &func.blocks {
                if block.instructions.len() == 1 {
                    let instr = &block.instructions[0];
                    if instr.op == OpCode::Jump {
                        if let Some(Operand::Block(target)) = instr.operands.first() {
                            redirect_map.push((block.id, *target));
                        }
                    }
                }
            }

            // Apply redirects: in all terminators, replace references to
            // empty_block with target. Skip entry block (index 0).
            for (empty_id, target_id) in &redirect_map {
                // Don't remove the entry block
                if func.blocks.first().map(|b| b.id) == Some(*empty_id) {
                    continue;
                }
                // Redirect predecessors' terminators
                for block in &mut func.blocks {
                    if block.id == *empty_id { continue; }
                    for instr in &mut block.instructions {
                        if instr.op.is_terminator() {
                            for op in &mut instr.operands {
                                match op {
                                    Operand::Block(b) if *b == *empty_id => {
                                        *b = *target_id;
                                        changed = true;
                                    }
                                    Operand::SwitchCase(_, b) if *b == *empty_id => {
                                        *b = *target_id;
                                        changed = true;
                                    }
                                    // Redirect phi entries from empty_block to target
                                    Operand::PhiEntry(b, _) if *b == *empty_id => {
                                        *b = *target_id;
                                        changed = true;
                                    }
                                    _ => {}
                                }
                            }
                        }
                    }
                }
            }

            // Remove empty blocks that were redirected (not entry block)
            let entry_id = func.blocks.first().map(|b| b.id);
            let before_blocks = func.blocks.len();
            let redirect_ids: HashSet<BlockId> = redirect_map.iter()
                .filter(|(eid, _)| Some(*eid) != entry_id)
                .map(|(eid, _)| *eid)
                .collect();
            func.blocks.retain(|b| !redirect_ids.contains(&b.id));
            let blocks_removed = before_blocks - func.blocks.len();
            if blocks_removed > 0 {
                removed_blocks += blocks_removed;
                changed = true;
            }
        }

        PassResult {
            changed,
            stats: vec![
                ("eliminated".into(), eliminated),
                ("simplified_phis".into(), simplified_phis),
                ("removed_blocks".into(), removed_blocks),
            ],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType, Operand, OpCode, ValueId};
    use crate::ir::instr::{BlockId, Instruction};

    fn instr(result: u32, op: OpCode, operands: Vec<Operand>) -> Instruction {
        Instruction {
            result: ValueId(result),
            op,
            operands,
            ty: IrType::I64,
        }
    }

    fn make_module(instructions: Vec<Instruction>) -> IrModule {
        let mut module = IrModule::new("test");
        let fid = module.add_function("f".into(), vec![], IrType::I64);
        let func = module.function_mut(fid).unwrap();
        let _bb = func.fresh_block("entry");
        func.blocks[0].instructions = instructions;
        module
    }

    #[test]
    fn test_unused_computation_removed() {
        // %0 = load 42       (unused — should be removed)
        // %1 = load 10       (used by return)
        // ret %1
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(42)]),
            instr(1, OpCode::Load, vec![Operand::ImmI64(10)]),
            instr(2, OpCode::Return, vec![Operand::Value(ValueId(1))]),
        ]);
        let pass = FinalDcePass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let elim = result.stats.iter().find(|(k, _)| k == "eliminated").unwrap().1;
        assert_eq!(elim, 1);
        // Only 2 instructions should remain: load 10 and ret
        assert_eq!(module.functions[0].blocks[0].instructions.len(), 2);
    }

    #[test]
    fn test_phi_identical_arms_simplified() {
        // phi [bb1: %0, bb2: %0] → copy %0
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(5)]),
            instr(1, OpCode::Phi, vec![
                Operand::PhiEntry(BlockId(1), ValueId(0)),
                Operand::PhiEntry(BlockId(2), ValueId(0)),
            ]),
            instr(2, OpCode::Return, vec![Operand::Value(ValueId(1))]),
        ]);
        let pass = FinalDcePass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let phis = result.stats.iter().find(|(k, _)| k == "simplified_phis").unwrap().1;
        assert_eq!(phis, 1);
        let i = &module.functions[0].blocks[0].instructions[1];
        assert_eq!(i.op, OpCode::Copy);
        assert!(matches!(&i.operands[0], Operand::Value(ValueId(0))));
    }

    #[test]
    fn test_phi_different_arms_not_simplified() {
        // phi [bb1: %0, bb2: %1] — different values, keep as phi
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(5)]),
            instr(1, OpCode::Load, vec![Operand::ImmI64(10)]),
            instr(2, OpCode::Phi, vec![
                Operand::PhiEntry(BlockId(1), ValueId(0)),
                Operand::PhiEntry(BlockId(2), ValueId(1)),
            ]),
            instr(3, OpCode::Return, vec![Operand::Value(ValueId(2))]),
        ]);
        let pass = FinalDcePass;
        let result = pass.run(&mut module);
        let phis = result.stats.iter().find(|(k, _)| k == "simplified_phis").unwrap().1;
        assert_eq!(phis, 0);
        let i = &module.functions[0].blocks[0].instructions[2];
        assert_eq!(i.op, OpCode::Phi);
    }

    #[test]
    fn test_stats_reported_correctly() {
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(42)]),  // dead
            instr(1, OpCode::Load, vec![Operand::ImmI64(99)]),  // dead
            instr(2, OpCode::Load, vec![Operand::ImmI64(10)]),  // used
            instr(3, OpCode::Return, vec![Operand::Value(ValueId(2))]),
        ]);
        let pass = FinalDcePass;
        let result = pass.run(&mut module);
        assert!(result.changed);

        let elim = result.stats.iter().find(|(k, _)| k == "eliminated").unwrap().1;
        let phis = result.stats.iter().find(|(k, _)| k == "simplified_phis").unwrap().1;
        let blocks = result.stats.iter().find(|(k, _)| k == "removed_blocks").unwrap().1;
        assert_eq!(elim, 2);
        assert_eq!(phis, 0);
        assert_eq!(blocks, 0);
    }

    #[test]
    fn test_side_effects_preserved() {
        // store is side-effecting → never removed
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(42)]),
            instr(1, OpCode::Store, vec![Operand::Value(ValueId(0)), Operand::ImmI64(0)]),
            instr(2, OpCode::Return, vec![]),
        ]);
        let pass = FinalDcePass;
        let _result = pass.run(&mut module);
        // All 3 instructions should be kept (store is side-effecting)
        assert_eq!(module.functions[0].blocks[0].instructions.len(), 3);
    }

    #[test]
    fn test_return_values_rooted() {
        // %0 is used by return → must be kept
        let mut module = make_module(vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(42)]),
            instr(1, OpCode::Return, vec![Operand::Value(ValueId(0))]),
        ]);
        let pass = FinalDcePass;
        let result = pass.run(&mut module);
        // Nothing should be removed
        let elim = result.stats.iter().find(|(k, _)| k == "eliminated").unwrap().1;
        assert_eq!(elim, 0);
        assert_eq!(module.functions[0].blocks[0].instructions.len(), 2);
    }
}

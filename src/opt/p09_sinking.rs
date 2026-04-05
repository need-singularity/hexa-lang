//! P9: Code Sinking — move computations closer to their use sites.
//!
//! Sinks instructions from a dominator block into the single successor
//! that uses the result, reducing register pressure and shortening live
//! ranges. Iterates to a fixpoint.

use std::collections::{HashMap, HashSet};
use crate::ir::{IrModule, Operand, ValueId, BlockId};
use super::{Pass, PassResult};

pub struct CodeSinkingPass;

/// Returns true when the instruction can safely be moved (no side effects,
/// not a terminator, not a phi node).
fn is_sinkable(op: &crate::ir::OpCode) -> bool {
    !op.has_side_effect() && !op.is_terminator() && !op.is_proof()
        && *op != crate::ir::OpCode::Phi
}

/// Simple dominator check: B dominates S if B is S's only predecessor.
fn dominates_simple(
    block_b: BlockId,
    block_s: BlockId,
    pred_map: &HashMap<BlockId, Vec<BlockId>>,
) -> bool {
    if let Some(preds) = pred_map.get(&block_s) {
        preds.len() == 1 && preds[0] == block_b
    } else {
        false
    }
}

impl Pass for CodeSinkingPass {
    fn name(&self) -> &'static str { "code_sinking" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut sunk = 0usize;

        for func in &mut module.functions {
            func.rebuild_cfg();

            loop {
                let mut round_changed = false;

                // Build predecessor map from the current CFG.
                let pred_map: HashMap<BlockId, Vec<BlockId>> = func
                    .blocks
                    .iter()
                    .map(|b| (b.id, b.preds.clone()))
                    .collect();

                // Build successor map.
                let succ_map: HashMap<BlockId, Vec<BlockId>> = func
                    .blocks
                    .iter()
                    .map(|b| (b.id, b.succs.clone()))
                    .collect();

                // Build use-site map: ValueId -> set of BlockIds where it is used.
                let mut use_sites: HashMap<ValueId, HashSet<BlockId>> = HashMap::new();
                for block in &func.blocks {
                    for instr in &block.instructions {
                        for op in &instr.operands {
                            match op {
                                Operand::Value(v) => {
                                    use_sites.entry(*v).or_default().insert(block.id);
                                }
                                Operand::PhiEntry(_, v) => {
                                    use_sites.entry(*v).or_default().insert(block.id);
                                }
                                _ => {}
                            }
                        }
                    }
                }

                // Build def-site map: ValueId -> BlockId where it is defined.
                let mut def_site: HashMap<ValueId, BlockId> = HashMap::new();
                for block in &func.blocks {
                    for instr in &block.instructions {
                        def_site.insert(instr.result, block.id);
                    }
                }

                // Find one sink candidate per iteration: (source_block, instr_index, target_block).
                let mut candidate: Option<(BlockId, usize, BlockId)> = None;

                'outer: for block in &func.blocks {
                    let succs = succ_map.get(&block.id).cloned().unwrap_or_default();
                    if succs.is_empty() {
                        continue;
                    }

                    // Iterate in reverse to sink bottom-most first (avoids dep issues).
                    let instr_count = block.instructions.len();
                    for rev_idx in 0..instr_count {
                        let idx = instr_count - 1 - rev_idx;
                        let instr = &block.instructions[idx];

                        if !is_sinkable(&instr.op) {
                            continue;
                        }

                        let result = instr.result;

                        let sites = match use_sites.get(&result) {
                            Some(s) => s,
                            None => continue,
                        };

                        // Used in exactly one block, and not this block.
                        if sites.len() != 1 {
                            continue;
                        }
                        let target = *sites.iter().next().unwrap();
                        if target == block.id {
                            continue;
                        }

                        // Target must be a successor.
                        if !succs.contains(&target) {
                            continue;
                        }

                        // B must dominate S.
                        if !dominates_simple(block.id, target, &pred_map) {
                            continue;
                        }

                        // Verify operands are available: any operand defined in
                        // the same block must come before this instruction.
                        let mut operands_ok = true;
                        for op in &instr.operands {
                            if let Operand::Value(v) = op {
                                if let Some(&db) = def_site.get(v) {
                                    if db == block.id {
                                        let di = block.instructions
                                            .iter()
                                            .position(|i| i.result == *v);
                                        if let Some(di) = di {
                                            if di >= idx {
                                                operands_ok = false;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if !operands_ok {
                            continue;
                        }

                        candidate = Some((block.id, idx, target));
                        break 'outer;
                    }
                }

                if let Some((src_id, idx, tgt_id)) = candidate {
                    let instr = {
                        let src = func.blocks.iter_mut().find(|b| b.id == src_id).unwrap();
                        src.instructions.remove(idx)
                    };
                    let tgt = func.blocks.iter_mut().find(|b| b.id == tgt_id).unwrap();
                    let insert_pos = tgt
                        .instructions
                        .iter()
                        .position(|i| i.op != crate::ir::OpCode::Phi)
                        .unwrap_or(tgt.instructions.len());
                    tgt.instructions.insert(insert_pos, instr);

                    round_changed = true;
                    sunk += 1;
                    changed = true;
                }

                if !round_changed {
                    break;
                }

                func.rebuild_cfg();
            }
        }

        PassResult {
            changed,
            stats: vec![("sunk".into(), sunk)],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType, OpCode};
    use crate::ir::instr::Instruction;

    fn instr(result: u32, op: OpCode, operands: Vec<Operand>, ty: IrType) -> Instruction {
        Instruction { result: ValueId(result), op, operands, ty }
    }

    fn two_block_module(
        bb0_instrs: Vec<Instruction>,
        bb1_instrs: Vec<Instruction>,
    ) -> IrModule {
        let mut module = IrModule::new("test");
        let fid = module.add_function("f".into(), vec![], IrType::I64);
        let func = module.function_mut(fid).unwrap();
        let _ = func.fresh_block("bb0");
        let _ = func.fresh_block("bb1");
        func.blocks[0].instructions = bb0_instrs;
        func.blocks[1].instructions = bb1_instrs;
        module
    }

    #[test]
    fn test_sink_single_use_to_successor() {
        let mut module = two_block_module(
            vec![
                instr(0, OpCode::Load, vec![Operand::ImmI64(42)], IrType::I64),
                instr(1, OpCode::Jump, vec![Operand::Block(BlockId(1))], IrType::Void),
            ],
            vec![
                instr(2, OpCode::Add, vec![Operand::Value(ValueId(0)), Operand::ImmI64(1)], IrType::I64),
                instr(3, OpCode::Return, vec![Operand::Value(ValueId(2))], IrType::Void),
            ],
        );
        let pass = CodeSinkingPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let s = result.stats.iter().find(|(k, _)| k == "sunk").unwrap().1;
        assert_eq!(s, 1);
        assert_eq!(module.functions[0].blocks[0].instructions.len(), 1);
        assert_eq!(module.functions[0].blocks[0].instructions[0].op, OpCode::Jump);
        assert_eq!(module.functions[0].blocks[1].instructions.len(), 3);
        assert_eq!(module.functions[0].blocks[1].instructions[0].op, OpCode::Load);
    }

    #[test]
    fn test_no_sink_if_used_in_source() {
        let mut module = two_block_module(
            vec![
                instr(0, OpCode::Load, vec![Operand::ImmI64(42)], IrType::I64),
                instr(1, OpCode::Add, vec![Operand::Value(ValueId(0)), Operand::ImmI64(1)], IrType::I64),
                instr(2, OpCode::Jump, vec![Operand::Block(BlockId(1))], IrType::Void),
            ],
            vec![
                instr(3, OpCode::Add, vec![Operand::Value(ValueId(0)), Operand::ImmI64(2)], IrType::I64),
                instr(4, OpCode::Return, vec![Operand::Value(ValueId(3))], IrType::Void),
            ],
        );
        let pass = CodeSinkingPass;
        let result = pass.run(&mut module);
        let s = result.stats.iter().find(|(k, _)| k == "sunk").unwrap().1;
        assert_eq!(s, 0);
    }

    #[test]
    fn test_no_sink_side_effect() {
        let mut module = two_block_module(
            vec![
                instr(0, OpCode::Load, vec![Operand::ImmI64(42)], IrType::I64),
                instr(1, OpCode::Store, vec![Operand::Value(ValueId(0)), Operand::ImmI64(0)], IrType::Void),
                instr(2, OpCode::Jump, vec![Operand::Block(BlockId(1))], IrType::Void),
            ],
            vec![
                instr(3, OpCode::Return, vec![], IrType::Void),
            ],
        );
        let pass = CodeSinkingPass;
        let result = pass.run(&mut module);
        let s = result.stats.iter().find(|(k, _)| k == "sunk").unwrap().1;
        assert_eq!(s, 0);
    }

    #[test]
    fn test_no_sink_multiple_predecessors() {
        let mut module = IrModule::new("test");
        let fid = module.add_function("f".into(), vec![], IrType::I64);
        let func = module.function_mut(fid).unwrap();
        let _ = func.fresh_block("bb0");
        let _ = func.fresh_block("bb1");
        let _ = func.fresh_block("bb2");
        func.blocks[0].instructions = vec![
            instr(0, OpCode::Load, vec![Operand::ImmI64(42)], IrType::I64),
            instr(1, OpCode::Jump, vec![Operand::Block(BlockId(2))], IrType::Void),
        ];
        func.blocks[1].instructions = vec![
            instr(2, OpCode::Jump, vec![Operand::Block(BlockId(2))], IrType::Void),
        ];
        func.blocks[2].instructions = vec![
            instr(3, OpCode::Add, vec![Operand::Value(ValueId(0)), Operand::ImmI64(1)], IrType::I64),
            instr(4, OpCode::Return, vec![Operand::Value(ValueId(3))], IrType::Void),
        ];
        let pass = CodeSinkingPass;
        let result = pass.run(&mut module);
        let s = result.stats.iter().find(|(k, _)| k == "sunk").unwrap().1;
        assert_eq!(s, 0);
    }

    #[test]
    fn test_sink_after_phi() {
        let mut module = two_block_module(
            vec![
                instr(0, OpCode::Load, vec![Operand::ImmI64(42)], IrType::I64),
                instr(1, OpCode::Jump, vec![Operand::Block(BlockId(1))], IrType::Void),
            ],
            vec![
                instr(2, OpCode::Phi, vec![
                    Operand::PhiEntry(BlockId(0), ValueId(0)),
                ], IrType::I64),
                instr(3, OpCode::Add, vec![Operand::Value(ValueId(0)), Operand::ImmI64(1)], IrType::I64),
                instr(4, OpCode::Return, vec![Operand::Value(ValueId(3))], IrType::Void),
            ],
        );
        let pass = CodeSinkingPass;
        let result = pass.run(&mut module);
        if result.changed {
            let bb1 = &module.functions[0].blocks[1];
            assert_eq!(bb1.instructions[0].op, OpCode::Phi);
            assert_eq!(bb1.instructions[1].op, OpCode::Load);
        }
    }

    #[test]
    fn test_empty_function_no_crash() {
        let mut module = IrModule::new("test");
        let _fid = module.add_function("f".into(), vec![], IrType::Void);
        let pass = CodeSinkingPass;
        let result = pass.run(&mut module);
        assert!(!result.changed);
    }

    #[test]
    fn test_chain_sinking() {
        let mut module = two_block_module(
            vec![
                instr(0, OpCode::Load, vec![Operand::ImmI64(10)], IrType::I64),
                instr(1, OpCode::Add, vec![Operand::Value(ValueId(0)), Operand::ImmI64(1)], IrType::I64),
                instr(2, OpCode::Jump, vec![Operand::Block(BlockId(1))], IrType::Void),
            ],
            vec![
                instr(3, OpCode::Mul, vec![Operand::Value(ValueId(1)), Operand::ImmI64(2)], IrType::I64),
                instr(4, OpCode::Return, vec![Operand::Value(ValueId(3))], IrType::Void),
            ],
        );
        let pass = CodeSinkingPass;
        let result = pass.run(&mut module);
        assert!(result.changed);
        let s = result.stats.iter().find(|(k, _)| k == "sunk").unwrap().1;
        assert_eq!(s, 2);
        assert_eq!(module.functions[0].blocks[0].instructions.len(), 1);
        assert_eq!(module.functions[0].blocks[1].instructions.len(), 4);
    }
}

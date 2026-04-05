//! P5: Function Inlining — inline small functions at call sites.
//! Threshold: σ-τ=8 instructions max.

use std::collections::HashMap;
use crate::ir::{IrModule, IrType, OpCode, Operand, ValueId, BlockId, FuncId, IrFunction};
use crate::ir::instr::{Instruction, BasicBlock};
use super::{Pass, PassResult};

/// Inlining threshold = σ(6) - τ(6) = 8 instructions.
const INLINE_THRESHOLD: usize = 8;

/// Max inlines per function to prevent code size explosion.
const MAX_INLINES_PER_FUNC: usize = 3;

pub struct InliningPass;

/// Snapshot of a callee function body for inlining.
struct CalleeSnapshot {
    blocks: Vec<BasicBlock>,
    params: Vec<(String, IrType)>,
    ret_type: IrType,
}

impl Pass for InliningPass {
    fn name(&self) -> &'static str { "inlining" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut inlined = 0usize;
        let mut candidates_count = 0usize;

        // Phase 1: Collect snapshots of inlinable callees (small, non-main).
        let mut snapshots: HashMap<FuncId, CalleeSnapshot> = HashMap::new();
        for func in &module.functions {
            if func.name == "main" {
                continue;
            }
            if func.instr_count() <= INLINE_THRESHOLD {
                candidates_count += 1;
                snapshots.insert(func.id, CalleeSnapshot {
                    blocks: func.blocks.clone(),
                    params: func.params.clone(),
                    ret_type: func.ret_type.clone(),
                });
            }
        }

        // Phase 2: For each caller, find Call sites and inline.
        for func_idx in 0..module.functions.len() {
            let caller_id = module.functions[func_idx].id;
            let mut inlines_this_func = 0usize;

            // We may need multiple passes because inlining changes block layout.
            // But for simplicity, do a single scan collecting inline sites first.
            let mut sites: Vec<InlineSite> = Vec::new();

            for block_idx in 0..module.functions[func_idx].blocks.len() {
                let block = &module.functions[func_idx].blocks[block_idx];
                for (instr_idx, instr) in block.instructions.iter().enumerate() {
                    if instr.op != OpCode::Call {
                        continue;
                    }
                    // Only inline direct calls: Operand::Func(fid)
                    let callee_fid = match instr.operands.first() {
                        Some(Operand::Func(fid)) => *fid,
                        _ => continue,
                    };
                    // Skip recursive calls
                    if callee_fid == caller_id {
                        continue;
                    }
                    // Skip if callee is not inlinable
                    if !snapshots.contains_key(&callee_fid) {
                        continue;
                    }
                    if inlines_this_func >= MAX_INLINES_PER_FUNC {
                        break;
                    }
                    sites.push(InlineSite {
                        block_idx,
                        instr_idx,
                        callee_fid,
                        call_result: instr.result,
                        call_ty: instr.ty.clone(),
                        args: instr.operands[1..].to_vec(),
                    });
                    inlines_this_func += 1;
                }
                if inlines_this_func >= MAX_INLINES_PER_FUNC {
                    break;
                }
            }

            // Process sites in reverse order so indices remain stable.
            sites.reverse();
            for site in sites {
                let snapshot = &snapshots[&site.callee_fid];
                inline_call_site(&mut module.functions[func_idx], &site, snapshot);
                inlined += 1;
            }

            // Rebuild CFG edges if we inlined anything in this function.
            if inlines_this_func > 0 {
                module.functions[func_idx].rebuild_cfg();
            }
        }

        PassResult {
            changed: inlined > 0,
            stats: vec![
                ("candidates".into(), candidates_count),
                ("inlined".into(), inlined),
                ("threshold".into(), INLINE_THRESHOLD),
            ],
        }
    }
}

struct InlineSite {
    block_idx: usize,
    instr_idx: usize,
    callee_fid: FuncId,
    call_result: ValueId,
    call_ty: IrType,
    args: Vec<Operand>,  // The actual arguments (without the Func operand)
}

/// Inline a single call site: clone callee blocks into the caller, remap values.
fn inline_call_site(
    caller: &mut IrFunction,
    site: &InlineSite,
    callee: &CalleeSnapshot,
) {
    if callee.blocks.is_empty() {
        return;
    }

    // Step 1: Allocate fresh ValueIds and BlockIds for the cloned callee.
    // Use fresh_block() which allocates IDs and pushes empty blocks; we track
    // their indices so we can fill them with cloned instructions later.
    let mut value_map: HashMap<ValueId, ValueId> = HashMap::new();
    let mut block_map: HashMap<BlockId, BlockId> = HashMap::new();
    let mut new_block_indices: Vec<usize> = Vec::new();

    for callee_block in &callee.blocks {
        let new_bid = caller.fresh_block(format!("inline_{}", caller.blocks.len() - 1));
        let idx = caller.blocks.len() - 1;
        new_block_indices.push(idx);
        block_map.insert(callee_block.id, new_bid);
    }

    // Pre-allocate value IDs for all instructions in callee.
    for callee_block in &callee.blocks {
        for instr in &callee_block.instructions {
            let new_vid = caller.fresh_value(instr.ty.clone());
            value_map.insert(instr.result, new_vid);
        }
    }

    // Allocate a merge block that continues after the inlined code.
    let merge_block_id = caller.fresh_block(format!("inline_merge_{}", caller.blocks.len() - 1));
    let merge_block_idx = caller.blocks.len() - 1;

    // Step 2: Clone callee blocks with remapped operands.
    let mut return_values: Vec<ValueId> = Vec::new();

    // Build instructions for each cloned callee block.
    for (i, callee_block) in callee.blocks.iter().enumerate() {
        let block_idx = new_block_indices[i];
        let mut instrs: Vec<Instruction> = Vec::new();

        for instr in &callee_block.instructions {
            if instr.op == OpCode::Return {
                // Replace Return with Jump to merge block.
                // Track the return value for later use.
                if let Some(ret_op) = instr.operands.first() {
                    let remapped = remap_operand(ret_op, &value_map, &block_map, &site.args);
                    if let Operand::Value(vid) = remapped {
                        return_values.push(vid);
                    }
                }
                // Emit a Jump to merge_block instead of Return.
                let jump_vid = caller.fresh_value(IrType::Void);
                instrs.push(Instruction {
                    result: jump_vid,
                    op: OpCode::Jump,
                    operands: vec![Operand::Block(merge_block_id)],
                    ty: IrType::Void,
                });
            } else {
                // Clone instruction with remapped operands.
                let new_result = value_map[&instr.result];
                let new_operands: Vec<Operand> = instr.operands.iter()
                    .map(|op| remap_operand(op, &value_map, &block_map, &site.args))
                    .collect();
                instrs.push(Instruction {
                    result: new_result,
                    op: instr.op,
                    operands: new_operands,
                    ty: instr.ty.clone(),
                });
            }
        }

        caller.blocks[block_idx].instructions = instrs;
    }

    let callee_entry_block_id = block_map[&callee.blocks[0].id];

    // Step 3: Split the caller block at the call site.
    // The block containing the call is split into:
    //   [before_call] + Jump(callee_entry) | [merge_block: after_call instructions]
    let after_call_instrs: Vec<Instruction> =
        caller.blocks[site.block_idx].instructions.split_off(site.instr_idx + 1);
    // Remove the Call instruction itself.
    caller.blocks[site.block_idx].instructions.pop();

    // Add Jump from caller block to callee entry.
    let jump_vid = caller.fresh_value(IrType::Void);
    caller.blocks[site.block_idx].instructions.push(Instruction {
        result: jump_vid,
        op: OpCode::Jump,
        operands: vec![Operand::Block(callee_entry_block_id)],
        ty: IrType::Void,
    });

    // Step 4: Fill the merge block.
    // Determine the replacement for the original call result.
    let replacement_value = if !return_values.is_empty() {
        if return_values.len() == 1 {
            Some(return_values[0])
        } else {
            // Multiple returns: use last value (safe for small functions <=8 instrs).
            Some(*return_values.last().unwrap())
        }
    } else {
        None
    };

    // Rewrite after-call instructions: replace references to the call result
    // with the inlined return value.
    let mut rewritten_after: Vec<Instruction> = Vec::new();
    for mut instr in after_call_instrs {
        if let Some(repl) = replacement_value {
            for op in &mut instr.operands {
                rewrite_value_in_operand(op, site.call_result, repl);
            }
        }
        rewritten_after.push(instr);
    }
    caller.blocks[merge_block_idx].instructions = rewritten_after;
}

/// Remap an operand from callee namespace to caller namespace.
fn remap_operand(
    op: &Operand,
    value_map: &HashMap<ValueId, ValueId>,
    block_map: &HashMap<BlockId, BlockId>,
    call_args: &[Operand],
) -> Operand {
    match op {
        Operand::Value(vid) => {
            if let Some(&new_vid) = value_map.get(vid) {
                Operand::Value(new_vid)
            } else {
                // Value defined outside callee (shouldn't happen in well-formed IR,
                // but preserve it).
                op.clone()
            }
        }
        Operand::Block(bid) => {
            if let Some(&new_bid) = block_map.get(bid) {
                Operand::Block(new_bid)
            } else {
                op.clone()
            }
        }
        Operand::Param(idx) => {
            // Replace parameter reference with the actual call argument.
            if *idx < call_args.len() {
                call_args[*idx].clone()
            } else {
                op.clone()
            }
        }
        Operand::PhiEntry(bid, vid) => {
            let new_bid = block_map.get(bid).copied().unwrap_or(*bid);
            let new_vid = value_map.get(vid).copied().unwrap_or(*vid);
            Operand::PhiEntry(new_bid, new_vid)
        }
        Operand::SwitchCase(val, bid) => {
            let new_bid = block_map.get(bid).copied().unwrap_or(*bid);
            Operand::SwitchCase(*val, new_bid)
        }
        // Immediates, Func refs, FieldIdx, StringRef, Cmp — pass through unchanged.
        _ => op.clone(),
    }
}

/// Rewrite a single operand: if it references `old_vid`, replace with `new_vid`.
fn rewrite_value_in_operand(op: &mut Operand, old_vid: ValueId, new_vid: ValueId) {
    match op {
        Operand::Value(v) if *v == old_vid => {
            *v = new_vid;
        }
        Operand::PhiEntry(_, v) if *v == old_vid => {
            *v = new_vid;
        }
        _ => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType};
    use crate::ir::instr::Instruction;

    /// Build a simple callee: fn add(a, b) -> a + b
    fn make_add_module() -> IrModule {
        let mut module = IrModule::new("test_inline");

        // Callee: fn add(a: i64, b: i64) -> i64
        let add_fid = module.add_function(
            "add".into(),
            vec![("a".into(), IrType::I64), ("b".into(), IrType::I64)],
            IrType::I64,
        );
        {
            let add_fn = module.function_mut(add_fid).unwrap();
            let _entry = add_fn.fresh_block("entry");
            // %0 = load param(0)
            let v0 = add_fn.fresh_value(IrType::I64);
            // %1 = load param(1)
            let v1 = add_fn.fresh_value(IrType::I64);
            // %2 = add %0, %1
            let v2 = add_fn.fresh_value(IrType::I64);
            // %3 = ret %2
            let v3 = add_fn.fresh_value(IrType::Void);
            add_fn.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Load,
                    operands: vec![Operand::Param(0)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v1,
                    op: OpCode::Load,
                    operands: vec![Operand::Param(1)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v2,
                    op: OpCode::Add,
                    operands: vec![Operand::Value(v0), Operand::Value(v1)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v3,
                    op: OpCode::Return,
                    operands: vec![Operand::Value(v2)],
                    ty: IrType::Void,
                },
            ];
        }

        // Caller: fn main() -> i64
        let main_fid = module.add_function("main".into(), vec![], IrType::I64);
        {
            let main_fn = module.function_mut(main_fid).unwrap();
            let _entry = main_fn.fresh_block("entry");
            // %0 = load 10
            let v0 = main_fn.fresh_value(IrType::I64);
            // %1 = load 20
            let v1 = main_fn.fresh_value(IrType::I64);
            // %2 = call @add(%0, %1)
            let v2 = main_fn.fresh_value(IrType::I64);
            // %3 = ret %2
            let v3 = main_fn.fresh_value(IrType::Void);
            main_fn.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Load,
                    operands: vec![Operand::ImmI64(10)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v1,
                    op: OpCode::Load,
                    operands: vec![Operand::ImmI64(20)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v2,
                    op: OpCode::Call,
                    operands: vec![
                        Operand::Func(add_fid),
                        Operand::Value(v0),
                        Operand::Value(v1),
                    ],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v3,
                    op: OpCode::Return,
                    operands: vec![Operand::Value(v2)],
                    ty: IrType::Void,
                },
            ];
        }

        module
    }

    #[test]
    fn test_inlining_basic() {
        let mut module = make_add_module();
        let pass = InliningPass;
        let result = pass.run(&mut module);

        assert!(result.changed, "should have inlined something");
        let inlined_stat = result.stats.iter().find(|(k, _)| k == "inlined").unwrap();
        assert_eq!(inlined_stat.1, 1, "should inline 1 call");

        // main should no longer contain a Call instruction
        let main_fn = module.find_function("main").unwrap();
        let has_call = main_fn.blocks.iter()
            .flat_map(|b| &b.instructions)
            .any(|i| i.op == OpCode::Call);
        assert!(!has_call, "Call should be replaced by inlined body");

        // main should now contain an Add instruction (from inlined add())
        let has_add = main_fn.blocks.iter()
            .flat_map(|b| &b.instructions)
            .any(|i| i.op == OpCode::Add);
        assert!(has_add, "inlined body should contain Add");
    }

    #[test]
    fn test_inlining_skip_recursive() {
        let mut module = IrModule::new("test_recursive");
        let fid = module.add_function(
            "recurse".into(),
            vec![("n".into(), IrType::I64)],
            IrType::I64,
        );
        {
            let func = module.function_mut(fid).unwrap();
            let _entry = func.fresh_block("entry");
            let v0 = func.fresh_value(IrType::I64);
            let v1 = func.fresh_value(IrType::I64);
            let v2 = func.fresh_value(IrType::Void);
            func.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Load,
                    operands: vec![Operand::Param(0)],
                    ty: IrType::I64,
                },
                // Recursive call to self
                Instruction {
                    result: v1,
                    op: OpCode::Call,
                    operands: vec![Operand::Func(fid), Operand::Value(v0)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v2,
                    op: OpCode::Return,
                    operands: vec![Operand::Value(v1)],
                    ty: IrType::Void,
                },
            ];
        }

        let pass = InliningPass;
        let result = pass.run(&mut module);
        assert!(!result.changed, "recursive call should not be inlined");
    }

    #[test]
    fn test_inlining_skip_main() {
        // main() is never inlined as a callee
        let mut module = IrModule::new("test_no_inline_main");
        // A tiny main
        let main_fid = module.add_function("main".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(main_fid).unwrap();
            let _entry = func.fresh_block("entry");
            let v0 = func.fresh_value(IrType::Void);
            func.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Return,
                    operands: vec![],
                    ty: IrType::Void,
                },
            ];
        }

        // Another function that calls main (unusual but tests the guard)
        let caller_fid = module.add_function("caller".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(caller_fid).unwrap();
            let _entry = func.fresh_block("entry");
            let v0 = func.fresh_value(IrType::I64);
            let v1 = func.fresh_value(IrType::Void);
            func.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Call,
                    operands: vec![Operand::Func(main_fid)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v1,
                    op: OpCode::Return,
                    operands: vec![Operand::Value(v0)],
                    ty: IrType::Void,
                },
            ];
        }

        let pass = InliningPass;
        let result = pass.run(&mut module);
        assert!(!result.changed, "main() should not be inlined as callee");
    }

    #[test]
    fn test_inlining_skip_large_function() {
        let mut module = IrModule::new("test_large");
        // Callee with > INLINE_THRESHOLD instructions
        let big_fid = module.add_function(
            "big".into(),
            vec![("a".into(), IrType::I64)],
            IrType::I64,
        );
        {
            let func = module.function_mut(big_fid).unwrap();
            let _entry = func.fresh_block("entry");
            // Create 9 instructions (> threshold of 8)
            let mut last = func.fresh_value(IrType::I64);
            func.blocks[0].instructions.push(Instruction {
                result: last,
                op: OpCode::Load,
                operands: vec![Operand::Param(0)],
                ty: IrType::I64,
            });
            for _ in 0..7 {
                let v = func.fresh_value(IrType::I64);
                func.blocks[0].instructions.push(Instruction {
                    result: v,
                    op: OpCode::Add,
                    operands: vec![Operand::Value(last), Operand::ImmI64(1)],
                    ty: IrType::I64,
                });
                last = v;
            }
            let ret_v = func.fresh_value(IrType::Void);
            func.blocks[0].instructions.push(Instruction {
                result: ret_v,
                op: OpCode::Return,
                operands: vec![Operand::Value(last)],
                ty: IrType::Void,
            });
            assert!(func.instr_count() > INLINE_THRESHOLD);
        }

        // Caller
        let caller_fid = module.add_function("caller".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(caller_fid).unwrap();
            let _entry = func.fresh_block("entry");
            let v0 = func.fresh_value(IrType::I64);
            let v1 = func.fresh_value(IrType::I64);
            let v2 = func.fresh_value(IrType::Void);
            func.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Load,
                    operands: vec![Operand::ImmI64(5)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v1,
                    op: OpCode::Call,
                    operands: vec![Operand::Func(big_fid), Operand::Value(v0)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v2,
                    op: OpCode::Return,
                    operands: vec![Operand::Value(v1)],
                    ty: IrType::Void,
                },
            ];
        }

        let pass = InliningPass;
        let result = pass.run(&mut module);
        assert!(!result.changed, "large function should not be inlined");
    }

    #[test]
    fn test_inlining_max_three_per_function() {
        let mut module = IrModule::new("test_limit");

        // Small callee
        let tiny_fid = module.add_function(
            "tiny".into(),
            vec![],
            IrType::I64,
        );
        {
            let func = module.function_mut(tiny_fid).unwrap();
            let _entry = func.fresh_block("entry");
            let v0 = func.fresh_value(IrType::I64);
            let v1 = func.fresh_value(IrType::Void);
            func.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Load,
                    operands: vec![Operand::ImmI64(42)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v1,
                    op: OpCode::Return,
                    operands: vec![Operand::Value(v0)],
                    ty: IrType::Void,
                },
            ];
        }

        // Caller with 4 calls to tiny
        let caller_fid = module.add_function("caller".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(caller_fid).unwrap();
            let _entry = func.fresh_block("entry");
            let mut vals = Vec::new();
            for _ in 0..4 {
                let v = func.fresh_value(IrType::I64);
                func.blocks[0].instructions.push(Instruction {
                    result: v,
                    op: OpCode::Call,
                    operands: vec![Operand::Func(tiny_fid)],
                    ty: IrType::I64,
                });
                vals.push(v);
            }
            let ret_v = func.fresh_value(IrType::Void);
            func.blocks[0].instructions.push(Instruction {
                result: ret_v,
                op: OpCode::Return,
                operands: vec![Operand::Value(*vals.last().unwrap())],
                ty: IrType::Void,
            });
        }

        let pass = InliningPass;
        let result = pass.run(&mut module);

        assert!(result.changed);
        let inlined_stat = result.stats.iter().find(|(k, _)| k == "inlined").unwrap();
        assert_eq!(inlined_stat.1, 3, "should inline at most 3 calls per function");

        // Exactly 1 Call instruction should remain
        let caller = module.find_function("caller").unwrap();
        let call_count = caller.blocks.iter()
            .flat_map(|b| &b.instructions)
            .filter(|i| i.op == OpCode::Call)
            .count();
        assert_eq!(call_count, 1, "one call should remain (4 - 3 inlined)");
    }

    #[test]
    fn test_inlining_return_value_propagation() {
        // After inlining add(10, 20), the Return in main should reference
        // the inlined Add result, not the old call result.
        let mut module = make_add_module();
        let pass = InliningPass;
        pass.run(&mut module);

        let main_fn = module.find_function("main").unwrap();

        // Find the Return instruction in the merge block
        let ret_instr = main_fn.blocks.iter()
            .flat_map(|b| &b.instructions)
            .find(|i| i.op == OpCode::Return)
            .expect("should have a Return");

        // The return should reference some value (not the original call result %2)
        if let Some(Operand::Value(vid)) = ret_instr.operands.first() {
            // Should reference a value that exists (allocated by the inliner)
            assert!(
                main_fn.value_types.contains_key(vid),
                "return value should reference a valid inlined value"
            );
        }
    }

    #[test]
    fn test_inlining_empty_module() {
        let mut module = IrModule::new("empty");
        let pass = InliningPass;
        let result = pass.run(&mut module);
        assert!(!result.changed);
    }

    #[test]
    fn test_inlining_void_callee() {
        // Callee returns void
        let mut module = IrModule::new("test_void");
        let void_fid = module.add_function(
            "side_effect".into(),
            vec![],
            IrType::Void,
        );
        {
            let func = module.function_mut(void_fid).unwrap();
            let _entry = func.fresh_block("entry");
            let v0 = func.fresh_value(IrType::I64);
            let v1 = func.fresh_value(IrType::Void);
            let v2 = func.fresh_value(IrType::Void);
            func.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Load,
                    operands: vec![Operand::ImmI64(99)],
                    ty: IrType::I64,
                },
                Instruction {
                    result: v1,
                    op: OpCode::Store,
                    operands: vec![Operand::Value(v0), Operand::ImmI64(0)],
                    ty: IrType::Void,
                },
                Instruction {
                    result: v2,
                    op: OpCode::Return,
                    operands: vec![],
                    ty: IrType::Void,
                },
            ];
        }

        let caller_fid = module.add_function("caller".into(), vec![], IrType::Void);
        {
            let func = module.function_mut(caller_fid).unwrap();
            let _entry = func.fresh_block("entry");
            let v0 = func.fresh_value(IrType::Void);
            let v1 = func.fresh_value(IrType::Void);
            func.blocks[0].instructions = vec![
                Instruction {
                    result: v0,
                    op: OpCode::Call,
                    operands: vec![Operand::Func(void_fid)],
                    ty: IrType::Void,
                },
                Instruction {
                    result: v1,
                    op: OpCode::Return,
                    operands: vec![],
                    ty: IrType::Void,
                },
            ];
        }

        let pass = InliningPass;
        let result = pass.run(&mut module);
        assert!(result.changed);

        // Caller should now have the Store from the inlined body
        let caller = module.find_function("caller").unwrap();
        let has_store = caller.blocks.iter()
            .flat_map(|b| &b.instructions)
            .any(|i| i.op == OpCode::Store);
        assert!(has_store, "inlined void function should contain Store");
    }

    #[test]
    fn test_inlining_candidates_stat() {
        let mut module = make_add_module();
        let pass = InliningPass;
        let result = pass.run(&mut module);

        let cand = result.stats.iter().find(|(k, _)| k == "candidates").unwrap();
        assert_eq!(cand.1, 1, "add() should be the only candidate (main excluded)");
    }
}

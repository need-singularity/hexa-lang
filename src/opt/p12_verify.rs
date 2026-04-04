//! P12: IR Verification — read-only invariant check (no mutations).
//!
//! Validates that the IR is well-formed after all optimization passes.

use crate::ir::{IrModule, OpCode, Operand, ValueId, BlockId};
use super::{Pass, PassResult};
use std::collections::HashSet;

pub struct VerifyPass;

impl Pass for VerifyPass {
    fn name(&self) -> &'static str { "verify" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut errors = 0usize;

        for func in &module.functions {
            // 1. Every block must end with a terminator
            for block in &func.blocks {
                if !block.instructions.is_empty() && !block.is_terminated() {
                    errors += 1;
                }
            }

            // 2. SSA: every value must be defined before use
            let mut defined: HashSet<ValueId> = HashSet::new();
            // Parameters are pre-defined
            for (i, _) in func.params.iter().enumerate() {
                defined.insert(ValueId(i as u32));
            }

            for block in &func.blocks {
                for instr in &block.instructions {
                    // Check all value operands are defined
                    for op in &instr.operands {
                        match op {
                            Operand::Value(v) => {
                                if !defined.contains(v) && !is_phi_safe(v, &instr.op) {
                                    errors += 1;
                                }
                            }
                            Operand::PhiEntry(_, v) => {
                                // Phi entries may reference values from other blocks
                                // (forward references are OK for phi)
                            }
                            _ => {}
                        }
                    }
                    defined.insert(instr.result);
                }
            }

            // 3. No duplicate value definitions
            let mut all_defs: HashSet<ValueId> = HashSet::new();
            for block in &func.blocks {
                for instr in &block.instructions {
                    if !all_defs.insert(instr.result) {
                        errors += 1; // duplicate definition
                    }
                }
            }

            // 4. Branch targets must exist
            let block_ids: HashSet<BlockId> = func.blocks.iter().map(|b| b.id).collect();
            for block in &func.blocks {
                for instr in &block.instructions {
                    for op in &instr.operands {
                        match op {
                            Operand::Block(b) => {
                                if !block_ids.contains(b) {
                                    errors += 1;
                                }
                            }
                            Operand::SwitchCase(_, b) | Operand::PhiEntry(b, _) => {
                                if !block_ids.contains(b) {
                                    errors += 1;
                                }
                            }
                            _ => {}
                        }
                    }
                }
            }

            // 5. Function calls must reference valid functions
            let func_ids: HashSet<crate::ir::FuncId> = module.functions.iter().map(|f| f.id).collect();
            for block in &func.blocks {
                for instr in &block.instructions {
                    if instr.op == OpCode::Call {
                        if let Some(Operand::Func(fid)) = instr.operands.first() {
                            if !func_ids.contains(fid) {
                                errors += 1;
                            }
                        }
                    }
                }
            }
        }

        PassResult {
            changed: false, // verify never mutates
            stats: vec![("errors".into(), errors)],
        }
    }
}

fn is_phi_safe(_v: &ValueId, op: &OpCode) -> bool {
    *op == OpCode::Phi
}

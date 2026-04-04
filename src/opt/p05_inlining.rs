//! P5: Function Inlining — inline small functions at call sites.
//! Threshold: σ-τ=8 instructions max.

use crate::ir::{IrModule, OpCode, Operand};
use super::{Pass, PassResult};

/// Inlining threshold = σ(6) - τ(6) = 8 instructions.
const INLINE_THRESHOLD: usize = 8;

pub struct InliningPass;

impl Pass for InliningPass {
    fn name(&self) -> &'static str { "inlining" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut inlined = 0usize;

        // Collect inline candidates: functions with ≤ threshold instructions
        let candidates: Vec<(String, usize)> = module.functions.iter()
            .filter(|f| f.name != "main")
            .filter(|f| f.instr_count() <= INLINE_THRESHOLD)
            .map(|f| (f.name.clone(), f.instr_count()))
            .collect();

        // For now, just mark candidates. Full inlining requires CFG cloning.
        // TODO: implement function body copying with value renaming

        PassResult {
            changed: false,
            stats: vec![
                ("candidates".into(), candidates.len()),
                ("inlined".into(), inlined),
                ("threshold".into(), INLINE_THRESHOLD),
            ],
        }
    }
}

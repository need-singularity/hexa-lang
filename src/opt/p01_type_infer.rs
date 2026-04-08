//! P1: Type Inference — resolve untyped values via context propagation.

use crate::ir::{IrModule, IrType, OpCode};
use super::{Pass, PassResult};

pub struct TypeInferPass;

impl Pass for TypeInferPass {
    fn name(&self) -> &'static str { "type_infer" }

    fn run(&self, module: &mut IrModule) -> PassResult {
        let mut changed = false;
        let mut inferred = 0usize;

        for func in &mut module.functions {
            // Propagate types from operands to results
            for block in &mut func.blocks {
                for instr in &mut block.instructions {
                    if instr.ty == IrType::I64 {
                        // Try to infer more specific type from operands
                        if let Some(better) = infer_from_op(&instr.op, &func.value_types, &instr.operands) {
                            if better != instr.ty {
                                instr.ty = better.clone();
                                func.value_types.insert(instr.result, better);
                                changed = true;
                                inferred += 1;
                            }
                        }
                    }
                }
            }
        }

        PassResult {
            changed,
            stats: vec![("inferred".into(), inferred)],
        }
    }
}

fn infer_from_op(
    op: &OpCode,
    types: &std::collections::HashMap<crate::ir::ValueId, IrType>,
    operands: &[crate::ir::Operand],
) -> Option<IrType> {
    use crate::ir::Operand;

    match op {
        // Arithmetic inherits from first operand
        OpCode::Add | OpCode::Sub | OpCode::Mul | OpCode::Div | OpCode::Mod => {
            if let Some(Operand::Value(v)) = operands.first() {
                return types.get(v).cloned();
            }
        }
        // Comparison always produces bool
        OpCode::Assert | OpCode::Assume | OpCode::Invariant => {
            return Some(IrType::Void);
        }
        // Load from immediate
        OpCode::Load => {
            if let Some(Operand::ImmF64(_)) = operands.first() {
                return Some(IrType::F64);
            }
            if let Some(Operand::ImmBool(_)) = operands.first() {
                return Some(IrType::Bool);
            }
            if let Some(Operand::StringRef(_)) = operands.first() {
                return Some(IrType::Str);
            }
        }
        _ => {}
    }
    None
}

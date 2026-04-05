//! x86-64 Machine Code Emitter — secondary target.
//!
//! Generates raw x86-64 instructions (variable width).

use std::collections::HashMap;
use crate::ir::{IrModule, IrFunction, OpCode, Operand, ValueId};
use super::regalloc::{AllocResult, FuncAlloc, Location};

/// x86-64 register encoding (REX.W compatible).
/// Order: rax=0, rcx=1, rdx=2, rbx=3, rsi=4, rdi=5, r8-r15=6-13
const REG_ENC: [u8; 14] = [0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];

/// Emit x86-64 machine code for the module.
pub fn emit(module: &IrModule, alloc: &AllocResult) -> Vec<u8> {
    let mut code = Vec::new();

    for func in &module.functions {
        if let Some(func_alloc) = alloc.func_allocs.get(&func.name) {
            emit_function(&mut code, func, func_alloc, module);
        }
    }

    code
}

fn emit_function(
    code: &mut Vec<u8>,
    func: &IrFunction,
    alloc: &FuncAlloc,
    module: &IrModule,
) {
    // Prologue
    emit_prologue(code, alloc);

    let mut block_offsets: HashMap<crate::ir::BlockId, usize> = HashMap::new();
    let mut fixups: Vec<(usize, crate::ir::BlockId)> = Vec::new();

    for block in &func.blocks {
        block_offsets.insert(block.id, code.len());

        for instr in &block.instructions {
            emit_instruction(code, instr, alloc, &mut fixups, module);
        }
    }

    // Resolve fixups
    for (offset, target_block) in &fixups {
        if let Some(&target_offset) = block_offsets.get(target_block) {
            let rel = target_offset as i32 - (*offset as i32 + 4);
            code[*offset..*offset + 4].copy_from_slice(&rel.to_le_bytes());
        }
    }

    // Epilogue
    emit_epilogue(code, alloc);
}

fn emit_prologue(code: &mut Vec<u8>, alloc: &FuncAlloc) {
    // push rbp
    code.push(0x55);
    // mov rbp, rsp
    code.extend_from_slice(&[0x48, 0x89, 0xe5]);
    // sub rsp, stack_size
    if alloc.stack_size > 0 {
        code.extend_from_slice(&[0x48, 0x81, 0xec]);
        code.extend_from_slice(&(alloc.stack_size as u32).to_le_bytes());
    }
}

fn emit_epilogue(code: &mut Vec<u8>, _alloc: &FuncAlloc) {
    // mov rsp, rbp
    code.extend_from_slice(&[0x48, 0x89, 0xec]);
    // pop rbp
    code.push(0x5d);
    // ret
    code.push(0xc3);
}

fn emit_instruction(
    code: &mut Vec<u8>,
    instr: &crate::ir::Instruction,
    alloc: &FuncAlloc,
    fixups: &mut Vec<(usize, crate::ir::BlockId)>,
    _module: &IrModule,
) {
    let dst = x86_reg(instr.result, alloc);

    match instr.op {
        OpCode::Add => {
            let (r1, r2) = x86_two_regs(&instr.operands, alloc);
            // mov dst, r1; add dst, r2
            emit_mov_rr(code, dst, r1);
            emit_alu_rr(code, 0x01, dst, r2); // add
        }
        OpCode::Sub => {
            let (r1, r2) = x86_two_regs(&instr.operands, alloc);
            emit_mov_rr(code, dst, r1);
            emit_alu_rr(code, 0x29, dst, r2); // sub
        }
        OpCode::Mul => {
            let (r1, r2) = x86_two_regs(&instr.operands, alloc);
            emit_mov_rr(code, dst, r1);
            // imul dst, r2
            code.extend_from_slice(&[0x48, 0x0f, 0xaf]);
            code.push(0xc0 | (dst << 3) | r2);
        }
        OpCode::Neg => {
            let r1 = x86_one_reg(&instr.operands, alloc);
            emit_mov_rr(code, dst, r1);
            // neg dst
            code.extend_from_slice(&[0x48, 0xf7, 0xd8 | dst]);
        }

        OpCode::Load => {
            match instr.operands.first() {
                Some(Operand::Param(i)) => {
                    // x86-64 SysV: rdi, rsi, rdx, rcx, r8, r9
                    let param_regs: [u8; 6] = [7, 6, 2, 1, 8, 9];
                    let src = if *i < 6 { param_regs[*i] } else { 0 };
                    if dst != src {
                        code.extend_from_slice(&[0x48, 0x89, 0xC0 | (src << 3) | dst]);
                    }
                }
                Some(Operand::ImmI64(n)) => {
                    emit_mov_imm64(code, dst, *n);
                }
                Some(Operand::Value(ptr)) => {
                    let base = x86_reg(*ptr, alloc);
                    // mov dst, [base]
                    code.extend_from_slice(&[0x48, 0x8b, (dst << 3) | base]);
                }
                _ => {
                    emit_mov_imm64(code, dst, 0);
                }
            }
        }
        OpCode::Store => {
            if let (Some(Operand::Value(ptr)), Some(Operand::Value(val))) =
                (instr.operands.get(0), instr.operands.get(1))
            {
                let base = x86_reg(*ptr, alloc);
                let src = x86_reg(*val, alloc);
                // mov [base], src
                code.extend_from_slice(&[0x48, 0x89, (src << 3) | base]);
            }
        }
        OpCode::Copy | OpCode::Move | OpCode::OwnershipTransfer => {
            let r1 = x86_one_reg(&instr.operands, alloc);
            emit_mov_rr(code, dst, r1);
        }

        OpCode::Jump => {
            if let Some(Operand::Block(target)) = instr.operands.first() {
                code.push(0xe9); // jmp rel32
                fixups.push((code.len(), *target));
                code.extend_from_slice(&[0, 0, 0, 0]); // placeholder
            }
        }
        OpCode::Branch => {
            if let (Some(Operand::Value(cond)), Some(Operand::Block(then_bb)), Some(Operand::Block(else_bb))) =
                (instr.operands.get(0), instr.operands.get(1), instr.operands.get(2))
            {
                let cr = x86_reg(*cond, alloc);
                // test cr, cr
                code.extend_from_slice(&[0x48, 0x85, 0xc0 | (cr << 3) | cr]);
                // jnz then
                code.extend_from_slice(&[0x0f, 0x85]);
                fixups.push((code.len(), *then_bb));
                code.extend_from_slice(&[0, 0, 0, 0]);
                // jmp else
                code.push(0xe9);
                fixups.push((code.len(), *else_bb));
                code.extend_from_slice(&[0, 0, 0, 0]);
            }
        }
        OpCode::Return => {
            if let Some(Operand::Value(v)) = instr.operands.first() {
                let src = x86_reg(*v, alloc);
                if src != 0 {
                    emit_mov_rr(code, 0, src); // mov rax, src
                }
            }
        }
        OpCode::Call => {
            // Simplified: emit call with placeholder
            code.push(0xe8);
            code.extend_from_slice(&[0, 0, 0, 0]); // rel32 placeholder
        }

        // Proof ops: no codegen
        OpCode::Assert | OpCode::Assume | OpCode::Invariant
        | OpCode::LifetimeStart | OpCode::LifetimeEnd => {}

        _ => {} // Other ops handled similarly
    }
}

fn emit_mov_rr(code: &mut Vec<u8>, dst: u8, src: u8) {
    if dst != src {
        code.extend_from_slice(&[0x48, 0x89, 0xc0 | (src << 3) | dst]);
    }
}

fn emit_alu_rr(code: &mut Vec<u8>, opcode: u8, dst: u8, src: u8) {
    code.extend_from_slice(&[0x48, opcode, 0xc0 | (src << 3) | dst]);
}

fn emit_mov_imm64(code: &mut Vec<u8>, reg: u8, val: i64) {
    // movabs reg, imm64
    code.extend_from_slice(&[0x48, 0xb8 + reg]);
    code.extend_from_slice(&(val as u64).to_le_bytes());
}

fn x86_reg(v: ValueId, alloc: &FuncAlloc) -> u8 {
    match alloc.locations.get(&v) {
        Some(Location::Reg(r)) => {
            if (r.0 as usize) < REG_ENC.len() { REG_ENC[r.0 as usize] } else { 0 }
        }
        Some(Location::Stack(_)) => 0, // spill → rax temp
        None => 0,
    }
}

fn x86_one_reg(operands: &[Operand], alloc: &FuncAlloc) -> u8 {
    match operands.first() {
        Some(Operand::Value(v)) => x86_reg(*v, alloc),
        _ => 0,
    }
}

fn x86_two_regs(operands: &[Operand], alloc: &FuncAlloc) -> (u8, u8) {
    let r1 = match operands.get(0) { Some(Operand::Value(v)) => x86_reg(*v, alloc), _ => 0 };
    let r2 = match operands.get(1) { Some(Operand::Value(v)) => x86_reg(*v, alloc), _ => 0 };
    (r1, r2)
}

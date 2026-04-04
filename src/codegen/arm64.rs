//! ARM64 (AArch64) Machine Code Emitter — primary target.
//!
//! Generates raw ARM64 instructions (4 bytes each, fixed width).

use std::collections::HashMap;
use crate::ir::{IrModule, IrFunction, OpCode, Operand, ValueId, IrType};
use super::regalloc::{AllocResult, FuncAlloc, Location, PhysReg};

/// ARM64 register names.
const REG_NAMES: [&str; 32] = [
    "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
    "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
    "x24", "x25", "x26", "x27", "x28", "x29", "x30", "xzr",
];

const FP: u8 = 29;  // x29 = frame pointer
const LR: u8 = 30;  // x30 = link register
const SP: u8 = 31;  // x31 = stack pointer (or xzr in some contexts)

/// Emit ARM64 machine code for the module.
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

    // Label tracking for branch resolution
    let mut block_offsets: HashMap<crate::ir::BlockId, usize> = HashMap::new();
    let mut fixups: Vec<(usize, crate::ir::BlockId)> = Vec::new();

    for block in &func.blocks {
        block_offsets.insert(block.id, code.len());

        for instr in &block.instructions {
            emit_instruction(code, instr, alloc, &mut fixups, module);
        }
    }

    // Resolve branch fixups
    for (offset, target_block) in &fixups {
        if let Some(&target_offset) = block_offsets.get(target_block) {
            let rel = (target_offset as i64 - *offset as i64) / 4;
            patch_branch(code, *offset, rel as i32);
        }
    }

    // Epilogue
    emit_epilogue(code, alloc);
}

fn emit_prologue(code: &mut Vec<u8>, alloc: &FuncAlloc) {
    // stp x29, x30, [sp, #-16]!
    emit32(code, 0xa9bf7bfd);
    // mov x29, sp
    emit32(code, 0x910003fd);
    // sub sp, sp, #stack_size
    if alloc.stack_size > 0 {
        let imm = (alloc.stack_size as u32) & 0xFFF;
        emit32(code, 0xd10003ff | (imm << 10));
    }
}

fn emit_epilogue(code: &mut Vec<u8>, alloc: &FuncAlloc) {
    // add sp, sp, #stack_size
    if alloc.stack_size > 0 {
        let imm = (alloc.stack_size as u32) & 0xFFF;
        emit32(code, 0x910003ff | (imm << 10));
    }
    // ldp x29, x30, [sp], #16
    emit32(code, 0xa8c17bfd);
    // ret
    emit32(code, 0xd65f03c0);
}

fn emit_instruction(
    code: &mut Vec<u8>,
    instr: &crate::ir::Instruction,
    alloc: &FuncAlloc,
    fixups: &mut Vec<(usize, crate::ir::BlockId)>,
    module: &IrModule,
) {
    let dst = reg_for(instr.result, alloc);

    match instr.op {
        // ── Arithmetic ──
        OpCode::Add => {
            let (r1, r2) = two_regs(&instr.operands, alloc);
            // add Xd, Xn, Xm
            emit32(code, encode_rrr(0x8b000000, dst, r1, r2));
        }
        OpCode::Sub => {
            let (r1, r2) = two_regs(&instr.operands, alloc);
            // sub Xd, Xn, Xm
            emit32(code, encode_rrr(0xcb000000, dst, r1, r2));
        }
        OpCode::Mul => {
            let (r1, r2) = two_regs(&instr.operands, alloc);
            // mul Xd, Xn, Xm
            emit32(code, encode_mul(dst, r1, r2));
        }
        OpCode::Div => {
            let (r1, r2) = two_regs(&instr.operands, alloc);
            // sdiv Xd, Xn, Xm
            emit32(code, encode_div(dst, r1, r2));
        }
        OpCode::Mod => {
            let (r1, r2) = two_regs(&instr.operands, alloc);
            // msub Xd, Xtmp, Xm, Xn (Xd = Xn - (Xn/Xm)*Xm)
            let tmp = 16u8; // x16 as temp
            emit32(code, encode_div(tmp, r1, r2));       // sdiv x16, Xn, Xm
            emit32(code, encode_msub(dst, tmp, r2, r1)); // msub Xd, x16, Xm, Xn
        }
        OpCode::Neg => {
            let r1 = one_reg(&instr.operands, alloc);
            // neg Xd, Xn → sub Xd, xzr, Xn
            emit32(code, encode_rrr(0xcb000000, dst, 31, r1));
        }

        // ── Memory ──
        OpCode::Load => {
            match instr.operands.first() {
                Some(Operand::ImmI64(n)) => {
                    emit_mov_imm(code, dst, *n);
                }
                Some(Operand::ImmF64(f)) => {
                    emit_mov_imm(code, dst, f.to_bits() as i64);
                }
                Some(Operand::ImmBool(b)) => {
                    emit_mov_imm(code, dst, if *b { 1 } else { 0 });
                }
                Some(Operand::Value(ptr)) => {
                    let base = reg_for(*ptr, alloc);
                    // ldr Xd, [Xn]
                    emit32(code, 0xf9400000 | ((base as u32) << 5) | (dst as u32));
                }
                _ => {
                    emit_mov_imm(code, dst, 0);
                }
            }
        }
        OpCode::Store => {
            if let (Some(Operand::Value(ptr)), Some(Operand::Value(val))) =
                (instr.operands.get(0), instr.operands.get(1))
            {
                let base = reg_for(*ptr, alloc);
                let src = reg_for(*val, alloc);
                // str Xsrc, [Xbase]
                emit32(code, 0xf9000000 | ((base as u32) << 5) | (src as u32));
            }
        }
        OpCode::Alloc => {
            // mmap syscall: x16=197, x0=addr(0), x1=size, x2=prot(3), x3=flags(0x1002), x4=fd(-1), x5=offset(0)
            let size = instr.ty.size_bytes().max(8);
            emit_mov_imm(code, 0, 0);           // addr = 0
            emit_mov_imm(code, 1, size as i64); // size
            emit_mov_imm(code, 2, 3);           // PROT_READ|PROT_WRITE
            emit_mov_imm(code, 3, 0x1002);      // MAP_ANON|MAP_PRIVATE
            emit_mov_imm(code, 4, -1);           // fd = -1
            emit_mov_imm(code, 5, 0);           // offset = 0
            emit_mov_imm(code, 16, 197);        // SYS_mmap
            emit32(code, 0xd4000001);           // svc #0
            if dst != 0 {
                // mov Xd, x0
                emit32(code, 0xaa0003e0 | (dst as u32));
            }
        }
        OpCode::Free => {
            if let Some(Operand::Value(ptr)) = instr.operands.first() {
                let r = reg_for(*ptr, alloc);
                if r != 0 { emit32(code, 0xaa0003e0 | (r as u32) | ((r as u32) << 16)); } // mov x0, Xr
                emit_mov_imm(code, 1, 4096);  // size (page)
                emit_mov_imm(code, 16, 73);   // SYS_munmap
                emit32(code, 0xd4000001);     // svc #0
            }
        }
        OpCode::Copy | OpCode::Move | OpCode::OwnershipTransfer => {
            let r1 = one_reg(&instr.operands, alloc);
            if dst != r1 {
                // mov Xd, Xn
                emit32(code, 0xaa0003e0 | (dst as u32) | ((r1 as u32) << 16));
            }
        }

        // ── Control ──
        OpCode::Jump => {
            if let Some(Operand::Block(target)) = instr.operands.first() {
                fixups.push((code.len(), *target));
                emit32(code, 0x14000000); // b <offset> — patched later
            }
        }
        OpCode::Branch => {
            if let (Some(Operand::Value(cond)), Some(Operand::Block(then_bb)), Some(Operand::Block(else_bb))) =
                (instr.operands.get(0), instr.operands.get(1), instr.operands.get(2))
            {
                let cr = reg_for(*cond, alloc);
                // cbnz Xcr, then_bb
                fixups.push((code.len(), *then_bb));
                emit32(code, 0xb5000000 | (cr as u32)); // cbnz — patched
                // b else_bb
                fixups.push((code.len(), *else_bb));
                emit32(code, 0x14000000); // b — patched
            }
        }
        OpCode::Call => {
            // Move args to x0, x1, ...
            let mut arg_idx = 0u8;
            for op in instr.operands.iter().skip(1) {
                if let Operand::Value(v) = op {
                    let src = reg_for(*v, alloc);
                    if src != arg_idx {
                        emit32(code, 0xaa0003e0 | (arg_idx as u32) | ((src as u32) << 16));
                    }
                    arg_idx += 1;
                }
            }
            // bl <offset> — needs fixup to function entry
            emit32(code, 0x94000000); // bl — simplified
            if dst != 0 {
                emit32(code, 0xaa0003e0 | (dst as u32)); // mov Xd, x0
            }
        }
        OpCode::Return => {
            if let Some(Operand::Value(v)) = instr.operands.first() {
                let src = reg_for(*v, alloc);
                if src != 0 {
                    // mov x0, Xsrc
                    emit32(code, 0xaa0003e0 | ((src as u32) << 16));
                }
            }
            // epilogue handles ret
        }
        OpCode::Phi => {
            // Phi is resolved during SSA destruction (not emitted directly)
        }
        OpCode::Switch => {
            // Lowered as chain of cbnz/b pairs
            if let Some(Operand::Value(v)) = instr.operands.first() {
                let vr = reg_for(*v, alloc);
                for op in instr.operands.iter().skip(1) {
                    if let Operand::SwitchCase(val, target) = op {
                        emit_mov_imm(code, 17, *val); // x17 = case value
                        // cmp Xv, x17
                        emit32(code, 0xeb1100 << 8 | (vr as u32) << 5 | 0x1f);
                        // b.eq target
                        fixups.push((code.len(), *target));
                        emit32(code, 0x54000000); // b.eq — patched
                    }
                    if let Operand::Block(default) = op {
                        fixups.push((code.len(), *default));
                        emit32(code, 0x14000000); // b default
                    }
                }
            }
        }

        // ── Proof (compile-time only, no codegen) ──
        OpCode::Assert | OpCode::Assume | OpCode::Invariant
        | OpCode::LifetimeStart | OpCode::LifetimeEnd => {}
    }
}

// ── Encoding helpers ──

fn emit32(code: &mut Vec<u8>, instr: u32) {
    code.extend_from_slice(&instr.to_le_bytes());
}

fn encode_rrr(base: u32, rd: u8, rn: u8, rm: u8) -> u32 {
    base | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32)
}

fn encode_mul(rd: u8, rn: u8, rm: u8) -> u32 {
    // madd Xd, Xn, Xm, xzr
    0x9b007c00 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32)
}

fn encode_div(rd: u8, rn: u8, rm: u8) -> u32 {
    // sdiv Xd, Xn, Xm
    0x9ac00c00 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32)
}

fn encode_msub(rd: u8, rn: u8, rm: u8, ra: u8) -> u32 {
    // msub Xd, Xn, Xm, Xa
    0x9b008000 | ((rm as u32) << 16) | ((ra as u32) << 10) | ((rn as u32) << 5) | (rd as u32)
}

fn emit_mov_imm(code: &mut Vec<u8>, reg: u8, val: i64) {
    let v = val as u64;
    // movz Xreg, #imm16
    emit32(code, 0xd2800000 | (((v & 0xFFFF) as u32) << 5) | (reg as u32));
    // movk for upper bits if needed
    if v > 0xFFFF {
        emit32(code, 0xf2a00000 | ((((v >> 16) & 0xFFFF) as u32) << 5) | (reg as u32));
    }
    if v > 0xFFFFFFFF {
        emit32(code, 0xf2c00000 | ((((v >> 32) & 0xFFFF) as u32) << 5) | (reg as u32));
    }
    if v > 0xFFFFFFFFFFFF {
        emit32(code, 0xf2e00000 | ((((v >> 48) & 0xFFFF) as u32) << 5) | (reg as u32));
    }
}

fn patch_branch(code: &mut Vec<u8>, offset: usize, rel: i32) {
    if offset + 4 <= code.len() {
        let existing = u32::from_le_bytes([code[offset], code[offset+1], code[offset+2], code[offset+3]]);
        let masked = existing & 0xFC000000; // keep opcode
        let imm26 = (rel as u32) & 0x03FFFFFF;
        let patched = masked | imm26;
        code[offset..offset+4].copy_from_slice(&patched.to_le_bytes());
    }
}

fn reg_for(v: ValueId, alloc: &FuncAlloc) -> u8 {
    match alloc.locations.get(&v) {
        Some(Location::Reg(r)) => r.0,
        Some(Location::Stack(_)) => 16, // spill to x16 temp
        None => 0, // fallback to x0
    }
}

fn one_reg(operands: &[Operand], alloc: &FuncAlloc) -> u8 {
    match operands.first() {
        Some(Operand::Value(v)) => reg_for(*v, alloc),
        _ => 0,
    }
}

fn two_regs(operands: &[Operand], alloc: &FuncAlloc) -> (u8, u8) {
    let r1 = match operands.get(0) {
        Some(Operand::Value(v)) => reg_for(*v, alloc),
        _ => 0,
    };
    let r2 = match operands.get(1) {
        Some(Operand::Value(v)) => reg_for(*v, alloc),
        _ => 0,
    };
    (r1, r2)
}

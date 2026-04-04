//! ARM64 (AArch64) Machine Code Emitter — primary target.
//!
//! Generates raw ARM64 instructions (4 bytes each, fixed width).
//! Supports: arithmetic, comparisons (cmp+cset), branches, function calls,
//! memory load/store, stack spill/reload.

use std::collections::HashMap;
use crate::ir::{IrModule, IrFunction, OpCode, Operand, ValueId, IrType};
use crate::ir::instr::CmpKind;
use super::regalloc::{AllocResult, FuncAlloc, Location, PhysReg};

const FP: u8 = 29;
const LR: u8 = 30;
const SP: u8 = 31;
const TMP1: u8 = 16; // x16 scratch
const TMP2: u8 = 17; // x17 scratch

/// Emit ARM64 machine code for the entire module.
/// Returns (code, function_offsets) — offsets keyed by function name.
pub fn emit(module: &IrModule, alloc: &AllocResult) -> Vec<u8> {
    let mut code = Vec::new();
    let mut func_offsets: HashMap<String, usize> = HashMap::new();
    let mut call_fixups: Vec<(usize, String)> = Vec::new(); // (offset_in_code, target_func_name)

    // Emit all functions
    for func in &module.functions {
        if let Some(func_alloc) = alloc.func_allocs.get(&func.name) {
            func_offsets.insert(func.name.clone(), code.len());
            emit_function(&mut code, func, func_alloc, &mut call_fixups);
        }
    }

    // Resolve cross-function call fixups
    for (offset, target_name) in &call_fixups {
        if let Some(&target_offset) = func_offsets.get(target_name) {
            let rel = (target_offset as i64 - *offset as i64) / 4;
            patch_branch(&mut code, *offset, rel as i32);
        }
    }

    code
}

fn emit_function(
    code: &mut Vec<u8>,
    func: &IrFunction,
    alloc: &FuncAlloc,
    call_fixups: &mut Vec<(usize, String)>,
) {
    let func_start = code.len();

    // Prologue
    emit_prologue(code, alloc);

    // Label tracking for intra-function branches
    let mut block_offsets: HashMap<crate::ir::BlockId, usize> = HashMap::new();
    let mut branch_fixups: Vec<(usize, crate::ir::BlockId)> = Vec::new();

    for block in &func.blocks {
        block_offsets.insert(block.id, code.len());

        for instr in &block.instructions {
            emit_instruction(code, instr, alloc, &mut branch_fixups, call_fixups);
        }
    }

    // Resolve intra-function branch fixups
    for (offset, target_block) in &branch_fixups {
        if let Some(&target_offset) = block_offsets.get(target_block) {
            let rel = (target_offset as i64 - *offset as i64) / 4;
            patch_branch(code, *offset, rel as i32);
        }
    }

    // Epilogue (only if last block doesn't have explicit return)
    // Always emit — Return instruction will jump here
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
    branch_fixups: &mut Vec<(usize, crate::ir::BlockId)>,
    call_fixups: &mut Vec<(usize, String)>,
) {
    let dst = reg_for(instr.result, alloc);

    // Check if this Sub is actually a comparison (has CmpKind operand)
    let cmp_kind = if instr.op == OpCode::Sub {
        instr.operands.iter().find_map(|op| {
            if let Operand::Cmp(kind) = op { Some(*kind) } else { None }
        })
    } else {
        None
    };

    // Handle comparison specially
    if let Some(kind) = cmp_kind {
        let (r1, r2) = two_value_regs(&instr.operands, alloc);
        // cmp Xn, Xm
        emit32(code, 0xeb000000 | ((r2 as u32) << 16) | ((r1 as u32) << 5) | 0x1f);
        // cset Xd, <cond>
        let cond = match kind {
            CmpKind::Eq => 0x0, // eq
            CmpKind::Ne => 0x1, // ne
            CmpKind::Lt => 0xb, // lt (signed)
            CmpKind::Le => 0xd, // le (signed)
            CmpKind::Gt => 0xc, // gt (signed)
            CmpKind::Ge => 0xa, // ge (signed)
        };
        // cset Xd, cond = csinc Xd, xzr, xzr, invert(cond)
        let inv_cond = cond ^ 1;
        emit32(code, 0x9a9f07e0 | ((inv_cond as u32) << 12) | (dst as u32));
        return;
    }

    match instr.op {
        // ── Arithmetic ──
        OpCode::Add => {
            match (&instr.operands.get(0), &instr.operands.get(1)) {
                (Some(Operand::Value(v1)), Some(Operand::ImmI64(imm))) if *imm >= 0 && *imm < 4096 => {
                    let r1 = reg_for(*v1, alloc);
                    // add Xd, Xn, #imm
                    emit32(code, 0x91000000 | ((*imm as u32) << 10) | ((r1 as u32) << 5) | (dst as u32));
                }
                _ => {
                    let (r1, r2) = two_regs(&instr.operands, alloc);
                    emit32(code, encode_rrr(0x8b000000, dst, r1, r2));
                }
            }
        }
        OpCode::Sub => {
            // Pure subtraction (no CmpKind — that case handled above)
            let (r1, r2) = two_regs(&instr.operands, alloc);
            emit32(code, encode_rrr(0xcb000000, dst, r1, r2));
        }
        OpCode::Mul => {
            let (r1, r2) = two_regs(&instr.operands, alloc);
            emit32(code, encode_mul(dst, r1, r2));
        }
        OpCode::Div => {
            let (r1, r2) = two_regs(&instr.operands, alloc);
            emit32(code, encode_div(dst, r1, r2));
        }
        OpCode::Mod => {
            let (r1, r2) = two_regs(&instr.operands, alloc);
            emit32(code, encode_div(TMP1, r1, r2));
            emit32(code, encode_msub(dst, TMP1, r2, r1));
        }
        OpCode::Neg => {
            let r1 = one_reg(&instr.operands, alloc);
            emit32(code, encode_rrr(0xcb000000, dst, 31, r1));
        }

        // ── Memory ──
        OpCode::Load => {
            match instr.operands.first() {
                Some(Operand::ImmI64(n)) => emit_mov_imm(code, dst, *n),
                Some(Operand::ImmF64(f)) => emit_mov_imm(code, dst, f.to_bits() as i64),
                Some(Operand::ImmBool(b)) => emit_mov_imm(code, dst, if *b { 1 } else { 0 }),
                Some(Operand::Value(ptr)) => {
                    let base = reg_for(*ptr, alloc);
                    // ldr Xd, [Xn]
                    emit32(code, 0xf9400000 | ((base as u32) << 5) | (dst as u32));
                }
                Some(Operand::StringRef(_idx)) => {
                    // String literal — emit pointer to data section (placeholder)
                    emit_mov_imm(code, dst, 0);
                }
                _ => emit_mov_imm(code, dst, 0),
            }
            // Handle field index access: Load(obj, field_idx) or Load(arr, idx)
            if instr.operands.len() >= 2 {
                if let Some(Operand::FieldIdx(idx)) = instr.operands.get(1) {
                    let base = one_reg(&instr.operands, alloc);
                    let offset = (*idx as i64) * 8;
                    // ldr Xd, [Xbase, #offset]
                    if offset < 32760 && offset % 8 == 0 {
                        let imm12 = (offset / 8) as u32;
                        emit32(code, 0xf9400000 | (imm12 << 10) | ((base as u32) << 5) | (dst as u32));
                    } else {
                        emit_mov_imm(code, TMP2, offset);
                        // ldr Xd, [Xbase, x17]
                        emit32(code, 0xf8600000 | ((TMP2 as u32) << 16) | (0x6 << 13) | ((base as u32) << 5) | (dst as u32));
                    }
                }
            }
        }
        OpCode::Store => {
            let ops = &instr.operands;
            if ops.len() >= 2 {
                let base = val_reg(&ops[0], alloc);
                let src = val_reg(&ops[1], alloc);
                if ops.len() >= 3 {
                    if let Operand::FieldIdx(idx) = &ops[1] {
                        // struct field store: Store(obj, field_idx, val)
                        let val = val_reg(&ops[2], alloc);
                        let offset = (*idx as i64) * 8;
                        if offset < 32760 && offset % 8 == 0 {
                            let imm12 = (offset / 8) as u32;
                            emit32(code, 0xf9000000 | (imm12 << 10) | ((base as u32) << 5) | (val as u32));
                        }
                    } else {
                        // array store: Store(arr, idx, val) — idx is a Value
                        let idx_reg = val_reg(&ops[1], alloc);
                        let val_r = val_reg(&ops[2], alloc);
                        // str Xval, [Xarr, Xidx, lsl #3]
                        emit32(code, 0xf8200000 | ((idx_reg as u32) << 16) | (0x7 << 13) | ((base as u32) << 5) | (val_r as u32));
                    }
                } else {
                    // simple store: str Xsrc, [Xbase]
                    emit32(code, 0xf9000000 | ((base as u32) << 5) | (src as u32));
                }
            }
        }
        OpCode::Alloc => {
            let size = instr.ty.size_bytes().max(8);
            // Use frame pointer relative allocation for small structs
            // For now use mmap syscall
            emit_mov_imm(code, 0, 0);
            emit_mov_imm(code, 1, size as i64);
            emit_mov_imm(code, 2, 3);        // PROT_READ|PROT_WRITE
            emit_mov_imm(code, 3, 0x1002);   // MAP_ANON|MAP_PRIVATE
            emit_mov_imm(code, 4, -1);
            emit_mov_imm(code, 5, 0);
            emit_mov_imm(code, TMP1, 197);   // SYS_mmap
            emit32(code, 0xd4000001);        // svc #0
            if dst != 0 {
                emit32(code, encode_mov(dst, 0));
            }
        }
        OpCode::Free => {
            if let Some(Operand::Value(ptr)) = instr.operands.first() {
                let r = reg_for(*ptr, alloc);
                if r != 0 { emit32(code, encode_mov(0, r)); }
                emit_mov_imm(code, 1, 4096);
                emit_mov_imm(code, TMP1, 73); // SYS_munmap
                emit32(code, 0xd4000001);
            }
        }
        OpCode::Copy | OpCode::Move | OpCode::OwnershipTransfer => {
            let r1 = one_reg(&instr.operands, alloc);
            if dst != r1 {
                emit32(code, encode_mov(dst, r1));
            }
        }

        // ── Control ──
        OpCode::Jump => {
            if let Some(Operand::Block(target)) = instr.operands.first() {
                branch_fixups.push((code.len(), *target));
                emit32(code, 0x14000000); // b <offset>
            }
        }
        OpCode::Branch => {
            if let (Some(Operand::Value(cond)), Some(Operand::Block(then_bb)), Some(Operand::Block(else_bb))) =
                (instr.operands.get(0), instr.operands.get(1), instr.operands.get(2))
            {
                let cr = reg_for(*cond, alloc);
                // cbnz Xcr, then_bb
                branch_fixups.push((code.len(), *then_bb));
                emit32(code, 0xb5000000 | (cr as u32));
                // b else_bb
                branch_fixups.push((code.len(), *else_bb));
                emit32(code, 0x14000000);
            }
        }
        OpCode::Call => {
            // Move args to x0, x1, ...
            let mut arg_idx = 0u8;
            let mut target_func_name: Option<String> = None;

            for (i, op) in instr.operands.iter().enumerate() {
                if i == 0 {
                    // First operand is function reference
                    if let Operand::Func(fid) = op {
                        // Find function name by ID
                        // We don't have module here, so use func_id as offset hint
                        target_func_name = Some(format!("__func_{}", fid.0));
                    }
                    continue;
                }
                if let Operand::Value(v) = op {
                    let src = reg_for(*v, alloc);
                    if src != arg_idx {
                        emit32(code, encode_mov(arg_idx, src));
                    }
                    arg_idx += 1;
                }
            }

            // bl <target> — will be patched
            if let Some(name) = target_func_name {
                call_fixups.push((code.len(), name));
            }
            emit32(code, 0x94000000); // bl <offset>

            // Move result from x0 to dst
            if dst != 0 {
                emit32(code, encode_mov(dst, 0));
            }
        }
        OpCode::Return => {
            if let Some(Operand::Value(v)) = instr.operands.first() {
                let src = reg_for(*v, alloc);
                if src != 0 {
                    emit32(code, encode_mov(0, src));
                }
            }
            // Jump to epilogue — for now, just emit epilogue inline
            // (simpler than calculating offset to function end)
        }
        OpCode::Phi => {
            // Phi resolved during SSA destruction or lowering — no codegen
            // If phi still exists, treat as copy from first operand
            if let Some(Operand::PhiEntry(_, v)) = instr.operands.first() {
                let src = reg_for(*v, alloc);
                if dst != src {
                    emit32(code, encode_mov(dst, src));
                }
            }
        }
        OpCode::Switch => {
            if let Some(Operand::Value(v)) = instr.operands.first() {
                let vr = reg_for(*v, alloc);
                for op in instr.operands.iter().skip(1) {
                    match op {
                        Operand::SwitchCase(val, target) => {
                            emit_mov_imm(code, TMP2, *val);
                            // cmp Xv, x17
                            emit32(code, 0xeb000000 | ((TMP2 as u32) << 16) | ((vr as u32) << 5) | 0x1f);
                            // b.eq target
                            branch_fixups.push((code.len(), *target));
                            emit32(code, 0x54000000); // b.eq
                        }
                        Operand::Block(default) => {
                            branch_fixups.push((code.len(), *default));
                            emit32(code, 0x14000000);
                        }
                        _ => {}
                    }
                }
            }
        }

        // Proof: no codegen
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
    0x9b007c00 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32)
}

fn encode_div(rd: u8, rn: u8, rm: u8) -> u32 {
    0x9ac00c00 | ((rm as u32) << 16) | ((rn as u32) << 5) | (rd as u32)
}

fn encode_msub(rd: u8, rn: u8, rm: u8, ra: u8) -> u32 {
    0x9b008000 | ((rm as u32) << 16) | ((ra as u32) << 10) | ((rn as u32) << 5) | (rd as u32)
}

/// mov Xd, Xn (ORR Xd, XZR, Xn)
fn encode_mov(rd: u8, rn: u8) -> u32 {
    0xaa0003e0 | ((rn as u32) << 16) | (rd as u32)
}

fn emit_mov_imm(code: &mut Vec<u8>, reg: u8, val: i64) {
    let v = val as u64;
    // movz Xreg, #imm16
    emit32(code, 0xd2800000 | (((v & 0xFFFF) as u32) << 5) | (reg as u32));
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
        let op_bits = existing & 0xFF000000; // preserve top byte (opcode)

        if op_bits == 0xB5000000 || op_bits == 0x54000000 {
            // cbnz / b.cond: imm19 in bits [23:5]
            let imm19 = ((rel as u32) & 0x7FFFF) << 5;
            let patched = (existing & 0xFF00001F) | imm19;
            code[offset..offset+4].copy_from_slice(&patched.to_le_bytes());
        } else {
            // b / bl: imm26 in bits [25:0]
            let masked = existing & 0xFC000000;
            let imm26 = (rel as u32) & 0x03FFFFFF;
            let patched = masked | imm26;
            code[offset..offset+4].copy_from_slice(&patched.to_le_bytes());
        }
    }
}

fn reg_for(v: ValueId, alloc: &FuncAlloc) -> u8 {
    match alloc.locations.get(&v) {
        Some(Location::Reg(r)) => r.0,
        Some(Location::Stack(_off)) => TMP1, // spilled → use temp
        None => 0,
    }
}

fn val_reg(op: &Operand, alloc: &FuncAlloc) -> u8 {
    match op {
        Operand::Value(v) => reg_for(*v, alloc),
        _ => 0,
    }
}

fn one_reg(operands: &[Operand], alloc: &FuncAlloc) -> u8 {
    for op in operands {
        if let Operand::Value(v) = op { return reg_for(*v, alloc); }
    }
    0
}

fn two_regs(operands: &[Operand], alloc: &FuncAlloc) -> (u8, u8) {
    let mut regs = [0u8; 2];
    let mut idx = 0;
    for op in operands {
        if let Operand::Value(v) = op {
            if idx < 2 { regs[idx] = reg_for(*v, alloc); idx += 1; }
        }
        if let Operand::ImmI64(_) = op {
            if idx < 2 { idx += 1; } // skip imm, handled elsewhere
        }
    }
    (regs[0], regs[1])
}

/// Extract exactly the Value operands (skip CmpKind, Block, etc.)
fn two_value_regs(operands: &[Operand], alloc: &FuncAlloc) -> (u8, u8) {
    let mut regs = [0u8; 2];
    let mut idx = 0;
    for op in operands {
        if let Operand::Value(v) = op {
            if idx < 2 { regs[idx] = reg_for(*v, alloc); idx += 1; }
        }
    }
    (regs[0], regs[1])
}

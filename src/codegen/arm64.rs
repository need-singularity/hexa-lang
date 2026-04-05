//! ARM64 (AArch64) Machine Code Emitter — primary target.
//!
//! Generates raw ARM64 instructions (4 bytes each, fixed width).
//! Supports: arithmetic, comparisons (cmp+cset), branches, function calls,
//! memory load/store, stack spill/reload.

use std::collections::HashMap;
use std::sync::Mutex;
use crate::ir::{IrModule, IrFunction, OpCode, Operand, ValueId};
use crate::ir::instr::CmpKind;
use super::regalloc::{AllocResult, FuncAlloc, Location};
use super::peephole::{run_peephole, PeepholeStats};

static LAST_PEEPHOLE_STATS: Mutex<Option<PeepholeStats>> = Mutex::new(None);

/// Fetch the stats from the most recent ARM64 emit (for diagnostics / tests).
pub fn last_peephole_stats() -> Option<PeepholeStats> {
    LAST_PEEPHOLE_STATS.lock().ok().and_then(|g| g.clone())
}

const FP: u8 = 29;
const LR: u8 = 30;
const SP: u8 = 31;
const TMP1: u8 = 16; // x16 scratch
const TMP2: u8 = 17; // x17 scratch

/// Emit ARM64 machine code for the entire module.
/// Returns (code, main_offset) — main_offset is the byte offset of the `main` function.
pub fn emit_with_main_offset(module: &IrModule, alloc: &AllocResult) -> (Vec<u8>, usize) {
    let mut code = Vec::new();
    let mut func_offsets: HashMap<String, usize> = HashMap::new();
    let mut call_fixups: Vec<(usize, String)> = Vec::new();

    // Build FuncId → name map
    let id_to_name: HashMap<u32, String> = module.functions.iter()
        .map(|f| (f.id.0, f.name.clone()))
        .collect();

    // Emit all functions (main last for entry point)
    let mut funcs_ordered: Vec<&IrFunction> = module.functions.iter()
        .filter(|f| f.name != "main")
        .collect();
    if let Some(main_fn) = module.functions.iter().find(|f| f.name == "main") {
        funcs_ordered.push(main_fn);
    }

    for func in &funcs_ordered {
        if let Some(func_alloc) = alloc.func_allocs.get(&func.name) {
            func_offsets.insert(func.name.clone(), code.len());
            emit_function(&mut code, func, func_alloc, &mut call_fixups, &id_to_name);
        }
    }

    // Resolve cross-function call fixups
    for (offset, target_name) in &call_fixups {
        if let Some(&target_offset) = func_offsets.get(target_name) {
            let rel = (target_offset as i64 - *offset as i64) / 4;
            patch_branch(&mut code, *offset, rel as i32);
        }
    }

    let main_offset = func_offsets.get("main").copied().unwrap_or(0);

    // Post-emit peephole pass (length-preserving; safe w.r.t. branch fixups).
    // Fixed-point with ≤6 rounds; runs 6 rules (n=6 alignment).
    let peephole_enabled = std::env::var("HEXA_NO_PEEPHOLE").is_err();
    let stats = if peephole_enabled { run_peephole(&mut code) } else { PeepholeStats::default() };
    if let Ok(mut slot) = LAST_PEEPHOLE_STATS.lock() { *slot = Some(stats); }

    (code, main_offset)
}

/// Convenience wrapper that returns just the code (for tests).
pub fn emit(module: &IrModule, alloc: &AllocResult) -> Vec<u8> {
    emit_with_main_offset(module, alloc).0
}

fn emit_function(
    code: &mut Vec<u8>,
    func: &IrFunction,
    alloc: &FuncAlloc,
    call_fixups: &mut Vec<(usize, String)>,
    id_to_name: &HashMap<u32, String>,
) {
    let _func_start = code.len();

    // Pre-scan: count Alloc instructions to assign stack slots for local variables
    let mut alloca_slots: HashMap<ValueId, i32> = HashMap::new();
    let mut next_slot_offset: i32 = -(alloc.stack_size + 16); // below saved FP/LR
    for block in &func.blocks {
        for instr in &block.instructions {
            if instr.op == OpCode::Alloc {
                next_slot_offset -= 8; // each alloca gets 8 bytes
                alloca_slots.insert(instr.result, next_slot_offset);
            }
        }
    }

    // Adjust stack size to include alloca slots
    let total_stack = alloc.stack_size + (alloca_slots.len() as i32) * 8;
    let adjusted_alloc = FuncAlloc {
        locations: alloc.locations.clone(),
        stack_size: (total_stack + 15) & !15, // 16-byte aligned
        used_regs: alloc.used_regs.clone(),
        callee_saved: alloc.callee_saved.clone(),
    };

    // Prologue with adjusted stack
    emit_prologue(code, &adjusted_alloc);

    // Parameter stores are now emitted explicitly in IR via Load(Param(i)) + Store.
    // P2 mem2reg promotes same-block Store→Load to Copy, eliminating stack round-trips.
    // Unpromoted params are stored via the IR Store instruction in emit_instruction.

    let _const_pins: HashMap<i64, u8> = HashMap::new(); // disabled for now

    // Collect phi moves: for each block that has successors with phi nodes,
    // insert mov instructions before the terminator.
    // phi_moves[block_id] = vec![(phi_dst_reg, src_reg)]
    let mut phi_moves: HashMap<crate::ir::BlockId, Vec<(u8, u8)>> = HashMap::new();
    for block in &func.blocks {
        for instr in &block.instructions {
            if instr.op == OpCode::Phi {
                let phi_dst = reg_for(instr.result, &adjusted_alloc);
                for op in &instr.operands {
                    if let Operand::PhiEntry(pred_block, val) = op {
                        let src = reg_for(*val, &adjusted_alloc);
                        phi_moves.entry(*pred_block).or_default().push((phi_dst, src));
                    }
                }
            }
        }
    }

    let mut block_offsets: HashMap<crate::ir::BlockId, usize> = HashMap::new();
    let mut branch_fixups: Vec<(usize, crate::ir::BlockId)> = Vec::new();

    for block in &func.blocks {
        block_offsets.insert(block.id, code.len());

        for instr in &block.instructions {
            // Insert phi moves before unconditional Jump using parallel copy protocol
            if instr.op == OpCode::Jump {
                if let Some(moves) = phi_moves.get(&block.id) {
                    // Parallel copy: first save all src values to temps, then assign
                    // This prevents clobbering when dst of one move = src of another
                    let need_temp = moves.iter().any(|&(dst, _)| {
                        moves.iter().any(|&(_, src)| src == dst)
                    });

                    if need_temp && !moves.is_empty() {
                        // Save all src values to stack
                        let n = moves.len();
                        let scratch = ((n * 8 + 15) & !15) as u32;
                        emit32(code, 0xd10003ff | (scratch << 10)); // sub sp, sp, #scratch
                        for (i, &(_, src)) in moves.iter().enumerate() {
                            let off = (i * 8) as u32;
                            emit32(code, 0xf9000000 | ((off / 8) << 10) | (31u32 << 5) | (src as u32));
                        }
                        // Load from stack to dst
                        for (i, &(dst, _)) in moves.iter().enumerate() {
                            let off = (i * 8) as u32;
                            emit32(code, 0xf9400000 | ((off / 8) << 10) | (31u32 << 5) | (dst as u32));
                        }
                        emit32(code, 0x910003ff | (scratch << 10)); // add sp, sp, #scratch
                    } else {
                        for &(phi_dst, src) in moves {
                            if phi_dst != src {
                                emit32(code, encode_mov(phi_dst, src));
                            }
                        }
                    }
                }
            }
            emit_instruction(code, instr, &adjusted_alloc, &mut branch_fixups, call_fixups, id_to_name, &alloca_slots);
        }
    }

    // Resolve intra-function branch fixups
    for (offset, target_block) in &branch_fixups {
        if let Some(&target_offset) = block_offsets.get(target_block) {
            let rel = (target_offset as i64 - *offset as i64) / 4;
            patch_branch(code, *offset, rel as i32);
        }
    }

    emit_epilogue(code, &adjusted_alloc);
}

fn emit_prologue(code: &mut Vec<u8>, alloc: &FuncAlloc) {
    // stp x29, x30, [sp, #-16]!
    emit32(code, 0xa9bf7bfd);
    // mov x29, sp
    emit32(code, 0x910003fd);

    // Save callee-saved registers (x19-x28) in pairs via stp
    let cs = &alloc.callee_saved;
    let mut i = 0;
    while i + 1 < cs.len() {
        let rt = cs[i].0 as u32;
        let rt2 = cs[i + 1].0 as u32;
        // stp Xt1, Xt2, [sp, #-16]!
        emit32(code, 0xa9bf0000 | (rt2 << 10) | ((SP as u32) << 5) | rt);
        i += 2;
    }
    if i < cs.len() {
        let rt = cs[i].0 as u32;
        // str Xrt, [sp, #-16]!
        emit32(code, 0xf81f0fe0 | rt);
    }

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

    // Restore callee-saved registers in reverse order
    let cs = &alloc.callee_saved;
    let n = cs.len();
    let mut i = if n % 2 == 1 { n - 1 } else { n };
    if n % 2 == 1 {
        let rt = cs[n - 1].0 as u32;
        // ldr Xrt, [sp], #16
        emit32(code, 0xf8410fe0 | rt);
    }
    while i >= 2 {
        i -= 2;
        let rt = cs[i].0 as u32;
        let rt2 = cs[i + 1].0 as u32;
        // ldp Xt1, Xt2, [sp], #16
        emit32(code, 0xa8c10000 | (rt2 << 10) | ((SP as u32) << 5) | rt);
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
    id_to_name: &HashMap<u32, String>,
    alloca_slots: &HashMap<ValueId, i32>,
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
            let (r1, r2) = two_regs(&instr.operands, alloc);
            emit32(code, encode_rrr(0x8b000000, dst, r1, r2));
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
                Some(Operand::Param(i)) => {
                    // Materialize incoming argument register xi → dst
                    let src = *i as u8;
                    if dst != src {
                        emit32(code, encode_mov(dst, src));
                    }
                }
                Some(Operand::ImmI64(n)) => emit_mov_imm(code, dst, *n),
                Some(Operand::ImmF64(f)) => emit_mov_imm(code, dst, f.to_bits() as i64),
                Some(Operand::ImmBool(b)) => emit_mov_imm(code, dst, if *b { 1 } else { 0 }),
                Some(Operand::Value(ptr)) => {
                    if let Some(&slot_off) = alloca_slots.get(ptr) {
                        let simm9 = (slot_off as u32) & 0x1FF;
                        emit32(code, 0xf8400000 | (simm9 << 12) | ((FP as u32) << 5) | (dst as u32));
                    } else {
                        let base = reg_for(*ptr, alloc);
                        // Peephole: if base == dst, no need to load (value already there)
                        if base != dst {
                            emit32(code, 0xf9400000 | ((base as u32) << 5) | (dst as u32));
                        }
                    }
                }
                Some(Operand::StringRef(_)) => emit_mov_imm(code, dst, 0),
                _ => emit_mov_imm(code, dst, 0),
            }
            // Field index access (struct fields, arrays)
            if instr.operands.len() >= 2 {
                if let Some(Operand::FieldIdx(idx)) = instr.operands.get(1) {
                    let base = one_reg(&instr.operands, alloc);
                    let offset = (*idx as i64) * 8;
                    if offset < 32760 && offset % 8 == 0 {
                        let imm12 = (offset / 8) as u32;
                        emit32(code, 0xf9400000 | (imm12 << 10) | ((base as u32) << 5) | (dst as u32));
                    }
                }
            }
        }
        OpCode::Store => {
            let ops = &instr.operands;
            if ops.len() >= 2 {
                let ptr_op = &ops[0];
                let val_r = val_reg(&ops[1], alloc);

                // Check if storing to an alloca slot
                if let Operand::Value(ptr_id) = ptr_op {
                    if let Some(&slot_off) = alloca_slots.get(ptr_id) {
                        // stur Xval, [x29, #-offset]
                        let simm9 = (slot_off as u32) & 0x1FF;
                        emit32(code, 0xf8000000 | (simm9 << 12) | ((FP as u32) << 5) | (val_r as u32));
                    } else {
                        let base = reg_for(*ptr_id, alloc);
                        emit32(code, 0xf9000000 | ((base as u32) << 5) | (val_r as u32));
                    }
                }
            }
        }
        OpCode::Alloc => {
            // NOP — alloca address is computed inline by Load/Store via alloca_slots.
            // No need to materialize the pointer in a register.
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
                // Peephole: if cond was produced by cmp (last emitted instruction),
                // fuse into b.<cond> instead of cbnz.
                // Check if the previous instruction is a cmp (Sub with CmpKind).
                let fused = if code.len() >= 8 {
                    // Previous 2 instructions: cmp + cset
                    // cset is 4 bytes, cmp before it is 4 bytes
                    let cset_word = u32::from_le_bytes([
                        code[code.len()-4], code[code.len()-3],
                        code[code.len()-2], code[code.len()-1],
                    ]);
                    // cset pattern: 0x9A9F07E0 | (inv_cond << 12) | rd
                    if (cset_word & 0xFFFFF000) & 0x9A9F0000 == 0x9A9F0000 {
                        // Extract the condition from cset
                        let inv_cond = (cset_word >> 12) & 0xF;
                        let cond_code = inv_cond ^ 1; // un-invert
                        // Remove cset (we'll use b.<cond> directly)
                        code.truncate(code.len() - 4);
                        // b.<cond> then_bb
                        branch_fixups.push((code.len(), *then_bb));
                        emit32(code, 0x54000000 | cond_code); // b.<cond>
                        // b else_bb
                        branch_fixups.push((code.len(), *else_bb));
                        emit32(code, 0x14000000);
                        true
                    } else { false }
                } else { false };

                if !fused {
                    let cr = reg_for(*cond, alloc);
                    branch_fixups.push((code.len(), *then_bb));
                    emit32(code, 0xb5000000 | (cr as u32)); // cbnz
                    branch_fixups.push((code.len(), *else_bb));
                    emit32(code, 0x14000000); // b
                }
            }
        }
        OpCode::Call => {
            // Collect args and function target
            let mut target_func_name: Option<String> = None;
            let mut indirect_callee_reg: Option<u8> = None;
            let mut arg_regs: Vec<u8> = Vec::new();

            for (i, op) in instr.operands.iter().enumerate() {
                if i == 0 {
                    match op {
                        Operand::Func(fid) => {
                            target_func_name = Some(id_to_name.get(&fid.0).cloned()
                                .unwrap_or_else(|| format!("__func_{}", fid.0)));
                        }
                        Operand::Value(v) => {
                            // Indirect call — callee is a runtime value
                            indirect_callee_reg = Some(reg_for(*v, alloc));
                        }
                        _ => {}
                    }
                    continue;
                }
                if let Operand::Value(v) = op {
                    arg_regs.push(reg_for(*v, alloc));
                }
            }

            // Move args to x0, x1, ... using temp stack to avoid clobber
            // First push all arg values to stack, then pop into x0, x1, ...
            let n_args = arg_regs.len();
            if n_args > 1 {
                // Save arg values to stack scratch area
                let scratch = ((n_args * 8 + 15) & !15) as u32;
                emit32(code, 0xd10003ff | (scratch << 10)); // sub sp, sp, #scratch
                for (i, &r) in arg_regs.iter().enumerate() {
                    let off = (i * 8) as u32;
                    emit32(code, 0xf9000000 | ((off / 8) << 10) | (31u32 << 5) | (r as u32));
                }
                // Load from scratch into x0, x1, ...
                for i in 0..n_args {
                    let off = (i * 8) as u32;
                    emit32(code, 0xf9400000 | ((off / 8) << 10) | (31u32 << 5) | (i as u32));
                }
                emit32(code, 0x910003ff | (scratch << 10)); // add sp, sp, #scratch
            } else if n_args == 1 {
                let src = arg_regs[0];
                if src != 0 {
                    emit32(code, encode_mov(0, src));
                }
            }

            if let Some(callee_r) = indirect_callee_reg {
                // Indirect call: move callee to x16 (IP0), then blr x16
                if callee_r != 16 {
                    emit32(code, encode_mov(16, callee_r)); // mov x16, Xcallee
                }
                emit32(code, 0xd63f0200); // blr x16
            } else {
                // Direct call: bl <target>
                if let Some(name) = target_func_name {
                    call_fixups.push((code.len(), name));
                }
                emit32(code, 0x94000000);
            }

            // Return value in x0. Store to a call-return alloca slot
            // at a fixed location that doesn't conflict with other allocas.
            // Use [fp, #-16] — safe because stp only uses [sp] before fp is set,
            // and our allocas start at -(stack_size + 16 + 8).
            let call_ret_off = (-16i32 as u32) & 0x1FF;
            emit32(code, 0xf8000000 | (call_ret_off << 12) | ((FP as u32) << 5) | 0); // stur x0, [fp, #-16]
            // Load it back to dst (this is safe because ldur doesn't clobber other regs)
            emit32(code, 0xf8400000 | (call_ret_off << 12) | ((FP as u32) << 5) | (dst as u32)); // ldur Xdst, [fp, #-16]
        }
        OpCode::Return => {
            if let Some(Operand::Value(v)) = instr.operands.first() {
                let src = reg_for(*v, alloc);
                if src != 0 {
                    emit32(code, encode_mov(0, src));
                }
            }
            // Inline epilogue at every return point
            emit_epilogue(code, alloc);
        }
        OpCode::Phi => {
            // Phi is handled by inserting moves in predecessors (see phi_moves below).
            // NOP at the phi instruction itself.
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

/// If operand at `idx` is a Value whose definition is an alloca Load,
/// reload from alloca slot to TMP1/TMP2 to avoid clobber issues.
/// Otherwise return the register as-is.
fn reload_if_alloca(
    operands: &[Operand],
    idx: usize,
    alloc: &FuncAlloc,
    _alloca_slots: &HashMap<ValueId, i32>,
    _code: &mut Vec<u8>,
) -> u8 {
    if let Some(Operand::Value(v)) = operands.get(idx) {
        let r = reg_for(*v, alloc);
        // If this value's source is an alloca load, reload from stack
        // We can't easily track which values came from alloca loads at codegen time,
        // so instead: if the register is caller-saved (x0-x15) and might have been
        // clobbered, use a defensive reload strategy.
        // For now, just return the register — the real fix is below.
        return r;
    }
    0
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

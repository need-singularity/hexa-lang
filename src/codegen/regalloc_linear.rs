//! Register Allocation — Linear Scan (Poletto & Sarkar 1999).
//!
//! Replaces the Chaitin-Briggs graph-coloring allocator with an O(n log n)
//! linear scan pass. Operates on SSA HEXA-IR:
//!   1. Number every instruction globally in block order.
//!   2. For each ValueId, compute a single live interval
//!      [first definition point, last use point], expanded across blocks
//!      via live_in / live_out sets from iterative dataflow.
//!   3. Sort intervals by start, walk linearly, maintaining an `active`
//!      set sorted by end. Free registers when intervals expire; when no
//!      free register is available, spill the interval whose end is
//!      farthest out (classic Poletto spill heuristic).
//!
//! The public entry point `allocate_function_linear` has the same signature
//! as the old Chaitin-Briggs `allocate_function`, so the rest of the codegen
//! pipeline is untouched.

use std::collections::{HashMap, HashSet};
use crate::ir::{IrFunction, ValueId, BlockId, Operand};
use super::regalloc::{
    FuncAlloc, Location, PhysReg, compute_liveness, is_callee_saved_pub,
};
use super::Target;

/// A single live range for a ValueId.
#[derive(Debug, Clone, Copy)]
struct LiveInterval {
    value: ValueId,
    start: u32, // global instruction index of first def
    end: u32,   // global instruction index of last use (inclusive)
    /// If Some(i), this interval is pinned to regs[i] (e.g. incoming param
    /// register). The allocator will force the assignment.
    fixed_reg: Option<usize>,
}

/// Sentinel ValueId used for synthetic param-register reservations.
/// These pre-intervals occupy an incoming argument register from function
/// entry until the last `Load Param(i)` that reads it, preventing the
/// allocator from clobbering x0..xN while params are still in flight.
const RESERVED_VID: ValueId = ValueId(u32::MAX);

/// Build global instruction numbering and per-value intervals.
///
/// Numbering: each instruction gets a unique ascending id. Block order is
/// the order stored in `func.blocks` (which is the SSA emission order —
/// stable across runs).
fn compute_intervals(func: &IrFunction) -> Vec<LiveInterval> {
    // Clone + rebuild_cfg so successor/predecessor edges are current.
    let mut fc = func.clone();
    fc.rebuild_cfg();
    let liveness = compute_liveness(&fc);

    // 1. Number instructions.
    // block_start[block_idx] = id of first instr in that block,
    // block_end[block_idx]   = id of last instr in that block.
    let mut block_start: HashMap<BlockId, u32> = HashMap::new();
    let mut block_end: HashMap<BlockId, u32> = HashMap::new();
    let mut instr_id: HashMap<ValueId, u32> = HashMap::new();

    let mut counter: u32 = 0;
    for block in &fc.blocks {
        let bid = block.id;
        block_start.insert(bid, counter);
        for instr in &block.instructions {
            instr_id.insert(instr.result, counter);
            counter += 1;
        }
        // end is last instr id (or start-1 if empty).
        let end = counter.saturating_sub(1);
        block_end.insert(bid, if block.instructions.is_empty() { counter } else { end });
    }

    // 2. For every ValueId: start = def point, end = last use.
    //    Then expand end across blocks using liveness:
    //      - if v is in live_out(B), end >= block_end(B)
    //      - if v is in live_in(B)  and def is before B, extend accordingly.
    let mut start: HashMap<ValueId, u32> = HashMap::new();
    let mut end: HashMap<ValueId, u32> = HashMap::new();

    // Collect ValueIds that are Alloc results — these are handled by the
    // backend's alloca_slots map (direct FP-relative addressing), so the
    // allocator must NOT assign them physical registers.
    let mut alloc_vids: HashSet<ValueId> = HashSet::new();
    for block in &fc.blocks {
        for instr in &block.instructions {
            if instr.op == crate::ir::opcode::OpCode::Alloc {
                alloc_vids.insert(instr.result);
            }
        }
    }

    // First pass: def sites and in-block last uses.
    for block in &fc.blocks {
        for instr in &block.instructions {
            let def_id = *instr_id.get(&instr.result).unwrap();
            start.entry(instr.result).or_insert(def_id);
            // def alone gives a degenerate [def, def] interval; refined below.
            end.entry(instr.result).and_modify(|e| *e = (*e).max(def_id))
                .or_insert(def_id);

            // Extend lifetimes of used values to this instruction id.
            for op in &instr.operands {
                match op {
                    Operand::Value(v) | Operand::PhiEntry(_, v) => {
                        if let Some(&s) = start.get(v) {
                            let _ = s;
                        } else {
                            // use before def in listing → conservative start=0
                            start.insert(*v, 0);
                        }
                        end.entry(*v)
                            .and_modify(|e| *e = (*e).max(def_id))
                            .or_insert(def_id);
                    }
                    _ => {}
                }
            }
        }
    }

    // Second pass: extend via live_in/live_out across blocks.
    for block in &fc.blocks {
        let bid = block.id;
        let bs = *block_start.get(&bid).unwrap();
        let be = *block_end.get(&bid).unwrap();

        if let Some(lo) = liveness.live_out.get(&bid) {
            for v in lo {
                // If v is live out of this block, its interval covers
                // through the end of this block.
                end.entry(*v).and_modify(|e| *e = (*e).max(be)).or_insert(be);
                // Start must be <= bs if defined before / in an earlier block.
                start.entry(*v).and_modify(|s| *s = (*s).min(bs)).or_insert(bs);
            }
        }
        if let Some(li) = liveness.live_in.get(&bid) {
            for v in li {
                // Live in → start is at most this block's start.
                start.entry(*v).and_modify(|s| *s = (*s).min(bs)).or_insert(bs);
                // and live at least through bs (covered by in-block uses or
                // the live_out propagation of predecessors).
                end.entry(*v).and_modify(|e| *e = (*e).max(bs)).or_insert(bs);
            }
        }
    }

    // 3. Collect intervals.
    //    Also record pinned-register constraints for `Load Param(i)`:
    //    incoming argument registers x0..xN must not be clobbered before
    //    their value has been read into an SSA value, so we create
    //    synthetic reserved intervals [0, pos_of_last_Load_Param_i] on
    //    register index i, and mark the Load-Param result as fixed_reg=i
    //    (so it naturally takes over the register right as it frees up).
    //
    //    We only handle Param(i) loaded in the *entry* block (blocks[0]).
    //    Any later Load Param(i) would indicate that x[i] survived across
    //    arbitrary computation, which HEXA-IR's frontend does not emit.
    let mut fixed: HashMap<ValueId, usize> = HashMap::new();
    let mut param_reserve: HashMap<usize, u32> = HashMap::new(); // reg_idx → latest pos
    if let Some(entry) = fc.blocks.first() {
        for instr in &entry.instructions {
            if instr.op == crate::ir::opcode::OpCode::Load {
                if let Some(Operand::Param(i)) = instr.operands.first() {
                    let pos = *instr_id.get(&instr.result).unwrap();
                    fixed.insert(instr.result, *i);
                    // Reservation interval for x[i]: from 0 to pos (inclusive)
                    let cur = param_reserve.get(i).copied().unwrap_or(0);
                    param_reserve.insert(*i, cur.max(pos));
                }
            }
        }
    }

    let mut intervals: Vec<LiveInterval> = Vec::with_capacity(start.len() + param_reserve.len());
    for (v, s) in &start {
        if alloc_vids.contains(v) {
            // Skip allocas — the backend addresses them via alloca_slots.
            continue;
        }
        let e = end.get(v).copied().unwrap_or(*s);
        let fr = fixed.get(v).copied();
        intervals.push(LiveInterval { value: *v, start: *s, end: e, fixed_reg: fr });
    }
    // Synthetic param-register reservations.
    for (reg_i, last_pos) in &param_reserve {
        intervals.push(LiveInterval {
            value: RESERVED_VID,
            start: 0,
            // end = last Load-Param position - 1, so that the reservation
            // expires exactly when the Load-Param instruction is processed,
            // leaving the register free for the Load-Param result (which is
            // fixed_reg to this register).
            end: last_pos.saturating_sub(1),
            fixed_reg: Some(*reg_i),
        });
    }
    intervals
}

/// Linear-scan register allocation for a single function.
pub fn allocate_function_linear(func: &IrFunction, regs: &[PhysReg]) -> FuncAlloc {
    if func.blocks.is_empty() {
        return FuncAlloc {
            locations: HashMap::new(),
            stack_size: 0,
            used_regs: Vec::new(),
            callee_saved: Vec::new(),
        };
    }

    let k = regs.len();
    let mut intervals = compute_intervals(func);
    // Sort by start, then by end for determinism. Reservation sentinels
    // (value == RESERVED_VID) sort AFTER real intervals at equal start,
    // which is fine here because we peel them off into a pre-pass below.
    intervals.sort_by_key(|i| (i.start, i.end, i.value.0));

    // Active list: kept sorted by increasing `end`.
    // (register_index into regs, interval)
    let mut active: Vec<(usize, LiveInterval)> = Vec::with_capacity(k);
    // free_regs: stack of register indices currently unassigned.
    let mut free_regs: Vec<usize> = (0..k).rev().collect();

    let mut locations: HashMap<ValueId, Location> = HashMap::new();
    let mut used_regs_set: HashSet<PhysReg> = HashSet::new();
    let mut stack_offset: i32 = 0;

    // Pre-pass: peel off the synthetic param-register reservations so they
    // claim their registers before any real interval is allocated.
    let mut remaining: Vec<LiveInterval> = Vec::with_capacity(intervals.len());
    for iv in &intervals {
        if iv.value == RESERVED_VID {
            let want = iv.fixed_reg.unwrap();
            if let Some(fp) = free_regs.iter().position(|&r| r == want) {
                free_regs.remove(fp);
            }
            insert_active(&mut active, (want, *iv));
        } else {
            remaining.push(*iv);
        }
    }
    let intervals = remaining;

    for iv in &intervals {
        // Expire old intervals: remove from active any whose end < iv.start.
        let i = 0;
        while i < active.len() {
            if active[i].1.end < iv.start {
                let reg_idx = active[i].0;
                free_regs.push(reg_idx);
                active.remove(i);
            } else {
                break; // active is sorted by end
            }
        }

        // Handle fixed-register intervals (incoming param registers).
        if let Some(want) = iv.fixed_reg {
            // If some other interval is currently holding regs[want], evict it.
            let pos = active.iter().position(|(r, _)| *r == want);
            if let Some(p) = pos {
                let (_, displaced) = active.remove(p);
                // Displaced interval loses its register. If it's a reserved
                // sentinel, just drop it (the reservation is now over).
                // Otherwise, spill or try to place it elsewhere.
                if displaced.value != RESERVED_VID {
                    // Give it a free register if available, else spill.
                    // Note: free_regs is a stack and doesn't yet contain `want`.
                    if let Some(other_idx) = free_regs.pop() {
                        let other_reg = regs[other_idx];
                        locations.insert(displaced.value, Location::Reg(other_reg));
                        used_regs_set.insert(other_reg);
                        insert_active(&mut active, (other_idx, displaced));
                    } else {
                        stack_offset -= 8;
                        locations.insert(displaced.value, Location::Stack(stack_offset));
                    }
                }
            } else {
                // `want` might currently be in free_regs — remove it.
                if let Some(fp) = free_regs.iter().position(|&r| r == want) {
                    free_regs.remove(fp);
                }
            }
            if iv.value != RESERVED_VID {
                let reg = regs[want];
                locations.insert(iv.value, Location::Reg(reg));
                used_regs_set.insert(reg);
            }
            insert_active(&mut active, (want, *iv));
            continue;
        }

        if active.len() == k {
            // Spill: pick the interval with the largest `end` (either the
            // last active or `iv` itself — whichever ends later).
            let last_idx = active.len() - 1;
            let (spill_reg, spill_iv) = active[last_idx];
            if spill_iv.end > iv.end {
                // Spill the incumbent → give `iv` its register.
                locations.insert(spill_iv.value, {
                    stack_offset -= 8;
                    Location::Stack(stack_offset)
                });
                let reg = regs[spill_reg];
                locations.insert(iv.value, Location::Reg(reg));
                used_regs_set.insert(reg);
                active.remove(last_idx);
                // Insert new interval keeping active sorted by end.
                insert_active(&mut active, (spill_reg, *iv));
            } else {
                // Spill the new interval.
                stack_offset -= 8;
                locations.insert(iv.value, Location::Stack(stack_offset));
            }
        } else {
            // Allocate a free register.
            let reg_idx = free_regs.pop().unwrap();
            let reg = regs[reg_idx];
            locations.insert(iv.value, Location::Reg(reg));
            used_regs_set.insert(reg);
            insert_active(&mut active, (reg_idx, *iv));
        }
    }

    // used_regs in deterministic order (by PhysReg number).
    let mut used_regs: Vec<PhysReg> = used_regs_set.into_iter().collect();
    used_regs.sort_by_key(|r| r.0);

    // Infer target from register set size (matches old allocator's heuristic).
    let target = if regs.len() > 16 { Target::Arm64 } else { Target::X86_64 };
    let callee_saved: Vec<PhysReg> = used_regs.iter()
        .filter(|r| is_callee_saved_pub(**r, target))
        .copied()
        .collect();

    // Align stack to 16 bytes.
    let stack_size = if stack_offset == 0 { 0 } else { ((-stack_offset + 15) & !15) as i32 };

    FuncAlloc {
        locations,
        stack_size,
        used_regs,
        callee_saved,
    }
}

/// Insert into `active`, keeping it sorted by interval end (ascending).
fn insert_active(active: &mut Vec<(usize, LiveInterval)>, item: (usize, LiveInterval)) {
    let pos = active.iter().position(|(_, iv)| iv.end > item.1.end)
        .unwrap_or(active.len());
    active.insert(pos, item);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType, IrBuilder};

    fn make_add_func() -> IrModule {
        let mut module = IrModule::new("test");
        let fid = module.add_function("add".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let a = b.const_i64(1);
            let bv = b.const_i64(2);
            let sum = b.add(a, bv, IrType::I64);
            b.ret(Some(sum));
        }
        module
    }

    fn make_pressure_func(n: usize) -> IrModule {
        let mut module = IrModule::new("test");
        let fid = module.add_function("pressure".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let mut vals = Vec::new();
            for i in 0..n {
                vals.push(b.const_i64(i as i64));
            }
            let mut acc = vals[0];
            for i in 1..n {
                acc = b.add(acc, vals[i], IrType::I64);
            }
            b.ret(Some(acc));
        }
        module
    }

    #[test]
    fn test_linear_basic() {
        let module = make_add_func();
        let func = module.find_function("add").unwrap();
        let regs: Vec<PhysReg> = (0..16).map(PhysReg).collect();
        let alloc = allocate_function_linear(func, &regs);
        assert!(alloc.locations.len() >= 3);
    }

    #[test]
    fn test_linear_spill_with_small_k() {
        let module = make_pressure_func(8);
        let func = module.find_function("pressure").unwrap();
        let regs: Vec<PhysReg> = (0..3).map(PhysReg).collect();
        let alloc = allocate_function_linear(func, &regs);
        let stack = alloc.locations.values()
            .filter(|l| matches!(l, Location::Stack(_)))
            .count();
        assert!(stack > 0, "pressure should force spills");
        assert!(alloc.used_regs.len() <= 3);
        assert_eq!(alloc.stack_size % 16, 0);
    }

    #[test]
    fn test_linear_no_spill_when_plenty() {
        let module = make_pressure_func(4);
        let func = module.find_function("pressure").unwrap();
        let regs: Vec<PhysReg> = (0..26).map(PhysReg).collect();
        let alloc = allocate_function_linear(func, &regs);
        let stack = alloc.locations.values()
            .filter(|l| matches!(l, Location::Stack(_)))
            .count();
        assert_eq!(stack, 0, "no spills expected with 26 regs");
    }
}

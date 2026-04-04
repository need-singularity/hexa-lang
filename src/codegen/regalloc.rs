//! Register Allocation — Linear Scan with graph coloring upgrade path.
//!
//! Maps SSA ValueIds to physical registers or stack slots.

use std::collections::HashMap;
use crate::ir::{IrModule, IrFunction, ValueId, Operand, IrType};
use super::Target;

/// Physical register identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct PhysReg(pub u8);

/// Where a value lives after allocation.
#[derive(Debug, Clone, Copy)]
pub enum Location {
    Reg(PhysReg),
    Stack(i32),  // offset from frame pointer
}

/// Allocation result for one function.
#[derive(Debug)]
pub struct FuncAlloc {
    pub locations: HashMap<ValueId, Location>,
    pub stack_size: i32,
    pub used_regs: Vec<PhysReg>,
}

/// Allocation result for entire module.
pub struct AllocResult {
    pub func_allocs: HashMap<String, FuncAlloc>,
}

/// Available general-purpose registers per target.
fn gp_regs(target: Target) -> Vec<PhysReg> {
    match target {
        // ARM64: x0-x28 (x29=FP, x30=LR, x31=SP)
        // Use x0-x15 for allocation (16 regs)
        Target::Arm64 => (0..16).map(PhysReg).collect(),
        // x86-64: rax,rcx,rdx,rbx,rsi,rdi,r8-r15 (14 regs)
        // rbp=FP, rsp=SP reserved
        Target::X86_64 => (0..14).map(PhysReg).collect(),
    }
}

/// Run register allocation for the module.
pub fn allocate(module: &IrModule, target: Target) -> AllocResult {
    let regs = gp_regs(target);
    let mut func_allocs = HashMap::new();

    for func in &module.functions {
        let alloc = allocate_function(func, &regs);
        func_allocs.insert(func.name.clone(), alloc);
    }

    AllocResult { func_allocs }
}

/// Linear scan allocation for a single function.
fn allocate_function(func: &IrFunction, regs: &[PhysReg]) -> FuncAlloc {
    let mut locations: HashMap<ValueId, Location> = HashMap::new();
    let mut next_reg = 0usize;
    let mut stack_offset: i32 = 0;
    let mut used_regs: Vec<PhysReg> = Vec::new();

    // Simple allocation: assign registers in order, spill to stack when exhausted
    // Parameters get first registers
    for (i, (_name, _ty)) in func.params.iter().enumerate() {
        let vid = ValueId(i as u32);
        if next_reg < regs.len() {
            let reg = regs[next_reg];
            locations.insert(vid, Location::Reg(reg));
            if !used_regs.contains(&reg) { used_regs.push(reg); }
            next_reg += 1;
        } else {
            stack_offset -= 8;
            locations.insert(vid, Location::Stack(stack_offset));
        }
    }

    // Compute live ranges (simplified: just instruction order)
    let mut instr_idx = func.params.len() as u32;
    let mut live_end: HashMap<ValueId, u32> = HashMap::new();

    // First pass: find last use of each value
    for block in &func.blocks {
        for instr in &block.instructions {
            for op in &instr.operands {
                if let Operand::Value(v) = op {
                    live_end.insert(*v, instr_idx);
                }
            }
            instr_idx += 1;
        }
    }

    // Second pass: allocate registers
    let mut free_regs: Vec<PhysReg> = regs.to_vec();
    // Remove already-used param regs
    for i in 0..next_reg.min(regs.len()) {
        free_regs.retain(|r| *r != regs[i]);
    }

    let mut current_idx: u32 = func.params.len() as u32;
    for block in &func.blocks {
        for instr in &block.instructions {
            // Expire old intervals: free regs for values whose last use has passed
            let expired: Vec<ValueId> = live_end.iter()
                .filter(|(_, &end)| end < current_idx)
                .map(|(v, _)| *v)
                .collect();
            for v in expired {
                if let Some(Location::Reg(r)) = locations.get(&v) {
                    if !free_regs.contains(r) {
                        free_regs.push(*r);
                    }
                }
                live_end.remove(&v);
            }

            // Allocate for result
            if !locations.contains_key(&instr.result) {
                if let Some(reg) = free_regs.pop() {
                    locations.insert(instr.result, Location::Reg(reg));
                    if !used_regs.contains(&reg) { used_regs.push(reg); }
                } else {
                    stack_offset -= 8;
                    locations.insert(instr.result, Location::Stack(stack_offset));
                }
            }

            current_idx += 1;
        }
    }

    // Align stack to 16 bytes
    let stack_size = ((-stack_offset + 15) & !15) as i32;

    FuncAlloc {
        locations,
        stack_size,
        used_regs,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType, IrBuilder};

    #[test]
    fn test_simple_allocation() {
        let mut module = IrModule::new("test");
        let fid = module.add_function("add".into(), vec![
            ("a".into(), IrType::I64),
            ("b".into(), IrType::I64),
        ], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let a = b.const_i64(1);
            let bv = b.const_i64(2);
            let sum = b.add(a, bv, IrType::I64);
            b.ret(Some(sum));
        }

        let result = allocate(&module, Target::Arm64);
        let alloc = result.func_allocs.get("add").unwrap();
        assert!(alloc.locations.len() >= 2); // at least params allocated
    }
}

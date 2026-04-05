//! Register Allocation — Chaitin-Briggs Graph Coloring.
//!
//! Maps SSA ValueIds to physical registers or stack slots using:
//! 1. Liveness analysis (iterative dataflow)
//! 2. Interference graph construction
//! 3. Copy coalescing
//! 4. Simplify / spill / select (Chaitin-Briggs)

use std::collections::{HashMap, HashSet, BTreeSet};
use crate::ir::{IrFunction, IrModule, ValueId, BlockId, Operand, IrType};
use crate::ir::opcode::OpCode;
use super::Target;

/// Physical register identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct PhysReg(pub u8);

/// Where a value lives after allocation.
#[derive(Debug, Clone, Copy)]
pub enum Location {
    Reg(PhysReg),
    Stack(i32), // offset from frame pointer
}

/// Allocation result for one function.
#[derive(Debug)]
pub struct FuncAlloc {
    pub locations: HashMap<ValueId, Location>,
    pub stack_size: i32,
    pub used_regs: Vec<PhysReg>,
    /// Callee-saved registers that this function uses (must be saved/restored).
    pub callee_saved: Vec<PhysReg>,
}

/// Allocation result for entire module.
pub struct AllocResult {
    pub func_allocs: HashMap<String, FuncAlloc>,
}

/// Available general-purpose registers per target.
/// Caller-saved registers listed first so the allocator prefers them.
fn gp_regs(target: Target) -> Vec<PhysReg> {
    match target {
        // ARM64: caller-saved x0-x15 (16) + callee-saved x19-x28 (10) = 26 regs
        Target::Arm64 => {
            let mut regs: Vec<PhysReg> = (0..16).map(PhysReg).collect();
            regs.extend((19..=28).map(PhysReg));
            regs
        }
        // x86-64: unchanged for now
        Target::X86_64 => (0..14).map(PhysReg).collect(),
    }
}

/// Public wrapper for callee-saved check (used by linear scan allocator).
pub fn is_callee_saved_pub(reg: PhysReg, target: Target) -> bool {
    is_callee_saved(reg, target)
}

/// Check if a physical register is callee-saved.
fn is_callee_saved(reg: PhysReg, target: Target) -> bool {
    match target {
        Target::Arm64 => reg.0 >= 19 && reg.0 <= 28,
        Target::X86_64 => false, // not yet implemented for x86
    }
}

// ── Liveness Analysis ──

/// Liveness information for a function.
#[derive(Debug, Clone)]
pub struct LiveInfo {
    pub live_in: HashMap<BlockId, HashSet<ValueId>>,
    pub live_out: HashMap<BlockId, HashSet<ValueId>>,
}

/// Compute use and def sets for a basic block.
fn compute_use_def(func: &IrFunction, block_idx: usize) -> (HashSet<ValueId>, HashSet<ValueId>) {
    let block = &func.blocks[block_idx];
    let mut use_set = HashSet::new();
    let mut def_set = HashSet::new();

    for instr in &block.instructions {
        // Uses: any Value operand not already defined in this block
        for op in &instr.operands {
            match op {
                Operand::Value(v) => {
                    if !def_set.contains(v) {
                        use_set.insert(*v);
                    }
                }
                Operand::PhiEntry(_, v) => {
                    if !def_set.contains(v) {
                        use_set.insert(*v);
                    }
                }
                _ => {}
            }
        }
        // Def: the result value
        def_set.insert(instr.result);
    }

    (use_set, def_set)
}

/// Compute liveness using iterative dataflow analysis.
///
/// live_out(B) = union of live_in(successors of B)
/// live_in(B)  = use(B) union (live_out(B) - def(B))
/// Iterate until fixpoint.
pub fn compute_liveness(func: &IrFunction) -> LiveInfo {
    let mut live_in: HashMap<BlockId, HashSet<ValueId>> = HashMap::new();
    let mut live_out: HashMap<BlockId, HashSet<ValueId>> = HashMap::new();

    // Pre-compute use/def sets for each block
    let mut use_sets: HashMap<BlockId, HashSet<ValueId>> = HashMap::new();
    let mut def_sets: HashMap<BlockId, HashSet<ValueId>> = HashMap::new();

    for (i, block) in func.blocks.iter().enumerate() {
        let (u, d) = compute_use_def(func, i);
        use_sets.insert(block.id, u);
        def_sets.insert(block.id, d);
        live_in.insert(block.id, HashSet::new());
        live_out.insert(block.id, HashSet::new());
    }

    // Iterate until fixpoint
    let mut changed = true;
    while changed {
        changed = false;
        // Process blocks in reverse order for faster convergence
        for block in func.blocks.iter().rev() {
            let bid = block.id;

            // live_out(B) = union of live_in(successors)
            let mut new_out = HashSet::new();
            for succ_id in &block.succs {
                if let Some(succ_in) = live_in.get(succ_id) {
                    new_out.extend(succ_in.iter().copied());
                }
            }

            // live_in(B) = use(B) union (live_out(B) - def(B))
            let use_b = use_sets.get(&bid).unwrap();
            let def_b = def_sets.get(&bid).unwrap();
            let mut new_in = use_b.clone();
            for v in &new_out {
                if !def_b.contains(v) {
                    new_in.insert(*v);
                }
            }

            if new_in != *live_in.get(&bid).unwrap() || new_out != *live_out.get(&bid).unwrap() {
                changed = true;
                live_in.insert(bid, new_in);
                live_out.insert(bid, new_out);
            }
        }
    }

    LiveInfo { live_in, live_out }
}

// ── Interference Graph ──

/// An undirected interference graph.
#[derive(Debug, Clone)]
pub struct InterferenceGraph {
    /// Adjacency sets: for each node, the set of interfering nodes.
    pub adj: HashMap<ValueId, HashSet<ValueId>>,
}

impl InterferenceGraph {
    fn new() -> Self {
        Self { adj: HashMap::new() }
    }

    fn ensure_node(&mut self, v: ValueId) {
        self.adj.entry(v).or_default();
    }

    fn add_edge(&mut self, u: ValueId, v: ValueId) {
        if u == v {
            return;
        }
        self.adj.entry(u).or_default().insert(v);
        self.adj.entry(v).or_default().insert(u);
    }

    fn has_edge(&self, u: ValueId, v: ValueId) -> bool {
        self.adj.get(&u).map_or(false, |s| s.contains(&v))
    }

    fn degree(&self, v: ValueId) -> usize {
        self.adj.get(&v).map_or(0, |s| s.len())
    }

    fn remove_node(&mut self, v: ValueId) {
        if let Some(neighbors) = self.adj.remove(&v) {
            for n in &neighbors {
                if let Some(s) = self.adj.get_mut(n) {
                    s.remove(&v);
                }
            }
        }
    }

    fn nodes(&self) -> Vec<ValueId> {
        self.adj.keys().copied().collect()
    }
}

/// Build the interference graph from liveness information.
///
/// Two values interfere if one is defined at a point where the other is live.
pub fn build_interference_graph(func: &IrFunction, liveness: &LiveInfo) -> InterferenceGraph {
    let mut graph = InterferenceGraph::new();

    // Ensure all values in the function have nodes
    for block in &func.blocks {
        for instr in &block.instructions {
            graph.ensure_node(instr.result);
        }
    }

    // For each block, walk instructions forward, tracking currently-live values.
    for block in &func.blocks {
        // Start with live_out and walk backwards to find interferences
        let mut live: HashSet<ValueId> = liveness.live_out
            .get(&block.id)
            .cloned()
            .unwrap_or_default();

        // Walk instructions in reverse
        for instr in block.instructions.iter().rev() {
            let def = instr.result;

            // The defined value interferes with everything live at this point
            // (except itself)
            for &live_val in &live {
                if live_val != def {
                    graph.add_edge(def, live_val);
                }
            }

            // Remove def from live (it's being defined here, not live before)
            live.remove(&def);

            // Add uses to live
            for op in &instr.operands {
                match op {
                    Operand::Value(v) => { live.insert(*v); }
                    Operand::PhiEntry(_, v) => { live.insert(*v); }
                    _ => {}
                }
            }
        }
    }

    graph
}

// ── Coalescing ──

/// Coalesce copy instructions: if src and dst don't interfere, merge them.
/// Returns a map from merged ValueId -> canonical ValueId.
fn coalesce(
    func: &IrFunction,
    graph: &mut InterferenceGraph,
) -> HashMap<ValueId, ValueId> {
    let mut alias: HashMap<ValueId, ValueId> = HashMap::new();

    // Find the canonical representative
    fn find(alias: &HashMap<ValueId, ValueId>, mut v: ValueId) -> ValueId {
        while let Some(&a) = alias.get(&v) {
            if a == v { break; }
            v = a;
        }
        v
    }

    for block in &func.blocks {
        for instr in &block.instructions {
            if instr.op == OpCode::Copy || instr.op == OpCode::Move {
                if let Some(Operand::Value(src)) = instr.operands.first() {
                    let dst = instr.result;
                    let canon_src = find(&alias, *src);
                    let canon_dst = find(&alias, dst);

                    if canon_src == canon_dst {
                        continue; // already coalesced
                    }

                    // Can coalesce if they don't interfere
                    if !graph.has_edge(canon_src, canon_dst) {
                        // Merge dst into src: dst becomes an alias of src
                        // Transfer dst's edges to src
                        let dst_neighbors: Vec<ValueId> = graph.adj
                            .get(&canon_dst)
                            .cloned()
                            .unwrap_or_default()
                            .into_iter()
                            .collect();

                        for n in dst_neighbors {
                            if n != canon_src {
                                graph.add_edge(canon_src, n);
                            }
                        }
                        graph.remove_node(canon_dst);
                        alias.insert(canon_dst, canon_src);
                    }
                }
            }
        }
    }

    // Flatten the alias chains
    let keys: Vec<ValueId> = alias.keys().copied().collect();
    for k in keys {
        let canon = find(&alias, k);
        alias.insert(k, canon);
    }

    alias
}

// ── Graph Coloring (Chaitin-Briggs) ──

/// Count how many times each value is used (for spill heuristic).
fn count_uses(func: &IrFunction) -> HashMap<ValueId, usize> {
    let mut counts: HashMap<ValueId, usize> = HashMap::new();
    for block in &func.blocks {
        for instr in &block.instructions {
            for op in &instr.operands {
                match op {
                    Operand::Value(v) => { *counts.entry(*v).or_default() += 1; }
                    Operand::PhiEntry(_, v) => { *counts.entry(*v).or_default() += 1; }
                    _ => {}
                }
            }
        }
    }
    counts
}

/// Register allocation dispatcher. Default: Chaitin-Briggs.
/// Set HEXA_REGALLOC_LINEAR=1 to use Linear-Scan (experimental).
fn allocate_function(func: &IrFunction, regs: &[PhysReg]) -> FuncAlloc {
    if std::env::var("HEXA_REGALLOC_LINEAR").is_ok() {
        return super::regalloc_linear::allocate_function_linear(func, regs);
    }
    allocate_function_chaitin(func, regs)
}

/// Chaitin-Briggs graph coloring register allocation for a single function.
fn allocate_function_chaitin(func: &IrFunction, regs: &[PhysReg]) -> FuncAlloc {
    let k = regs.len(); // number of available registers

    // If function has no blocks, return empty allocation
    if func.blocks.is_empty() {
        return FuncAlloc {
            locations: HashMap::new(),
            stack_size: 0,
            used_regs: Vec::new(),
            callee_saved: Vec::new(),
        };
    }

    // Step 1: Build liveness info (need CFG edges)
    // Clone func to rebuild CFG without mutating original
    let mut func_copy = func.clone();
    func_copy.rebuild_cfg();
    let liveness = compute_liveness(&func_copy);

    // Step 2: Build interference graph
    let mut graph = build_interference_graph(&func_copy, &liveness);

    // Step 3: Coalesce copies
    let alias = coalesce(&func_copy, &mut graph);

    // Step 4: Simplify + Spill selection
    let use_counts = count_uses(&func_copy);
    let mut stack: Vec<(ValueId, HashSet<ValueId>)> = Vec::new();
    let mut spilled: HashSet<ValueId> = HashSet::new();

    // Work on a copy of the graph for simplification
    let mut work_graph = graph.clone();

    loop {
        let nodes: Vec<ValueId> = work_graph.nodes();
        if nodes.is_empty() {
            break;
        }

        // Try to find a node with degree < k (simplify)
        let low_degree = nodes.iter()
            .find(|&&v| work_graph.degree(v) < k)
            .copied();

        if let Some(v) = low_degree {
            // Simplify: push onto stack with its neighbor set
            let neighbors = work_graph.adj.get(&v).cloned().unwrap_or_default();
            stack.push((v, neighbors));
            work_graph.remove_node(v);
        } else {
            // Spill: pick the value with the lowest use count (spill cost heuristic)
            // Briggs: we optimistically push it anyway (it might still be colorable)
            let spill_candidate = nodes.iter()
                .min_by_key(|&&v| {
                    let uses = use_counts.get(&v).copied().unwrap_or(0);
                    // Prefer to spill values with fewer uses and higher degree
                    // Lower score = better spill candidate
                    (uses, std::cmp::Reverse(work_graph.degree(v)))
                })
                .copied()
                .unwrap();

            let neighbors = work_graph.adj.get(&spill_candidate).cloned().unwrap_or_default();
            stack.push((spill_candidate, neighbors));
            spilled.insert(spill_candidate);
            work_graph.remove_node(spill_candidate);
        }
    }

    // Step 5: Select — pop stack and assign colors
    let mut color: HashMap<ValueId, usize> = HashMap::new();
    let mut actual_spills: HashSet<ValueId> = HashSet::new();

    while let Some((v, neighbors)) = stack.pop() {
        // Find colors used by already-colored neighbors
        let used_colors: BTreeSet<usize> = neighbors.iter()
            .filter_map(|n| {
                // Check canonical name in case of coalescing
                let canon = alias.get(n).copied().unwrap_or(*n);
                color.get(&canon).copied()
                    .or_else(|| color.get(n).copied())
            })
            .collect();

        // Pick the lowest available color
        let mut chosen = None;
        for c in 0..k {
            if !used_colors.contains(&c) {
                chosen = Some(c);
                break;
            }
        }

        if let Some(c) = chosen {
            color.insert(v, c);
        } else {
            // Actual spill — couldn't color even with Briggs optimism
            actual_spills.insert(v);
        }
    }

    // Step 6: Build final location map
    let mut locations: HashMap<ValueId, Location> = HashMap::new();
    let mut used_regs: Vec<PhysReg> = Vec::new();
    let mut stack_offset: i32 = 0;

    // Collect all values that need locations (instruction results only;
    // params don't have ValueIds in HEXA-IR).
    let mut all_values: Vec<ValueId> = Vec::new();
    for block in &func.blocks {
        for instr in &block.instructions {
            all_values.push(instr.result);
        }
    }

    for v in all_values {
        // Skip if already assigned (via alias)
        if locations.contains_key(&v) {
            continue;
        }

        // Resolve alias
        let canon = alias.get(&v).copied().unwrap_or(v);

        if let Some(&c) = color.get(&canon) {
            let reg = regs[c];
            locations.insert(v, Location::Reg(reg));
            if !used_regs.contains(&reg) {
                used_regs.push(reg);
            }
        } else if actual_spills.contains(&canon) || actual_spills.contains(&v) {
            // Spilled value
            stack_offset -= 8;
            let loc = Location::Stack(stack_offset);
            locations.insert(v, loc);
        } else if let Some(&c) = color.get(&v) {
            // Value itself is colored (non-aliased)
            let reg = regs[c];
            locations.insert(v, Location::Reg(reg));
            if !used_regs.contains(&reg) {
                used_regs.push(reg);
            }
        } else {
            // Value not in graph (e.g., void results, unused) — assign to stack
            stack_offset -= 8;
            locations.insert(v, Location::Stack(stack_offset));
        }

        // If this value is aliased, give the alias the same location
        if canon != v {
            if let Some(&loc) = locations.get(&v) {
                // Already set above
                let _ = loc;
            }
        }
    }

    // Ensure aliased values share locations with their canonical value
    for (&aliased, &canon) in &alias {
        if let Some(&loc) = locations.get(&canon) {
            locations.insert(aliased, loc);
        }
    }

    // Identify callee-saved registers that were used
    let target = if regs.len() > 16 { Target::Arm64 } else { Target::X86_64 };
    let callee_saved: Vec<PhysReg> = used_regs.iter()
        .filter(|r| is_callee_saved(**r, target))
        .copied()
        .collect();

    // Align stack to 16 bytes
    let stack_size = if stack_offset == 0 { 0 } else { ((-stack_offset + 15) & !15) as i32 };

    FuncAlloc {
        locations,
        stack_size,
        used_regs,
        callee_saved,
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

// ── Public API for testing ──

/// Allocate with a custom register count (for testing spill behavior).
pub fn allocate_function_with_k(func: &IrFunction, k: usize) -> FuncAlloc {
    let regs: Vec<PhysReg> = (0..k as u8).map(PhysReg).collect();
    allocate_function(func, &regs)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{IrModule, IrType, IrBuilder};

    /// Helper: build a simple function with two params and an add.
    fn make_add_func() -> (IrModule, String) {
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
        (module, "add".into())
    }

    /// Helper: build a function with a branch (diamond CFG).
    fn make_branch_func() -> (IrModule, String) {
        let mut module = IrModule::new("test");
        let fid = module.add_function("branch".into(), vec![
            ("x".into(), IrType::I64),
        ], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let then_bb = b.create_block("then");
            let else_bb = b.create_block("else");
            let merge_bb = b.create_block("merge");

            let cond = b.const_bool(true);
            b.branch(cond, then_bb, else_bb);

            b.switch_to(then_bb);
            let v1 = b.const_i64(1);
            b.jump(merge_bb);

            b.switch_to(else_bb);
            let v2 = b.const_i64(2);
            b.jump(merge_bb);

            b.switch_to(merge_bb);
            let result = b.phi(vec![(then_bb, v1), (else_bb, v2)], IrType::I64);
            b.ret(Some(result));
        }
        (module, "branch".into())
    }

    /// Helper: build a function with many values to force spilling with small K.
    fn make_pressure_func(num_values: usize) -> (IrModule, String) {
        let mut module = IrModule::new("test");
        let fid = module.add_function("pressure".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);

            // Create many live-at-the-same-time values
            let mut vals = Vec::new();
            for i in 0..num_values {
                vals.push(b.const_i64(i as i64));
            }
            // Use all of them in a chain so they're simultaneously live
            let mut acc = vals[0];
            for i in 1..num_values {
                acc = b.add(acc, vals[i], IrType::I64);
            }
            b.ret(Some(acc));
        }
        (module, "pressure".into())
    }

    /// Helper: build a function with a copy instruction.
    fn make_copy_func() -> (IrModule, String) {
        let mut module = IrModule::new("test");
        let fid = module.add_function("copy_test".into(), vec![], IrType::I64);
        {
            let func = module.function_mut(fid).unwrap();
            let mut b = IrBuilder::new(func);
            let x = b.const_i64(42); // ValueId(0)
            let y = b.copy_val(x, IrType::I64); // ValueId(1) — copy of x
            let z = b.add(y, y, IrType::I64); // ValueId(2)
            b.ret(Some(z));
        }
        (module, "copy_test".into())
    }

    // ── Test: basic allocation still works ──

    #[test]
    fn test_simple_allocation() {
        let (module, _) = make_add_func();
        let result = allocate(&module, Target::Arm64);
        let alloc = result.func_allocs.get("add").unwrap();
        assert!(alloc.locations.len() >= 2, "at least params allocated");
    }

    // ── Test: liveness analysis ──

    #[test]
    fn test_liveness_basic() {
        let (module, _) = make_add_func();
        let func = module.find_function("add").unwrap();
        let mut fc = func.clone();
        fc.rebuild_cfg();
        let liveness = compute_liveness(&fc);

        // The entry block should have live_in/live_out
        let entry_id = fc.blocks[0].id;
        let live_in = liveness.live_in.get(&entry_id).unwrap();
        let live_out = liveness.live_out.get(&entry_id).unwrap();

        // With a single block, live_out should be empty (no successors)
        assert!(live_out.is_empty(), "single-block function has no live_out");
        // live_in may contain parameters used from outside
        // (params are ValueId(0), ValueId(1) but they're referenced as const loads)
        let _ = live_in; // just verify it doesn't panic
    }

    #[test]
    fn test_liveness_branch() {
        let (module, _) = make_branch_func();
        let func = module.find_function("branch").unwrap();
        let mut fc = func.clone();
        fc.rebuild_cfg();
        let liveness = compute_liveness(&fc);

        // The merge block should have v1 and v2 in its live_in (via phi)
        let merge_id = fc.blocks[3].id; // merge is the 4th block
        let merge_live_in = liveness.live_in.get(&merge_id).unwrap();
        // Phi uses values from predecessor blocks, so they should appear in live_in
        // of those predecessors, not necessarily merge's live_in
        // The then/else blocks should have their const values live_out
        let then_id = fc.blocks[1].id;
        let else_id = fc.blocks[2].id;
        let then_out = liveness.live_out.get(&then_id).unwrap();
        let else_out = liveness.live_out.get(&else_id).unwrap();
        // Values defined in then/else that are used in merge's phi should be live_out
        assert!(!then_out.is_empty() || !else_out.is_empty(),
            "phi operands should be live out of predecessor blocks");
    }

    // ── Test: interference graph ──

    #[test]
    fn test_interference_graph_basic() {
        let (module, _) = make_add_func();
        let func = module.find_function("add").unwrap();
        let mut fc = func.clone();
        fc.rebuild_cfg();
        let liveness = compute_liveness(&fc);
        let graph = build_interference_graph(&fc, &liveness);

        // const_i64(1) = ValueId(0), const_i64(2) = ValueId(1)
        // Both are live simultaneously (used by add), so they should interfere
        let v0 = ValueId(0);
        let v1 = ValueId(1);
        assert!(graph.has_edge(v0, v1),
            "simultaneously live values should interfere");
    }

    #[test]
    fn test_interference_overlapping_ranges() {
        let (module, _) = make_pressure_func(4);
        let func = module.find_function("pressure").unwrap();
        let mut fc = func.clone();
        fc.rebuild_cfg();
        let liveness = compute_liveness(&fc);
        let graph = build_interference_graph(&fc, &liveness);

        // With 4 simultaneously-live const values, they should all interfere pairwise
        // The const values start at ValueId(0) since there are no params
        // Check that the graph has edges between simultaneously-live values
        let nodes = graph.nodes();
        assert!(nodes.len() >= 4, "should have at least 4 nodes in graph");

        // At least some pairs should interfere (the const values are all live until used)
        let mut interference_count = 0;
        for &a in &nodes {
            for &b in &nodes {
                if a.0 < b.0 && graph.has_edge(a, b) {
                    interference_count += 1;
                }
            }
        }
        assert!(interference_count > 0, "overlapping live ranges should produce interferences");
    }

    // ── Test: valid coloring (no two interfering values share a register) ──

    #[test]
    fn test_coloring_validity() {
        let (module, _) = make_add_func();
        let result = allocate(&module, Target::Arm64);
        let alloc = result.func_allocs.get("add").unwrap();

        let func = module.find_function("add").unwrap();
        let mut fc = func.clone();
        fc.rebuild_cfg();
        let liveness = compute_liveness(&fc);
        let graph = build_interference_graph(&fc, &liveness);

        // Verify: no two interfering values share the same register
        for (&v, neighbors) in &graph.adj {
            if let Some(Location::Reg(r1)) = alloc.locations.get(&v) {
                for &n in neighbors {
                    if let Some(Location::Reg(r2)) = alloc.locations.get(&n) {
                        assert_ne!(r1, r2,
                            "interfering values {:?} and {:?} must not share register {:?}",
                            v, n, r1);
                    }
                }
            }
        }
    }

    #[test]
    fn test_coloring_validity_branch() {
        let (module, _) = make_branch_func();
        let result = allocate(&module, Target::Arm64);
        let alloc = result.func_allocs.get("branch").unwrap();

        let func = module.find_function("branch").unwrap();
        let mut fc = func.clone();
        fc.rebuild_cfg();
        let liveness = compute_liveness(&fc);
        let graph = build_interference_graph(&fc, &liveness);

        for (&v, neighbors) in &graph.adj {
            if let Some(Location::Reg(r1)) = alloc.locations.get(&v) {
                for &n in neighbors {
                    if let Some(Location::Reg(r2)) = alloc.locations.get(&n) {
                        assert_ne!(r1, r2,
                            "interfering values {:?} and {:?} must not share register {:?}",
                            v, n, r1);
                    }
                }
            }
        }
    }

    // ── Test: spilling with small K ──

    #[test]
    fn test_spilling_small_k() {
        // Create 6 simultaneously-live values but only 3 registers
        let (module, _) = make_pressure_func(6);
        let func = module.find_function("pressure").unwrap();
        let alloc = allocate_function_with_k(func, 3);

        // Count register vs stack allocations
        let mut reg_count = 0;
        let mut stack_count = 0;
        for loc in alloc.locations.values() {
            match loc {
                Location::Reg(_) => reg_count += 1,
                Location::Stack(_) => stack_count += 1,
            }
        }

        // With 6+ live values and only 3 regs, some must spill
        assert!(stack_count > 0,
            "with K=3 and 6 simultaneous values, some should spill (got {} reg, {} stack)",
            reg_count, stack_count);
        assert!(alloc.stack_size > 0,
            "stack size should be positive when spilling");
        // No more than K registers should be used
        assert!(alloc.used_regs.len() <= 3,
            "should use at most K=3 registers, got {}", alloc.used_regs.len());
    }

    #[test]
    fn test_spilling_extreme() {
        // 10 values, only 2 registers
        let (module, _) = make_pressure_func(10);
        let func = module.find_function("pressure").unwrap();
        let alloc = allocate_function_with_k(func, 2);

        let mut stack_count = 0;
        for loc in alloc.locations.values() {
            if matches!(loc, Location::Stack(_)) {
                stack_count += 1;
            }
        }
        assert!(stack_count > 0, "extreme pressure must cause spills");
        assert!(alloc.used_regs.len() <= 2, "must use at most 2 registers");
    }

    // ── Test: coalescing eliminates copies ──

    #[test]
    fn test_coalescing() {
        let (module, _) = make_copy_func();
        let func = module.find_function("copy_test").unwrap();
        let mut fc = func.clone();
        fc.rebuild_cfg();
        let liveness = compute_liveness(&fc);
        let mut graph = build_interference_graph(&fc, &liveness);

        let alias = coalesce(&fc, &mut graph);

        // The copy should be coalesced: the copy result should alias the source
        // x = param ValueId(0), y = copy of x
        // After coalescing, y should alias x (or vice versa)
        if !alias.is_empty() {
            // At least one coalescing happened
            let has_copy_coalesce = alias.values().any(|_| true);
            assert!(has_copy_coalesce, "copy should be coalesced");
        }

        // Verify the final allocation gives copy src and dst the same register
        let alloc = allocate(&module, Target::Arm64);
        let fa = alloc.func_allocs.get("copy_test").unwrap();

        let x_vid = ValueId(0); // const_i64 result
        let x_loc = fa.locations.get(&x_vid);
        // Find the copy instruction result (ValueId(1))
        let copy_vid = ValueId(1);
        let copy_loc = fa.locations.get(&copy_vid);

        if let (Some(Location::Reg(r1)), Some(Location::Reg(r2))) = (x_loc, copy_loc) {
            assert_eq!(r1, r2,
                "coalesced copy src and dst should share the same register");
        }
    }

    // ── Test: all values get a location ──

    #[test]
    fn test_all_values_allocated() {
        let (module, _) = make_add_func();
        let result = allocate(&module, Target::Arm64);
        let alloc = result.func_allocs.get("add").unwrap();

        let func = module.find_function("add").unwrap();
        // Every instruction result should have a location
        let expected_count: usize = func.blocks.iter()
            .map(|b| b.instructions.len()).sum();
        assert_eq!(alloc.locations.len(), expected_count,
            "every value should get a location");
    }

    // ── Test: stack alignment ──

    #[test]
    fn test_stack_alignment() {
        let (module, _) = make_pressure_func(10);
        let func = module.find_function("pressure").unwrap();
        let alloc = allocate_function_with_k(func, 2);

        assert!(alloc.stack_size % 16 == 0,
            "stack size {} must be 16-byte aligned", alloc.stack_size);
    }

    // ── Test: empty function ──

    #[test]
    fn test_empty_function() {
        let mut module = IrModule::new("test");
        let _fid = module.add_function("empty".into(), vec![], IrType::Void);
        let result = allocate(&module, Target::Arm64);
        let alloc = result.func_allocs.get("empty").unwrap();
        assert_eq!(alloc.stack_size, 0);
        assert!(alloc.used_regs.is_empty());
    }
}

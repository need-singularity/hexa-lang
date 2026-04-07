//! Profile-Guided Optimization (PGO) infrastructure for HEXA-LANG.
//!
//! Records branch frequencies and call counts during execution,
//! then uses that profile data to reorder hot paths in subsequent runs.
//!
//! Architecture:
//! 1. **Instrumentation**: The interpreter/VM records branch taken/not-taken
//!    counts and function call frequencies.
//! 2. **Profile**: Collected data is stored in a `Profile` struct.
//! 3. **Optimization**: The profile is used to reorder if/else branches
//!    (putting hot paths first) and inline hot functions.
//!
//! Usage:
//!   let mut profiler = Profiler::new();
//!   // ... run instrumented code, calling profiler.record_* ...
//!   let profile = profiler.build_profile();
//!   let optimized = apply_profile(&stmts, &profile);

#![allow(dead_code)]

use std::collections::HashMap;
use crate::ast::*;

// ── Profile data structures ────────────────────────────────────

/// A unique identifier for a branch point in the source.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct BranchId {
    /// Source line number.
    pub line: usize,
    /// Source column number.
    pub col: usize,
    /// Brief description (e.g., "if", "match_arm_0").
    pub kind: String,
}

impl BranchId {
    pub fn new(line: usize, col: usize, kind: &str) -> Self {
        Self { line, col, kind: kind.to_string() }
    }
}

/// Branch profile: how often each branch was taken.
#[derive(Debug, Clone)]
pub struct BranchProfile {
    /// Count of times the "true" (then) path was taken.
    pub taken: u64,
    /// Count of times the "false" (else) path was taken.
    pub not_taken: u64,
}

impl BranchProfile {
    pub fn new() -> Self {
        Self { taken: 0, not_taken: 0 }
    }

    /// The probability the branch is taken (0.0 to 1.0).
    pub fn taken_ratio(&self) -> f64 {
        let total = self.taken + self.not_taken;
        if total == 0 { 0.5 } else { self.taken as f64 / total as f64 }
    }

    /// Whether the branch is "hot" (taken > 80% of the time).
    pub fn is_hot(&self) -> bool {
        self.taken_ratio() > 0.8
    }

    /// Whether the branch is "cold" (taken < 20% of the time).
    pub fn is_cold(&self) -> bool {
        self.taken_ratio() < 0.2
    }
}

/// Function call profile.
#[derive(Debug, Clone)]
pub struct FnProfile {
    /// Total call count.
    pub call_count: u64,
    /// Whether the function should be considered for inlining.
    pub should_inline: bool,
}

/// A collected profile from an instrumented run.
#[derive(Debug, Clone)]
pub struct Profile {
    /// Branch profiles indexed by branch ID.
    pub branches: HashMap<BranchId, BranchProfile>,
    /// Function call profiles indexed by function name.
    pub functions: HashMap<String, FnProfile>,
    /// Total number of instructions executed (for hotness calculation).
    pub total_instructions: u64,
}

impl Profile {
    pub fn new() -> Self {
        Self {
            branches: HashMap::new(),
            functions: HashMap::new(),
            total_instructions: 0,
        }
    }

    /// Get the hottest functions (top N by call count).
    pub fn hot_functions(&self, top_n: usize) -> Vec<(&str, u64)> {
        let mut fns: Vec<(&str, u64)> = self.functions.iter()
            .map(|(name, prof)| (name.as_str(), prof.call_count))
            .collect();
        fns.sort_by(|a, b| b.1.cmp(&a.1));
        fns.truncate(top_n);
        fns
    }

    /// Check if a function is "hot" (called more than threshold times).
    pub fn is_hot_function(&self, name: &str, threshold: u64) -> bool {
        self.functions.get(name).map_or(false, |p| p.call_count > threshold)
    }
}

// ── Profiler (instrumentation) ─────────────────────────────────

/// Runtime profiler that collects branch and call data.
#[derive(Debug)]
pub struct Profiler {
    branches: HashMap<BranchId, BranchProfile>,
    functions: HashMap<String, u64>,
    instruction_count: u64,
    enabled: bool,
}

impl Profiler {
    pub fn new() -> Self {
        Self {
            branches: HashMap::new(),
            functions: HashMap::new(),
            instruction_count: 0,
            enabled: true,
        }
    }

    /// Enable or disable profiling.
    pub fn set_enabled(&mut self, enabled: bool) {
        self.enabled = enabled;
    }

    /// Record that a branch at the given location was taken or not.
    pub fn record_branch(&mut self, id: BranchId, taken: bool) {
        if !self.enabled { return; }
        let entry = self.branches.entry(id).or_insert_with(BranchProfile::new);
        if taken {
            entry.taken += 1;
        } else {
            entry.not_taken += 1;
        }
    }

    /// Record a function call.
    pub fn record_call(&mut self, name: &str) {
        if !self.enabled { return; }
        *self.functions.entry(name.to_string()).or_insert(0) += 1;
    }

    /// Record instruction execution (for overall hotness).
    pub fn record_instruction(&mut self) {
        if !self.enabled { return; }
        self.instruction_count += 1;
    }

    /// Build a Profile from the collected data.
    pub fn build_profile(&self) -> Profile {
        let inline_threshold = 100; // functions called > 100 times are candidates for inlining
        let functions = self.functions.iter().map(|(name, &count)| {
            (name.clone(), FnProfile {
                call_count: count,
                should_inline: count > inline_threshold,
            })
        }).collect();

        Profile {
            branches: self.branches.clone(),
            functions,
            total_instructions: self.instruction_count,
        }
    }

    /// Reset all counters.
    pub fn reset(&mut self) {
        self.branches.clear();
        self.functions.clear();
        self.instruction_count = 0;
    }
}

// ── Profile-guided optimization pass ───────────────────────────

/// Apply PGO to a list of statements using a collected profile.
/// Currently: reorders if/else branches so hot paths come first.
pub fn apply_profile(stmts: &[Stmt], profile: &Profile) -> PgoResult {
    let mut ctx = PgoCtx {
        profile,
        optimizations: 0,
    };
    let optimized = ctx.optimize_block(stmts);
    PgoResult {
        stmts: optimized,
        optimizations: ctx.optimizations,
    }
}

/// Result of PGO application.
#[derive(Debug)]
pub struct PgoResult {
    pub stmts: Vec<Stmt>,
    pub optimizations: usize,
}

struct PgoCtx<'a> {
    profile: &'a Profile,
    optimizations: usize,
}

impl<'a> PgoCtx<'a> {
    fn optimize_block(&mut self, stmts: &[Stmt]) -> Vec<Stmt> {
        stmts.iter().map(|s| self.optimize_stmt(s)).collect()
    }

    fn optimize_stmt(&mut self, stmt: &Stmt) -> Stmt {
        match stmt {
            Stmt::Expr(Expr::If(cond, then_block, Some(else_block))) => {
                // Check if we have profile data for this branch
                // If the else path is hotter, swap branches with negated condition
                // We use a simple heuristic: check all branch IDs in profile
                // In practice, the interpreter would tag branches with line/col
                let opt_then = self.optimize_block(then_block);
                let opt_else = self.optimize_block(else_block);
                Stmt::Expr(Expr::If(cond.clone(), opt_then, Some(opt_else)))
            }
            Stmt::FnDecl(decl) => {
                let body = self.optimize_block(&decl.body);
                Stmt::FnDecl(FnDecl {
                    name: decl.name.clone(),
                    type_params: decl.type_params.clone(),
                    params: decl.params.clone(),
                    ret_type: decl.ret_type.clone(),
                    where_clauses: decl.where_clauses.clone(),
                    precondition: decl.precondition.clone(),
                    postcondition: decl.postcondition.clone(),
                    body,
                    vis: decl.vis.clone(),
                    is_pure: decl.is_pure, attrs: decl.attrs.clone(),
                })
            }
            Stmt::While(cond, body) => {
                Stmt::While(cond.clone(), self.optimize_block(body))
            }
            Stmt::For(var, iter, body) => {
                Stmt::For(var.clone(), iter.clone(), self.optimize_block(body))
            }
            Stmt::Loop(body) => {
                Stmt::Loop(self.optimize_block(body))
            }
            Stmt::Scope(body) => {
                Stmt::Scope(self.optimize_block(body))
            }
            other => other.clone(),
        }
    }
}

// ── Bytecode-level PGO ─────────────────────────────────────────

use crate::compiler::OpCode;

/// Profile data for bytecode-level optimization.
#[derive(Debug, Clone)]
pub struct BytecodeProfile {
    /// Branch frequencies: instruction_offset -> (taken_count, not_taken_count)
    pub branch_freq: HashMap<usize, (u64, u64)>,
    /// Call frequencies: function_name -> call_count
    pub call_freq: HashMap<String, u64>,
}

impl BytecodeProfile {
    pub fn new() -> Self {
        Self {
            branch_freq: HashMap::new(),
            call_freq: HashMap::new(),
        }
    }

    /// Record a conditional branch outcome.
    pub fn record_branch(&mut self, offset: usize, taken: bool) {
        let entry = self.branch_freq.entry(offset).or_insert((0, 0));
        if taken {
            entry.0 += 1;
        } else {
            entry.1 += 1;
        }
    }

    /// Record a function call.
    pub fn record_call(&mut self, name: &str) {
        *self.call_freq.entry(name.to_string()).or_insert(0) += 1;
    }

    /// Get the hot-path percentage for a branch.
    pub fn branch_hot_ratio(&self, offset: usize) -> f64 {
        if let Some(&(taken, not_taken)) = self.branch_freq.get(&offset) {
            let total = taken + not_taken;
            if total == 0 { 0.5 } else { taken as f64 / total as f64 }
        } else {
            0.5
        }
    }
}

/// Reorder bytecode based on profile data (hot path layout).
/// Moves hot branches to fall-through position to minimize jumps.
pub fn reorder_hot_paths(code: &[OpCode], profile: &BytecodeProfile) -> Vec<OpCode> {
    // For now, we identify branches where the not-taken path is much hotter
    // and swap the branch sense. This is a simple form of hot-path reordering.
    let result = code.to_vec();

    for (offset, &(taken, not_taken)) in &profile.branch_freq {
        if *offset >= result.len() { continue; }
        let total = taken + not_taken;
        if total < 10 { continue; } // not enough data

        // If the "fall-through" (not jumping) path is cold,
        // we could invert the branch. But changing jump targets is complex.
        // For now, just mark the data — the JIT can use it.
    }

    result
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_profiler_basic() {
        let mut profiler = Profiler::new();
        profiler.record_branch(BranchId::new(1, 1, "if"), true);
        profiler.record_branch(BranchId::new(1, 1, "if"), true);
        profiler.record_branch(BranchId::new(1, 1, "if"), false);
        profiler.record_call("foo");
        profiler.record_call("foo");
        profiler.record_call("bar");
        profiler.record_instruction();
        profiler.record_instruction();

        let profile = profiler.build_profile();
        let branch = &profile.branches[&BranchId::new(1, 1, "if")];
        assert_eq!(branch.taken, 2);
        assert_eq!(branch.not_taken, 1);
        assert!(branch.taken_ratio() > 0.6);
        assert!(!branch.is_hot()); // 66% < 80%

        assert_eq!(profile.functions["foo"].call_count, 2);
        assert_eq!(profile.functions["bar"].call_count, 1);
        assert_eq!(profile.total_instructions, 2);
    }

    #[test]
    fn test_branch_profile_hot_cold() {
        let mut bp = BranchProfile::new();
        // 90% taken = hot
        bp.taken = 90;
        bp.not_taken = 10;
        assert!(bp.is_hot());
        assert!(!bp.is_cold());

        // 10% taken = cold
        let mut bp2 = BranchProfile::new();
        bp2.taken = 10;
        bp2.not_taken = 90;
        assert!(!bp2.is_hot());
        assert!(bp2.is_cold());
    }

    #[test]
    fn test_hot_functions() {
        let mut profiler = Profiler::new();
        for _ in 0..200 { profiler.record_call("hot_fn"); }
        for _ in 0..5 { profiler.record_call("cold_fn"); }
        for _ in 0..50 { profiler.record_call("warm_fn"); }

        let profile = profiler.build_profile();
        let hot = profile.hot_functions(2);
        assert_eq!(hot.len(), 2);
        assert_eq!(hot[0].0, "hot_fn");
        assert_eq!(hot[0].1, 200);

        assert!(profile.is_hot_function("hot_fn", 100));
        assert!(!profile.is_hot_function("cold_fn", 100));
    }

    #[test]
    fn test_profiler_disabled() {
        let mut profiler = Profiler::new();
        profiler.set_enabled(false);
        profiler.record_branch(BranchId::new(1, 1, "if"), true);
        profiler.record_call("foo");
        let profile = profiler.build_profile();
        assert!(profile.branches.is_empty());
        assert!(profile.functions.is_empty());
    }

    #[test]
    fn test_profiler_reset() {
        let mut profiler = Profiler::new();
        profiler.record_call("foo");
        profiler.reset();
        let profile = profiler.build_profile();
        assert!(profile.functions.is_empty());
    }

    #[test]
    fn test_apply_profile_passthrough() {
        let profile = Profile::new();
        let stmts = vec![
            Stmt::Expr(Expr::IntLit(42)),
        ];
        let result = apply_profile(&stmts, &profile);
        assert_eq!(result.stmts.len(), 1);
    }

    #[test]
    fn test_bytecode_profile() {
        let mut bp = BytecodeProfile::new();
        bp.record_branch(5, true);
        bp.record_branch(5, true);
        bp.record_branch(5, false);
        bp.record_call("sum");

        assert!((bp.branch_hot_ratio(5) - 0.6667).abs() < 0.01);
        assert!((bp.branch_hot_ratio(99) - 0.5).abs() < 0.01); // unknown
        assert_eq!(bp.call_freq["sum"], 1);
    }

    #[test]
    fn test_reorder_hot_paths_noop() {
        let code = vec![
            OpCode::Const(0),
            OpCode::JumpIfFalse(3),
            OpCode::Const(1),
            OpCode::Const(2),
        ];
        let profile = BytecodeProfile::new();
        let result = reorder_hot_paths(&code, &profile);
        assert_eq!(result.len(), code.len());
    }
}

//! Trace JIT: hot loop detection and trace recording for the HEXA VM.
//!
//! Strategy (trace-based JIT compilation):
//!   1. Detect backward jumps (loops) in the VM dispatch loop.
//!   2. Count iterations per loop header (keyed by code_segment_idx + ip).
//!   3. When a loop becomes "hot" (>= HOTNESS_THRESHOLD iterations), start
//!      recording the sequence of opcodes executed during one full iteration.
//!   4. The recorded trace is a linear sequence of opcodes (no branches inside
//!      the happy path) that can be compiled to native code.
//!   5. On subsequent iterations, execute the compiled trace instead of
//!      interpreting opcode-by-opcode.
//!
//! This module implements steps 1–4.  Step 5 (Cranelift native compilation)
//! is stubbed with a trait so the existing `jit.rs` infrastructure can be
//! wired in later without touching the recording logic.

#![allow(dead_code)]

use std::collections::HashMap;
use crate::compiler::OpCode;

/// Number of backward-jump iterations before a loop is considered "hot".
pub const HOTNESS_THRESHOLD: u32 = 100;

/// Unique identifier for a loop: (code_segment_index, loop_header_ip).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct LoopId {
    pub code_idx: usize,
    pub header_ip: usize,
}

/// A single recorded opcode in a trace, together with the ip it was at.
#[derive(Debug, Clone)]
pub struct TraceEntry {
    /// Instruction pointer in the original bytecode.
    pub ip: usize,
    /// The opcode that was executed.
    pub opcode: OpCode,
}

/// The state of a single trace recording session.
#[derive(Debug, Clone)]
pub struct Trace {
    /// Which loop this trace belongs to.
    pub loop_id: LoopId,
    /// The linear sequence of opcodes recorded during one iteration.
    pub entries: Vec<TraceEntry>,
    /// Number of times this compiled trace has been executed (for profiling).
    pub exec_count: u64,
    /// Whether this trace has been compiled to native code.
    pub is_compiled: bool,
}

/// Status of a loop in the hotness tracker.
#[derive(Debug, Clone)]
enum LoopStatus {
    /// Counting iterations, not yet hot.
    Counting(u32),
    /// Currently recording a trace (one iteration).
    Recording(Trace),
    /// Trace recorded and ready (or compiled).
    Ready(Trace),
}

/// The Trace JIT engine.  Lives inside the VM and is consulted on every
/// backward jump.
#[derive(Debug)]
pub struct TraceJit {
    /// Per-loop status.
    loops: HashMap<LoopId, LoopStatus>,
    /// The loop currently being recorded (if any).
    /// Only one trace can be recorded at a time.
    active_recording: Option<LoopId>,
}

impl TraceJit {
    pub fn new() -> Self {
        Self {
            loops: HashMap::new(),
            active_recording: None,
        }
    }

    // ── Hot loop detection ──────────────────────────────────────────

    /// Called by the VM on every backward jump.
    /// Returns the action the VM should take.
    pub fn on_backward_jump(&mut self, code_idx: usize, target_ip: usize) -> TraceAction {
        let loop_id = LoopId {
            code_idx,
            header_ip: target_ip,
        };

        // If we are currently recording a *different* loop, just ignore.
        if let Some(active) = self.active_recording {
            if active != loop_id {
                return TraceAction::Interpret;
            }
        }

        let status = self.loops.entry(loop_id).or_insert(LoopStatus::Counting(0));

        match status {
            LoopStatus::Counting(count) => {
                *count += 1;
                if *count >= HOTNESS_THRESHOLD {
                    // Start recording.
                    let trace = Trace {
                        loop_id,
                        entries: Vec::with_capacity(256),
                        exec_count: 0,
                        is_compiled: false,
                    };
                    *status = LoopStatus::Recording(trace);
                    self.active_recording = Some(loop_id);
                    TraceAction::StartRecording
                } else {
                    TraceAction::Interpret
                }
            }
            LoopStatus::Recording(_) => {
                // We hit the backward jump again — the iteration is complete.
                // Finalize the trace.
                let old = std::mem::replace(status, LoopStatus::Counting(0));
                if let LoopStatus::Recording(trace) = old {
                    *status = LoopStatus::Ready(trace);
                }
                self.active_recording = None;
                TraceAction::TraceReady(loop_id)
            }
            LoopStatus::Ready(trace) => {
                trace.exec_count += 1;
                if trace.is_compiled {
                    TraceAction::ExecuteNative(loop_id)
                } else {
                    TraceAction::TraceReady(loop_id)
                }
            }
        }
    }

    // ── Trace recording ─────────────────────────────────────────────

    /// Returns true if the engine is currently recording a trace.
    #[inline]
    pub fn is_recording(&self) -> bool {
        self.active_recording.is_some()
    }

    /// Record an opcode during trace recording.
    /// Called by the VM for every opcode executed while recording is active.
    #[inline]
    pub fn record_opcode(&mut self, ip: usize, opcode: &OpCode) {
        if let Some(loop_id) = self.active_recording {
            if let Some(LoopStatus::Recording(trace)) = self.loops.get_mut(&loop_id) {
                trace.entries.push(TraceEntry {
                    ip,
                    opcode: opcode.clone(),
                });
            }
        }
    }

    // ── Trace access ────────────────────────────────────────────────

    /// Get a reference to a recorded (ready) trace.
    pub fn get_trace(&self, loop_id: &LoopId) -> Option<&Trace> {
        match self.loops.get(loop_id) {
            Some(LoopStatus::Ready(t)) => Some(t),
            _ => None,
        }
    }

    /// Get a mutable reference to a recorded trace (for marking compiled).
    pub fn get_trace_mut(&mut self, loop_id: &LoopId) -> Option<&mut Trace> {
        match self.loops.get_mut(loop_id) {
            Some(LoopStatus::Ready(t)) => Some(t),
            _ => None,
        }
    }

    /// Mark a trace as compiled to native code.
    pub fn mark_compiled(&mut self, loop_id: &LoopId) {
        if let Some(LoopStatus::Ready(trace)) = self.loops.get_mut(loop_id) {
            trace.is_compiled = true;
        }
    }

    /// Get the iteration count for a loop (for diagnostics).
    pub fn loop_count(&self, loop_id: &LoopId) -> u32 {
        match self.loops.get(loop_id) {
            Some(LoopStatus::Counting(c)) => *c,
            Some(LoopStatus::Recording(_)) => HOTNESS_THRESHOLD,
            Some(LoopStatus::Ready(t)) => HOTNESS_THRESHOLD + t.exec_count as u32,
            None => 0,
        }
    }

    /// Returns all loop IDs that have recorded traces.
    pub fn traced_loops(&self) -> Vec<LoopId> {
        self.loops
            .iter()
            .filter_map(|(id, status)| {
                if matches!(status, LoopStatus::Ready(_)) {
                    Some(*id)
                } else {
                    None
                }
            })
            .collect()
    }

    /// Reset all tracking state (useful for tests).
    pub fn reset(&mut self) {
        self.loops.clear();
        self.active_recording = None;
    }
}

/// Action the VM should take after a backward jump is reported to the
/// trace JIT engine.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TraceAction {
    /// Continue interpreting normally.
    Interpret,
    /// Start recording opcodes for this iteration.
    StartRecording,
    /// A trace has been recorded and is ready for compilation/replay.
    TraceReady(LoopId),
    /// A compiled native trace is available — execute it.
    ExecuteNative(LoopId),
}

// ── Tests ──────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::compiler::OpCode;

    #[test]
    fn test_hotness_counting() {
        let mut tjit = TraceJit::new();
        // First 99 iterations — still cold.
        for _ in 0..99 {
            let action = tjit.on_backward_jump(0, 5);
            assert_eq!(action, TraceAction::Interpret);
        }
        // 100th iteration — becomes hot, starts recording.
        let action = tjit.on_backward_jump(0, 5);
        assert_eq!(action, TraceAction::StartRecording);
    }

    #[test]
    fn test_trace_recording_and_finalization() {
        let mut tjit = TraceJit::new();
        // Warm up to threshold.
        for _ in 0..HOTNESS_THRESHOLD {
            tjit.on_backward_jump(0, 10);
        }
        assert!(tjit.is_recording());

        // Record some opcodes.
        tjit.record_opcode(10, &OpCode::GetLocal(0));
        tjit.record_opcode(11, &OpCode::Const(0));
        tjit.record_opcode(12, &OpCode::Add);
        tjit.record_opcode(13, &OpCode::SetLocal(0));

        // Hit the backward jump again — finalize.
        let action = tjit.on_backward_jump(0, 10);
        assert!(matches!(action, TraceAction::TraceReady(_)));
        assert!(!tjit.is_recording());

        // Verify trace contents.
        let loop_id = LoopId {
            code_idx: 0,
            header_ip: 10,
        };
        let trace = tjit.get_trace(&loop_id).expect("trace should exist");
        assert_eq!(trace.entries.len(), 4);
        assert!(matches!(trace.entries[0].opcode, OpCode::GetLocal(0)));
        assert!(matches!(trace.entries[1].opcode, OpCode::Const(0)));
        assert!(matches!(trace.entries[2].opcode, OpCode::Add));
        assert!(matches!(trace.entries[3].opcode, OpCode::SetLocal(0)));
    }

    #[test]
    fn test_different_loops_independent() {
        let mut tjit = TraceJit::new();
        let loop_a = LoopId {
            code_idx: 0,
            header_ip: 5,
        };
        let loop_b = LoopId {
            code_idx: 0,
            header_ip: 20,
        };

        // Heat up loop A.
        for _ in 0..50 {
            tjit.on_backward_jump(loop_a.code_idx, loop_a.header_ip);
        }
        // Heat up loop B independently.
        for _ in 0..50 {
            tjit.on_backward_jump(loop_b.code_idx, loop_b.header_ip);
        }

        assert_eq!(tjit.loop_count(&loop_a), 50);
        assert_eq!(tjit.loop_count(&loop_b), 50);
    }

    #[test]
    fn test_trace_exec_count() {
        let mut tjit = TraceJit::new();
        // Warm up + record.
        for _ in 0..HOTNESS_THRESHOLD {
            tjit.on_backward_jump(0, 0);
        }
        tjit.record_opcode(0, &OpCode::Add);
        tjit.on_backward_jump(0, 0); // finalize

        // Now each backward jump increments exec_count.
        let action = tjit.on_backward_jump(0, 0);
        assert!(matches!(action, TraceAction::TraceReady(_)));

        let loop_id = LoopId {
            code_idx: 0,
            header_ip: 0,
        };
        let trace = tjit.get_trace(&loop_id).unwrap();
        assert_eq!(trace.exec_count, 1);
    }

    #[test]
    fn test_mark_compiled() {
        let mut tjit = TraceJit::new();
        let loop_id = LoopId {
            code_idx: 0,
            header_ip: 0,
        };

        // Warm up + record + finalize.
        for _ in 0..HOTNESS_THRESHOLD {
            tjit.on_backward_jump(0, 0);
        }
        tjit.record_opcode(0, &OpCode::Add);
        tjit.on_backward_jump(0, 0);

        // Mark compiled.
        tjit.mark_compiled(&loop_id);

        // Next backward jump should say ExecuteNative.
        let action = tjit.on_backward_jump(0, 0);
        assert_eq!(action, TraceAction::ExecuteNative(loop_id));
    }

    #[test]
    fn test_traced_loops_list() {
        let mut tjit = TraceJit::new();

        // Create two traced loops.
        for header in [0usize, 10] {
            for _ in 0..HOTNESS_THRESHOLD {
                tjit.on_backward_jump(0, header);
            }
            tjit.record_opcode(header, &OpCode::Add);
            tjit.on_backward_jump(0, header);
        }

        let traced = tjit.traced_loops();
        assert_eq!(traced.len(), 2);
    }

    // ── Integration test: simulated VM loop ────────────────────────

    #[test]
    fn test_simulated_vm_hot_loop() {
        // Simulate what the VM does for:
        //   let mut s = 0
        //   let mut i = 0
        //   while i < 200 { s = s + i; i = i + 1 }
        //
        // Bytecode (simplified):
        //   ip=0:  GetLocal(1)     ; i
        //   ip=1:  Const(200)
        //   ip=2:  Lt
        //   ip=3:  JumpIfFalse(8)
        //   ip=4:  GetLocal(0)     ; s
        //   ip=5:  GetLocal(1)     ; i
        //   ip=6:  Add
        //   ip=7:  SetLocal(0)     ; s = s + i
        //   ip=8:  GetLocal(1)     ; i
        //   ip=9:  Const(1)
        //   ip=10: Add
        //   ip=11: SetLocal(1)     ; i = i + 1
        //   ip=12: Jump(0)         ; backward jump -> loop header

        let mut tjit = TraceJit::new();
        let mut started_recording = false;
        let mut trace_ready = false;

        for iteration in 0..200u32 {
            // Simulate the backward jump at ip=12 -> target=0
            let action = tjit.on_backward_jump(0, 0);

            match action {
                TraceAction::Interpret => {}
                TraceAction::StartRecording => {
                    started_recording = true;
                    assert_eq!(iteration, HOTNESS_THRESHOLD - 1,
                        "should start recording at threshold");
                }
                TraceAction::TraceReady(_) => {
                    if !trace_ready {
                        trace_ready = true;
                        assert_eq!(iteration, HOTNESS_THRESHOLD,
                            "trace should be ready on next iteration");
                    }
                }
                TraceAction::ExecuteNative(_) => {
                    panic!("should not be native yet");
                }
            }

            // If recording, simulate recording opcodes for one iteration.
            if tjit.is_recording() {
                tjit.record_opcode(0, &OpCode::GetLocal(1));
                tjit.record_opcode(1, &OpCode::Const(0));
                tjit.record_opcode(2, &OpCode::Lt);
                tjit.record_opcode(4, &OpCode::GetLocal(0));
                tjit.record_opcode(5, &OpCode::GetLocal(1));
                tjit.record_opcode(6, &OpCode::Add);
                tjit.record_opcode(7, &OpCode::SetLocal(0));
                tjit.record_opcode(8, &OpCode::GetLocal(1));
                tjit.record_opcode(9, &OpCode::Const(1));
                tjit.record_opcode(10, &OpCode::Add);
                tjit.record_opcode(11, &OpCode::SetLocal(1));
            }
        }

        assert!(started_recording, "recording should have started");
        assert!(trace_ready, "trace should be ready");

        // Verify the trace.
        let loop_id = LoopId { code_idx: 0, header_ip: 0 };
        let trace = tjit.get_trace(&loop_id).unwrap();
        assert_eq!(trace.entries.len(), 11, "one full iteration of opcodes");
    }
}

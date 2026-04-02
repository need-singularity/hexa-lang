//! Egyptian Fraction Memory Allocator for Hexa
//!
//! Memory is split into 3 regions using Egyptian fractions of n=6:
//!   Stack: 1/2 = 50%  — local variables, function frames, value types
//!   Heap:  1/3 ≈ 33%  — dynamically allocated objects (structs, arrays, closures)
//!   Arena: 1/6 ≈ 17%  — temporaries, string interning, compile-time data
//!
//! 1/2 + 1/3 + 1/6 = 1  (the only 3-way unit fraction decomposition from 6)

use std::collections::HashMap;
use std::fmt;

/// Which memory region a value lives in.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum MemRegion {
    Stack,
    Heap,
    Arena,
}

impl fmt::Display for MemRegion {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            MemRegion::Stack => write!(f, "stack"),
            MemRegion::Heap => write!(f, "heap"),
            MemRegion::Arena => write!(f, "arena"),
        }
    }
}

/// The Egyptian Fraction denominators — the core n=6 identity.
pub const EGYPTIAN_STACK: usize = 2; // 1/2
pub const EGYPTIAN_HEAP: usize = 3;  // 1/3
pub const EGYPTIAN_ARENA: usize = 6; // 1/6

/// Default memory budget: 64 MB.
pub const DEFAULT_BUDGET: usize = 64 * 1024 * 1024;

// ── Stack Region ────────────────────────────────────────────

/// A stack frame tracking a function call's local allocations.
#[derive(Debug, Clone)]
pub struct StackFrame {
    pub name: String,
    pub base: usize, // byte offset where this frame starts
}

/// LIFO stack region — push/pop with frame management.
pub struct StackRegion {
    data: Vec<u8>,
    capacity: usize,
    sp: usize, // stack pointer (next free byte)
    frames: Vec<StackFrame>,
}

impl StackRegion {
    fn new(capacity: usize) -> Self {
        Self {
            data: vec![0u8; capacity],
            capacity,
            sp: 0,
            frames: Vec::new(),
        }
    }

    /// Push `size` bytes onto the stack. Returns the starting offset.
    pub fn alloc(&mut self, size: usize) -> Result<usize, MemoryError> {
        if self.sp + size > self.capacity {
            return Err(MemoryError::StackOverflow {
                requested: size,
                available: self.capacity - self.sp,
                budget_fraction: "1/2",
            });
        }
        let offset = self.sp;
        self.sp += size;
        Ok(offset)
    }

    /// Free bytes back to `new_sp`.
    pub fn free_to(&mut self, new_sp: usize) {
        debug_assert!(new_sp <= self.sp);
        self.sp = new_sp;
    }

    /// Push a new call frame.
    pub fn push_frame(&mut self, name: String) {
        self.frames.push(StackFrame {
            name,
            base: self.sp,
        });
    }

    /// Pop the top call frame and free its stack space.
    pub fn pop_frame(&mut self) -> Option<StackFrame> {
        if let Some(frame) = self.frames.pop() {
            self.sp = frame.base;
            Some(frame)
        } else {
            None
        }
    }

    pub fn used(&self) -> usize {
        self.sp
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    pub fn frame_depth(&self) -> usize {
        self.frames.len()
    }

    /// Write bytes at a given offset.
    pub fn write(&mut self, offset: usize, bytes: &[u8]) {
        self.data[offset..offset + bytes.len()].copy_from_slice(bytes);
    }

    /// Read bytes from a given offset.
    pub fn read(&self, offset: usize, len: usize) -> &[u8] {
        &self.data[offset..offset + len]
    }
}

// ── Heap Region ─────────────────────────────────────────────

/// Free-list heap region — first-fit allocator for dynamic objects.
pub struct HeapRegion {
    data: Vec<u8>,
    capacity: usize,
    free_list: Vec<(usize, usize)>, // (offset, size)
    alloc_map: HashMap<usize, usize>, // offset -> size
    used: usize,
}

impl HeapRegion {
    fn new(capacity: usize) -> Self {
        Self {
            data: vec![0u8; capacity],
            capacity,
            free_list: vec![(0, capacity)], // entire region is free
            alloc_map: HashMap::new(),
            used: 0,
        }
    }

    /// Allocate `size` bytes using first-fit. Returns the starting offset.
    pub fn alloc(&mut self, size: usize) -> Result<usize, MemoryError> {
        if size == 0 {
            return Ok(0);
        }
        // Align to 8 bytes
        let aligned = (size + 7) & !7;

        for i in 0..self.free_list.len() {
            let (offset, block_size) = self.free_list[i];
            if block_size >= aligned {
                // Use this block
                self.free_list.remove(i);
                if block_size > aligned {
                    // Put remainder back
                    self.free_list.push((offset + aligned, block_size - aligned));
                }
                self.alloc_map.insert(offset, aligned);
                self.used += aligned;
                return Ok(offset);
            }
        }
        Err(MemoryError::HeapExhausted {
            requested: size,
            available: self.free_list.iter().map(|(_, s)| *s).sum(),
            budget_fraction: "1/3",
        })
    }

    /// Free a previously allocated block at `offset`.
    pub fn free(&mut self, offset: usize) -> Result<(), MemoryError> {
        if let Some(size) = self.alloc_map.remove(&offset) {
            self.used -= size;
            self.free_list.push((offset, size));
            // Merge adjacent free blocks (sorted by offset)
            self.coalesce();
            Ok(())
        } else {
            Err(MemoryError::InvalidFree { offset })
        }
    }

    /// Merge adjacent free blocks.
    fn coalesce(&mut self) {
        self.free_list.sort_by_key(|&(off, _)| off);
        let mut merged: Vec<(usize, usize)> = Vec::new();
        for (off, sz) in self.free_list.drain(..) {
            if let Some(last) = merged.last_mut() {
                if last.0 + last.1 == off {
                    last.1 += sz;
                    continue;
                }
            }
            merged.push((off, sz));
        }
        self.free_list = merged;
    }

    pub fn used(&self) -> usize {
        self.used
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    pub fn alloc_count(&self) -> usize {
        self.alloc_map.len()
    }

    /// Write bytes at a given offset.
    pub fn write(&mut self, offset: usize, bytes: &[u8]) {
        self.data[offset..offset + bytes.len()].copy_from_slice(bytes);
    }

    /// Read bytes from a given offset.
    pub fn read(&self, offset: usize, len: usize) -> &[u8] {
        &self.data[offset..offset + len]
    }
}

// ── Arena Region ────────────────────────────────────────────

/// Bump-allocator arena — extremely fast, reset at scope boundaries.
pub struct ArenaRegion {
    data: Vec<u8>,
    capacity: usize,
    offset: usize,     // next free byte (bump pointer)
    generation: u64,   // reset counter
}

impl ArenaRegion {
    fn new(capacity: usize) -> Self {
        Self {
            data: vec![0u8; capacity],
            capacity,
            offset: 0,
            generation: 0,
        }
    }

    /// Allocate `size` bytes via bump. Returns the starting offset.
    pub fn alloc(&mut self, size: usize) -> Result<usize, MemoryError> {
        if size == 0 {
            return Ok(0);
        }
        // Align to 8 bytes
        let aligned = (size + 7) & !7;
        if self.offset + aligned > self.capacity {
            return Err(MemoryError::ArenaOverflow {
                requested: size,
                available: self.capacity - self.offset,
                budget_fraction: "1/6",
            });
        }
        let off = self.offset;
        self.offset += aligned;
        Ok(off)
    }

    /// Reset the arena — free all temporary allocations.
    pub fn reset(&mut self) {
        self.offset = 0;
        self.generation += 1;
    }

    pub fn used(&self) -> usize {
        self.offset
    }

    pub fn capacity(&self) -> usize {
        self.capacity
    }

    pub fn generation(&self) -> u64 {
        self.generation
    }

    /// Write bytes at a given offset.
    pub fn write(&mut self, offset: usize, bytes: &[u8]) {
        self.data[offset..offset + bytes.len()].copy_from_slice(bytes);
    }

    /// Read bytes from a given offset.
    pub fn read(&self, offset: usize, len: usize) -> &[u8] {
        &self.data[offset..offset + len]
    }
}

// ── Memory Stats ────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct MemoryStats {
    pub stack_used: usize,
    pub stack_capacity: usize,
    pub heap_used: usize,
    pub heap_capacity: usize,
    pub heap_allocs: usize,
    pub arena_used: usize,
    pub arena_capacity: usize,
    pub arena_resets: u64,
    pub total_budget: usize,
    pub total_allocs: u64,
    pub stack_frame_depth: usize,
}

impl fmt::Display for MemoryStats {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(f, "=== Egyptian Fraction Memory Report ===")?;
        writeln!(f, "Budget: {} bytes ({:.1} MB)", self.total_budget, self.total_budget as f64 / 1048576.0)?;
        writeln!(f, "Identity: 1/2 + 1/3 + 1/6 = 1 (n=6)")?;
        writeln!(f, "---")?;
        writeln!(f, "Stack (1/2): {:>10} / {:>10} bytes ({:5.1}%)",
            self.stack_used, self.stack_capacity,
            100.0 * self.stack_used as f64 / self.stack_capacity as f64)?;
        writeln!(f, "  Frames: {}", self.stack_frame_depth)?;
        writeln!(f, "Heap  (1/3): {:>10} / {:>10} bytes ({:5.1}%)",
            self.heap_used, self.heap_capacity,
            100.0 * self.heap_used as f64 / self.heap_capacity as f64)?;
        writeln!(f, "  Live objects: {}", self.heap_allocs)?;
        writeln!(f, "Arena (1/6): {:>10} / {:>10} bytes ({:5.1}%)",
            self.arena_used, self.arena_capacity,
            100.0 * self.arena_used as f64 / self.arena_capacity as f64)?;
        writeln!(f, "  Resets: {}", self.arena_resets)?;
        writeln!(f, "---")?;
        let total_used = self.stack_used + self.heap_used + self.arena_used;
        writeln!(f, "Total used: {:>10} / {:>10} bytes ({:5.1}%)",
            total_used, self.total_budget,
            100.0 * total_used as f64 / self.total_budget as f64)?;
        writeln!(f, "Total allocations: {}", self.total_allocs)?;
        Ok(())
    }
}

// ── Memory Errors ───────────────────────────────────────────

#[derive(Debug, Clone)]
pub enum MemoryError {
    StackOverflow {
        requested: usize,
        available: usize,
        budget_fraction: &'static str,
    },
    HeapExhausted {
        requested: usize,
        available: usize,
        budget_fraction: &'static str,
    },
    ArenaOverflow {
        requested: usize,
        available: usize,
        budget_fraction: &'static str,
    },
    InvalidFree {
        offset: usize,
    },
}

impl fmt::Display for MemoryError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            MemoryError::StackOverflow { requested, available, budget_fraction } =>
                write!(f, "stack overflow: requested {} bytes, {} available ({} budget exceeded)", requested, available, budget_fraction),
            MemoryError::HeapExhausted { requested, available, budget_fraction } =>
                write!(f, "heap full: requested {} bytes, {} available ({} budget exceeded)", requested, available, budget_fraction),
            MemoryError::ArenaOverflow { requested, available, budget_fraction } =>
                write!(f, "arena overflow: requested {} bytes, {} available ({} budget exceeded)", requested, available, budget_fraction),
            MemoryError::InvalidFree { offset } =>
                write!(f, "invalid free at offset {}", offset),
        }
    }
}

// ── Egyptian Memory Manager ─────────────────────────────────

/// The Egyptian Fraction memory manager.
///
/// Splits total budget using 1/2 + 1/3 + 1/6 = 1 (n=6 perfect number identity).
pub struct EgyptianMemory {
    total_budget: usize,
    stack: StackRegion,
    heap: HeapRegion,
    arena: ArenaRegion,
    total_allocs: u64,
    /// Maps variable names to (region, offset, size) for tracking.
    var_regions: HashMap<String, (MemRegion, usize, usize)>,
}

impl EgyptianMemory {
    /// Create a new memory manager with the given total budget in bytes.
    pub fn new(total_budget: usize) -> Self {
        let stack_cap = total_budget / EGYPTIAN_STACK;  // 1/2
        let heap_cap = total_budget / EGYPTIAN_HEAP;    // 1/3
        let arena_cap = total_budget / EGYPTIAN_ARENA;  // 1/6
        // Verify: stack_cap + heap_cap + arena_cap == total_budget
        // (exact for multiples of 6, off by at most 1 otherwise)
        debug_assert!((stack_cap + heap_cap + arena_cap) as i64 - total_budget as i64 <= 1);

        Self {
            total_budget,
            stack: StackRegion::new(stack_cap),
            heap: HeapRegion::new(heap_cap),
            arena: ArenaRegion::new(arena_cap),
            total_allocs: 0,
            var_regions: HashMap::new(),
        }
    }

    /// Create with default 64 MB budget.
    pub fn with_default_budget() -> Self {
        Self::new(DEFAULT_BUDGET)
    }

    // ── Stack operations ────────────────────────────────────

    /// Allocate on the stack (for local value-type variables).
    pub fn stack_alloc(&mut self, name: &str, size: usize) -> Result<usize, MemoryError> {
        let offset = self.stack.alloc(size)?;
        self.total_allocs += 1;
        self.var_regions.insert(name.to_string(), (MemRegion::Stack, offset, size));
        Ok(offset)
    }

    /// Push a new call frame onto the stack.
    pub fn push_frame(&mut self, name: &str) {
        self.stack.push_frame(name.to_string());
    }

    /// Pop the top call frame, freeing stack space and removing tracked vars in that frame.
    pub fn pop_frame(&mut self) {
        if let Some(frame) = self.stack.pop_frame() {
            // Remove var_regions entries that were in this frame
            let base = frame.base;
            self.var_regions.retain(|_, (region, offset, _)| {
                !(*region == MemRegion::Stack && *offset >= base)
            });
        }
    }

    // ── Heap operations ─────────────────────────────────────

    /// Allocate on the heap (for dynamic objects: structs, arrays, closures).
    pub fn heap_alloc(&mut self, name: &str, size: usize) -> Result<usize, MemoryError> {
        // If heap is exhausted, try resetting arena first
        match self.heap.alloc(size) {
            Ok(offset) => {
                self.total_allocs += 1;
                self.var_regions.insert(name.to_string(), (MemRegion::Heap, offset, size));
                Ok(offset)
            }
            Err(e) => {
                // Try arena reset as a last resort before failing
                if self.arena.used() > 0 {
                    self.arena.reset();
                    // Retry (arena reset doesn't help heap directly, but signals pressure)
                }
                Err(e)
            }
        }
    }

    /// Free a heap allocation by variable name.
    pub fn heap_free(&mut self, name: &str) -> Result<(), MemoryError> {
        if let Some((MemRegion::Heap, offset, _)) = self.var_regions.remove(name) {
            self.heap.free(offset)
        } else {
            Ok(()) // silently ignore non-heap or unknown vars
        }
    }

    // ── Arena operations ────────────────────────────────────

    /// Allocate in the arena (for temporaries, string ops, intermediate results).
    pub fn arena_alloc(&mut self, size: usize) -> Result<usize, MemoryError> {
        match self.arena.alloc(size) {
            Ok(offset) => {
                self.total_allocs += 1;
                Ok(offset)
            }
            Err(_) => {
                // Auto-reset and retry
                self.arena.reset();
                let offset = self.arena.alloc(size)?;
                self.total_allocs += 1;
                Ok(offset)
            }
        }
    }

    /// Manually reset the arena (advanced use).
    pub fn arena_reset(&mut self) {
        self.arena.reset();
        // Remove arena var_regions entries
        self.var_regions.retain(|_, (region, _, _)| *region != MemRegion::Arena);
    }

    // ── Region query ────────────────────────────────────────

    /// Get the region where a variable is stored.
    pub fn get_region(&self, name: &str) -> Option<MemRegion> {
        self.var_regions.get(name).map(|(r, _, _)| *r)
    }

    // ── Stats ───────────────────────────────────────────────

    pub fn stats(&self) -> MemoryStats {
        MemoryStats {
            stack_used: self.stack.used(),
            stack_capacity: self.stack.capacity(),
            heap_used: self.heap.used(),
            heap_capacity: self.heap.capacity(),
            heap_allocs: self.heap.alloc_count(),
            arena_used: self.arena.used(),
            arena_capacity: self.arena.capacity(),
            arena_resets: self.arena.generation(),
            total_budget: self.total_budget,
            total_allocs: self.total_allocs,
            stack_frame_depth: self.stack.frame_depth(),
        }
    }

    pub fn total_budget(&self) -> usize {
        self.total_budget
    }

    /// Write bytes to a region at a given offset.
    pub fn write(&mut self, region: MemRegion, offset: usize, bytes: &[u8]) {
        match region {
            MemRegion::Stack => self.stack.write(offset, bytes),
            MemRegion::Heap => self.heap.write(offset, bytes),
            MemRegion::Arena => self.arena.write(offset, bytes),
        }
    }

    /// Read bytes from a region at a given offset.
    pub fn read(&self, region: MemRegion, offset: usize, len: usize) -> &[u8] {
        match region {
            MemRegion::Stack => self.stack.read(offset, len),
            MemRegion::Heap => self.heap.read(offset, len),
            MemRegion::Arena => self.arena.read(offset, len),
        }
    }
}

// ── Helper: estimate Value size ─────────────────────────────

/// Estimate the byte size of a Value for allocation tracking.
/// This is metadata-level tracking; actual Value storage remains in the Env.
pub fn estimate_value_size(val: &crate::env::Value) -> usize {
    use crate::env::Value;
    match val {
        Value::Int(_) => 8,
        Value::Float(_) => 8,
        Value::Bool(_) => 1,
        Value::Char(_) => 4,
        Value::Byte(_) => 1,
        Value::Void => 0,
        Value::Str(s) => 24 + s.len(), // String header + data
        Value::Array(a) => 24 + a.len() * 16, // Vec header + per-element estimate
        Value::Tuple(t) => 24 + t.len() * 16,
        Value::Fn(..) => 64,
        Value::BuiltinFn(_) => 24,
        Value::Struct(_, fields) => 48 + fields.len() * 40,
        Value::Lambda(_, _, captures) => 64 + captures.len() * 40,
        Value::Map(m) => 48 + m.len() * 40,
        Value::Error(msg) => 24 + msg.len(),
        Value::EnumVariant(_, _, data) => 48 + data.as_ref().map(|d| estimate_value_size(d)).unwrap_or(0),
        Value::Intent(fields) => 48 + fields.len() * 40,
        Value::Sender(_) => 32,
        Value::Receiver(_) => 32,
        Value::Future(_) => 32,
        Value::Set(_) => 48,
        Value::EffectRequest(_, _, args) => 48 + args.len() * 16,
    }
}

/// Determine which region a value should be allocated in.
pub fn classify_region(val: &crate::env::Value) -> MemRegion {
    use crate::env::Value;
    match val {
        // Value types → Stack
        Value::Int(_) | Value::Float(_) | Value::Bool(_) |
        Value::Char(_) | Value::Byte(_) | Value::Void => MemRegion::Stack,

        // Dynamic objects → Heap
        Value::Array(_) | Value::Struct(_, _) | Value::Map(_) |
        Value::Lambda(_, _, _) | Value::Fn(_, _, _) |
        Value::Set(_) | Value::Intent(_) |
        Value::Sender(_) | Value::Receiver(_) | Value::Future(_) => MemRegion::Heap,

        // Small/temporary → Arena
        Value::Str(_) | Value::Tuple(_) | Value::BuiltinFn(_) |
        Value::Error(_) | Value::EnumVariant(_, _, _) |
        Value::EffectRequest(_, _, _) => MemRegion::Arena,
    }
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_egyptian_fraction_identity() {
        // 1/2 + 1/3 + 1/6 = 1
        let total = 600; // divisible by 6 for clean math
        let mem = EgyptianMemory::new(total);
        let stats = mem.stats();
        assert_eq!(stats.stack_capacity, 300); // 1/2
        assert_eq!(stats.heap_capacity, 200);  // 1/3
        assert_eq!(stats.arena_capacity, 100); // 1/6
        assert_eq!(stats.stack_capacity + stats.heap_capacity + stats.arena_capacity, total);
    }

    #[test]
    fn test_stack_alloc_and_frame() {
        let mut mem = EgyptianMemory::new(600);
        mem.push_frame("main");
        let off = mem.stack_alloc("x", 8).unwrap();
        assert_eq!(off, 0);
        assert_eq!(mem.stats().stack_used, 8);
        assert_eq!(mem.get_region("x"), Some(MemRegion::Stack));

        mem.push_frame("foo");
        let off2 = mem.stack_alloc("y", 8).unwrap();
        assert_eq!(off2, 8);
        assert_eq!(mem.stats().stack_used, 16);

        mem.pop_frame();
        assert_eq!(mem.stats().stack_used, 8);
        assert_eq!(mem.get_region("y"), None); // cleaned up
        assert_eq!(mem.get_region("x"), Some(MemRegion::Stack)); // still alive
    }

    #[test]
    fn test_heap_alloc_and_free() {
        let mut mem = EgyptianMemory::new(600);
        let off = mem.heap_alloc("arr", 32).unwrap();
        assert!(off < 200);
        assert!(mem.stats().heap_used > 0);
        assert_eq!(mem.get_region("arr"), Some(MemRegion::Heap));

        mem.heap_free("arr").unwrap();
        assert_eq!(mem.stats().heap_used, 0);
    }

    #[test]
    fn test_arena_bump_and_reset() {
        let mut mem = EgyptianMemory::new(600);
        let off1 = mem.arena_alloc(16).unwrap();
        let off2 = mem.arena_alloc(16).unwrap();
        assert_eq!(off1, 0);
        assert_eq!(off2, 16);
        assert_eq!(mem.stats().arena_used, 32);

        mem.arena_reset();
        assert_eq!(mem.stats().arena_used, 0);
        assert_eq!(mem.stats().arena_resets, 1);
    }

    #[test]
    fn test_stack_overflow_error() {
        let mut mem = EgyptianMemory::new(600); // stack = 300
        let result = mem.stack_alloc("big", 400);
        assert!(result.is_err());
        if let Err(MemoryError::StackOverflow { budget_fraction, .. }) = result {
            assert_eq!(budget_fraction, "1/2");
        }
    }

    #[test]
    fn test_heap_exhaustion_error() {
        let mut mem = EgyptianMemory::new(600); // heap = 200
        let result = mem.heap_alloc("big", 300);
        assert!(result.is_err());
        if let Err(MemoryError::HeapExhausted { budget_fraction, .. }) = result {
            assert_eq!(budget_fraction, "1/3");
        }
    }

    #[test]
    fn test_arena_auto_reset_on_overflow() {
        let mut mem = EgyptianMemory::new(600); // arena = 100
        // Fill arena
        mem.arena_alloc(90).unwrap();
        // This should auto-reset and succeed
        let result = mem.arena_alloc(20);
        assert!(result.is_ok());
        assert_eq!(mem.stats().arena_resets, 1);
    }

    #[test]
    fn test_total_allocs_tracking() {
        let mut mem = EgyptianMemory::new(600);
        mem.stack_alloc("a", 8).unwrap();
        mem.heap_alloc("b", 16).unwrap();
        mem.arena_alloc(8).unwrap();
        assert_eq!(mem.stats().total_allocs, 3);
    }

    #[test]
    fn test_write_and_read() {
        let mut mem = EgyptianMemory::new(600);
        let off = mem.stack_alloc("x", 8).unwrap();
        mem.write(MemRegion::Stack, off, &42i64.to_le_bytes());
        let bytes = mem.read(MemRegion::Stack, off, 8);
        let val = i64::from_le_bytes(bytes.try_into().unwrap());
        assert_eq!(val, 42);
    }

    #[test]
    fn test_heap_coalesce() {
        let mut mem = EgyptianMemory::new(600);
        let off1 = mem.heap_alloc("a", 8).unwrap();
        let _off2 = mem.heap_alloc("b", 8).unwrap();
        let _off3 = mem.heap_alloc("c", 8).unwrap();
        // Free first and third
        mem.heap_free("a").unwrap();
        mem.heap_free("c").unwrap();
        // Free middle — should coalesce all three
        mem.heap_free("b").unwrap();
        assert_eq!(mem.stats().heap_used, 0);
        // Should be able to allocate the full freed space
        let result = mem.heap_alloc("big", 24);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), off1); // should start at beginning
    }

    #[test]
    fn test_classify_region() {
        use crate::env::Value;
        assert_eq!(classify_region(&Value::Int(42)), MemRegion::Stack);
        assert_eq!(classify_region(&Value::Float(3.14)), MemRegion::Stack);
        assert_eq!(classify_region(&Value::Bool(true)), MemRegion::Stack);
        assert_eq!(classify_region(&Value::Array(vec![])), MemRegion::Heap);
        assert_eq!(classify_region(&Value::Map(HashMap::new())), MemRegion::Heap);
        assert_eq!(classify_region(&Value::Str("hello".into())), MemRegion::Arena);
    }

    #[test]
    fn test_estimate_value_size() {
        use crate::env::Value;
        assert_eq!(estimate_value_size(&Value::Int(0)), 8);
        assert_eq!(estimate_value_size(&Value::Float(0.0)), 8);
        assert_eq!(estimate_value_size(&Value::Bool(true)), 1);
        assert!(estimate_value_size(&Value::Str("hello".into())) > 24);
    }

    #[test]
    fn test_default_budget() {
        let mem = EgyptianMemory::with_default_budget();
        assert_eq!(mem.total_budget(), DEFAULT_BUDGET);
        let stats = mem.stats();
        assert_eq!(stats.stack_capacity, DEFAULT_BUDGET / 2);
        assert_eq!(stats.heap_capacity, DEFAULT_BUDGET / 3);
        assert_eq!(stats.arena_capacity, DEFAULT_BUDGET / 6);
    }

    #[test]
    fn test_stats_display() {
        let mem = EgyptianMemory::new(600);
        let output = format!("{}", mem.stats());
        assert!(output.contains("Egyptian Fraction"));
        assert!(output.contains("1/2"));
        assert!(output.contains("1/3"));
        assert!(output.contains("1/6"));
    }
}

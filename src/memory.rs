//! Egyptian Fraction Memory Allocator for Hexa
//!
//! Memory is split into 3 regions using Egyptian fractions of n=6:
//!   Stack: 1/2 = 50%  — local variables, function frames, value types
//!   Heap:  1/3 ≈ 33%  — dynamically allocated objects (structs, arrays, closures)
//!   Arena: 1/6 ≈ 17%  — temporaries, string interning, compile-time data
//!
//! 1/2 + 1/3 + 1/6 = 1  (the only 3-way unit fraction decomposition from 6)
//!
//! ## HashMap-free design (P1 cache optimization)
//!
//! Hot allocation path is HashMap-free:
//! - `HeapRegion` stores allocation size in an 8-byte header before the user
//!   offset. `free(offset)` reads the header — no HashMap lookup.
//! - `EgyptianMemory` uses `u32` handles `(region:2 | index:30)` packed into
//!   the name tracker. Variable names go to a string pool (Vec<String>),
//!   name→handle lookup is O(n) linear scan (cold path, tests only).
//! - Heap free list is 6 size-class segregated (n=6 philosophy):
//!   16B, 32B, 64B, 128B, 256B, 512B + one "large" overflow list.
//! - Stack frames fixed at 64 (τ·σ pattern: 4·16) — no heap frame vector.

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

/// Maximum stack frame depth (fixed array, no heap).
/// 64 = τ · σ · (1/1) — well within normal recursion.
pub const MAX_FRAMES: usize = 64;

/// Number of size classes for heap segregated free list (n=6).
pub const N_SIZE_CLASSES: usize = 6;

/// Size-class boundaries: 16, 32, 64, 128, 256, 512 bytes.
/// Large allocations (>512B) use the overflow list.
const SIZE_CLASSES: [usize; N_SIZE_CLASSES] = [16, 32, 64, 128, 256, 512];

/// Heap allocation header (stored just before each live allocation).
/// Holds the raw aligned size so `free(offset)` can recover it without
/// a HashMap lookup.
const HEADER_SIZE: usize = 8;

// ── Stack Region ────────────────────────────────────────────

/// A stack frame tracking a function call's local allocations.
/// Name stored as a string-pool index (u32) for cache-friendly frames.
#[derive(Debug, Clone, Copy)]
pub struct StackFrame {
    pub name_id: u32, // index into string pool (u32::MAX = anonymous)
    pub base: usize,  // byte offset where this frame starts
}

impl StackFrame {
    pub const ANON: u32 = u32::MAX;
}

/// LIFO stack region — push/pop with frame management.
/// Uses a fixed-size frame array (no Vec allocation on push_frame).
pub struct StackRegion {
    data: Vec<u8>,
    capacity: usize,
    sp: usize,              // stack pointer (next free byte)
    generation: u64,        // bumped on each free_to (cache invalidation hint)
    frames: [StackFrame; MAX_FRAMES],
    frame_top: usize,       // number of live frames
}

impl StackRegion {
    fn new(capacity: usize) -> Self {
        Self {
            data: vec![0u8; capacity],
            capacity,
            sp: 0,
            generation: 0,
            frames: [StackFrame { name_id: StackFrame::ANON, base: 0 }; MAX_FRAMES],
            frame_top: 0,
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
        self.generation = self.generation.wrapping_add(1);
    }

    /// Push a new call frame (using a string-pool index for the name).
    pub fn push_frame_id(&mut self, name_id: u32) {
        if self.frame_top >= MAX_FRAMES {
            // Silently drop on overflow — mirrors original behaviour where
            // Vec would allocate indefinitely. In practice 64 frames is huge.
            return;
        }
        self.frames[self.frame_top] = StackFrame { name_id, base: self.sp };
        self.frame_top += 1;
    }

    /// Pop the top call frame and free its stack space. Returns the popped frame.
    pub fn pop_frame(&mut self) -> Option<StackFrame> {
        if self.frame_top == 0 {
            return None;
        }
        self.frame_top -= 1;
        let frame = self.frames[self.frame_top];
        self.sp = frame.base;
        self.generation = self.generation.wrapping_add(1);
        Some(frame)
    }

    pub fn used(&self) -> usize { self.sp }
    pub fn capacity(&self) -> usize { self.capacity }
    pub fn frame_depth(&self) -> usize { self.frame_top }
    pub fn generation(&self) -> u64 { self.generation }

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

/// Heap region with header-based size tracking and 6 segregated free lists.
/// No HashMap. free(offset) reads an 8-byte header at offset-8.
pub struct HeapRegion {
    data: Vec<u8>,
    capacity: usize,
    /// Next bump offset (head of the unexplored region).
    bump: usize,
    /// Segregated free lists — one per size class.
    /// Each entry stores (user_offset, aligned_size).
    size_class_lists: [Vec<(usize, usize)>; N_SIZE_CLASSES],
    /// Overflow list for allocations > 512B.
    large_list: Vec<(usize, usize)>,
    used: usize,
    live_allocs: usize,
}

impl HeapRegion {
    fn new(capacity: usize) -> Self {
        Self {
            data: vec![0u8; capacity],
            capacity,
            bump: 0,
            size_class_lists: Default::default(),
            large_list: Vec::new(),
            used: 0,
            live_allocs: 0,
        }
    }

    /// Map an aligned size to its size-class index, or None if it's "large".
    #[inline]
    fn size_class(aligned: usize) -> Option<usize> {
        for (i, &cap) in SIZE_CLASSES.iter().enumerate() {
            if aligned <= cap {
                return Some(i);
            }
        }
        None
    }

    /// Align a user-requested size to 8 bytes (minimum granularity).
    #[inline]
    fn align8(size: usize) -> usize {
        (size + 7) & !7
    }

    /// Allocate `size` bytes. Returns the user-visible offset (past the header).
    pub fn alloc(&mut self, size: usize) -> Result<usize, MemoryError> {
        if size == 0 {
            return Ok(0);
        }
        let aligned = Self::align8(size);

        // 1. Try size-class free list reuse (exact fit per class).
        if let Some(cls) = Self::size_class(aligned) {
            // Scan the class bucket for a block >= aligned.
            let bucket = &mut self.size_class_lists[cls];
            for i in 0..bucket.len() {
                if bucket[i].1 >= aligned {
                    let (user_off, block_sz) = bucket.swap_remove(i);
                    self.used += block_sz;
                    self.live_allocs += 1;
                    return Ok(user_off);
                }
            }
        } else {
            // Large list: first-fit.
            for i in 0..self.large_list.len() {
                if self.large_list[i].1 >= aligned {
                    let (user_off, block_sz) = self.large_list.swap_remove(i);
                    self.used += block_sz;
                    self.live_allocs += 1;
                    return Ok(user_off);
                }
            }
        }

        // 2. Bump a new block: [header: 8B][user_data: aligned B]
        let total = HEADER_SIZE + aligned;
        if self.bump + total > self.capacity {
            let free_bytes: usize = self.size_class_lists.iter()
                .flat_map(|v| v.iter())
                .chain(self.large_list.iter())
                .map(|&(_, s)| s)
                .sum::<usize>()
                + (self.capacity - self.bump);
            return Err(MemoryError::HeapExhausted {
                requested: size,
                available: free_bytes,
                budget_fraction: "1/3",
            });
        }
        let header_off = self.bump;
        let user_off = header_off + HEADER_SIZE;
        self.bump += total;

        // Write the size header (little-endian u64).
        let hdr_bytes = (aligned as u64).to_le_bytes();
        self.data[header_off..header_off + HEADER_SIZE].copy_from_slice(&hdr_bytes);

        self.used += aligned;
        self.live_allocs += 1;
        Ok(user_off)
    }

    /// Free a previously allocated block, reading its size from the header.
    pub fn free(&mut self, user_offset: usize) -> Result<(), MemoryError> {
        if user_offset == 0 || user_offset < HEADER_SIZE || user_offset > self.capacity {
            return Err(MemoryError::InvalidFree { offset: user_offset });
        }
        let header_off = user_offset - HEADER_SIZE;
        if header_off + HEADER_SIZE > self.capacity {
            return Err(MemoryError::InvalidFree { offset: user_offset });
        }
        let mut hdr = [0u8; HEADER_SIZE];
        hdr.copy_from_slice(&self.data[header_off..header_off + HEADER_SIZE]);
        let aligned = u64::from_le_bytes(hdr) as usize;
        if aligned == 0 || aligned > self.capacity {
            return Err(MemoryError::InvalidFree { offset: user_offset });
        }
        // Return to appropriate list.
        if let Some(cls) = Self::size_class(aligned) {
            self.size_class_lists[cls].push((user_offset, aligned));
        } else {
            self.large_list.push((user_offset, aligned));
        }
        self.used = self.used.saturating_sub(aligned);
        self.live_allocs = self.live_allocs.saturating_sub(1);
        Ok(())
    }

    pub fn used(&self) -> usize { self.used }
    pub fn capacity(&self) -> usize { self.capacity }
    pub fn alloc_count(&self) -> usize { self.live_allocs }

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

    pub fn used(&self) -> usize { self.offset }
    pub fn capacity(&self) -> usize { self.capacity }
    pub fn generation(&self) -> u64 { self.generation }

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

// ── Name → Handle tracking (HashMap-free) ───────────────────

/// Packed handle: (region:2 bits | index:30 bits).
/// `index` refers to an entry in `Tracker::entries`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VarHandle(pub u32);

impl VarHandle {
    pub const NONE: VarHandle = VarHandle(u32::MAX);

    #[inline]
    pub fn new(region: MemRegion, index: u32) -> Self {
        debug_assert!(index < (1u32 << 30));
        let tag: u32 = match region {
            MemRegion::Stack => 0,
            MemRegion::Heap => 1,
            MemRegion::Arena => 2,
        };
        VarHandle((tag << 30) | index)
    }

    #[inline]
    pub fn region(self) -> MemRegion {
        match self.0 >> 30 {
            0 => MemRegion::Stack,
            1 => MemRegion::Heap,
            2 => MemRegion::Arena,
            _ => MemRegion::Arena,
        }
    }

    #[inline]
    pub fn index(self) -> u32 { self.0 & 0x3FFF_FFFF }
}

/// A single live variable-tracking record.
#[derive(Debug, Clone, Copy)]
struct VarRecord {
    name_id: u32, // string pool index; u32::MAX = tombstone (freed)
    region: MemRegion,
    offset: usize,
    size: usize,
}

/// Name → handle tracker. No HashMap: linear scan over a Vec.
/// This is only used on cold paths (debug/introspection, heap_free by name).
/// Hot alloc path never scans.
struct Tracker {
    string_pool: Vec<String>,
    entries: Vec<VarRecord>,
}

impl Tracker {
    fn new() -> Self {
        Self { string_pool: Vec::new(), entries: Vec::new() }
    }

    /// Intern a name into the string pool, returning its id.
    fn intern(&mut self, name: &str) -> u32 {
        // Linear scan — pool is small in practice.
        for (i, s) in self.string_pool.iter().enumerate() {
            if s == name { return i as u32; }
        }
        let id = self.string_pool.len() as u32;
        self.string_pool.push(name.to_string());
        id
    }

    /// Record a fresh allocation. Returns index into `entries`.
    fn record(&mut self, name: &str, region: MemRegion, offset: usize, size: usize) -> u32 {
        let name_id = self.intern(name);
        // If a previous live entry exists for this name+region, tombstone it.
        for rec in self.entries.iter_mut() {
            if rec.name_id == name_id && rec.region == region {
                rec.name_id = u32::MAX; // tombstone old record
            }
        }
        let idx = self.entries.len() as u32;
        self.entries.push(VarRecord { name_id, region, offset, size });
        idx
    }

    /// Look up a live entry by name (cold path).
    fn find(&self, name: &str) -> Option<(u32, &VarRecord)> {
        let name_id = self.string_pool.iter().position(|s| s == name)? as u32;
        for (i, rec) in self.entries.iter().enumerate() {
            if rec.name_id == name_id {
                return Some((i as u32, rec));
            }
        }
        None
    }

    fn tombstone(&mut self, idx: u32) {
        if let Some(rec) = self.entries.get_mut(idx as usize) {
            rec.name_id = u32::MAX;
        }
    }

    /// Drop entries in a given stack-frame range.
    fn drop_stack_frame(&mut self, base: usize) {
        for rec in self.entries.iter_mut() {
            if rec.region == MemRegion::Stack && rec.offset >= base {
                rec.name_id = u32::MAX;
            }
        }
    }

    /// Drop all arena entries.
    fn drop_arena(&mut self) {
        for rec in self.entries.iter_mut() {
            if rec.region == MemRegion::Arena {
                rec.name_id = u32::MAX;
            }
        }
    }
}

// ── Egyptian Memory Manager ─────────────────────────────────

/// The Egyptian Fraction memory manager.
///
/// Splits total budget using 1/2 + 1/3 + 1/6 = 1 (n=6 perfect number identity).
/// The Egyptian Fraction memory manager — **lazy initialization**.
///
/// Splits total budget using 1/2 + 1/3 + 1/6 = 1 (n=6 perfect number identity).
///
/// Regions are allocated on first use, not at construction. This saves ~64 MB RSS
/// for simple programs (e.g. fib(30)) that only use value types and never trigger
/// the memory tracker.
pub struct EgyptianMemory {
    total_budget: usize,
    /// Lazy-initialized regions. `None` until first allocation in that region.
    stack: Option<Box<StackRegion>>,
    heap: Option<Box<HeapRegion>>,
    arena: Option<Box<ArenaRegion>>,
    total_allocs: u64,
    tracker: Tracker,
}

impl EgyptianMemory {
    /// Create a new memory manager with the given total budget in bytes.
    /// Regions are NOT allocated until first use (lazy init).
    pub fn new(total_budget: usize) -> Self {
        Self {
            total_budget,
            stack: None,
            heap: None,
            arena: None,
            total_allocs: 0,
            tracker: Tracker::new(),
        }
    }

    /// Create an eagerly-initialized memory manager (for tests that need immediate allocation).
    pub fn new_eager(total_budget: usize) -> Self {
        let stack_cap = total_budget / EGYPTIAN_STACK;
        let heap_cap = total_budget / EGYPTIAN_HEAP;
        let arena_cap = total_budget / EGYPTIAN_ARENA;
        Self {
            total_budget,
            stack: Some(Box::new(StackRegion::new(stack_cap))),
            heap: Some(Box::new(HeapRegion::new(heap_cap))),
            arena: Some(Box::new(ArenaRegion::new(arena_cap))),
            total_allocs: 0,
            tracker: Tracker::new(),
        }
    }

    /// Create with default 64 MB budget (lazy).
    pub fn with_default_budget() -> Self {
        Self::new(DEFAULT_BUDGET)
    }

    /// Ensure the stack region is initialized, returning a mutable reference.
    #[inline]
    fn ensure_stack(&mut self) -> &mut StackRegion {
        if self.stack.is_none() {
            let cap = self.total_budget / EGYPTIAN_STACK;
            self.stack = Some(Box::new(StackRegion::new(cap)));
        }
        self.stack.as_mut().unwrap()
    }

    /// Ensure the heap region is initialized, returning a mutable reference.
    #[inline]
    fn ensure_heap(&mut self) -> &mut HeapRegion {
        if self.heap.is_none() {
            let cap = self.total_budget / EGYPTIAN_HEAP;
            self.heap = Some(Box::new(HeapRegion::new(cap)));
        }
        self.heap.as_mut().unwrap()
    }

    /// Ensure the arena region is initialized, returning a mutable reference.
    #[inline]
    fn ensure_arena(&mut self) -> &mut ArenaRegion {
        if self.arena.is_none() {
            let cap = self.total_budget / EGYPTIAN_ARENA;
            self.arena = Some(Box::new(ArenaRegion::new(cap)));
        }
        self.arena.as_mut().unwrap()
    }

    // ── Stack operations ────────────────────────────────────

    /// Allocate on the stack (for local value-type variables).
    pub fn stack_alloc(&mut self, name: &str, size: usize) -> Result<usize, MemoryError> {
        let offset = self.ensure_stack().alloc(size)?;
        self.total_allocs += 1;
        self.tracker.record(name, MemRegion::Stack, offset, size);
        Ok(offset)
    }

    /// Push a new call frame onto the stack.
    pub fn push_frame(&mut self, name: &str) {
        let name_id = self.tracker.intern(name);
        self.ensure_stack().push_frame_id(name_id);
    }

    /// Pop the top call frame, freeing stack space and removing tracked vars.
    pub fn pop_frame(&mut self) {
        if let Some(ref mut stack) = self.stack {
            if let Some(frame) = stack.pop_frame() {
                self.tracker.drop_stack_frame(frame.base);
            }
        }
    }

    // ── Heap operations ─────────────────────────────────────

    /// Allocate on the heap (for dynamic objects: structs, arrays, closures).
    pub fn heap_alloc(&mut self, name: &str, size: usize) -> Result<usize, MemoryError> {
        match self.ensure_heap().alloc(size) {
            Ok(offset) => {
                self.total_allocs += 1;
                self.tracker.record(name, MemRegion::Heap, offset, size);
                Ok(offset)
            }
            Err(e) => {
                if let Some(ref mut arena) = self.arena {
                    if arena.used() > 0 {
                        arena.reset();
                    }
                }
                Err(e)
            }
        }
    }

    /// Free a heap allocation by variable name.
    pub fn heap_free(&mut self, name: &str) -> Result<(), MemoryError> {
        let (idx, offset) = match self.tracker.find(name) {
            Some((i, rec)) if rec.region == MemRegion::Heap => (i, rec.offset),
            _ => return Ok(()), // silently ignore non-heap / unknown
        };
        self.tracker.tombstone(idx);
        self.ensure_heap().free(offset)
    }

    // ── Arena operations ────────────────────────────────────

    /// Allocate in the arena (for temporaries, string ops, intermediate results).
    pub fn arena_alloc(&mut self, size: usize) -> Result<usize, MemoryError> {
        match self.ensure_arena().alloc(size) {
            Ok(offset) => {
                self.total_allocs += 1;
                Ok(offset)
            }
            Err(_) => {
                self.ensure_arena().reset();
                let offset = self.ensure_arena().alloc(size)?;
                self.total_allocs += 1;
                Ok(offset)
            }
        }
    }

    /// Manually reset the arena (advanced use).
    pub fn arena_reset(&mut self) {
        if let Some(ref mut arena) = self.arena {
            arena.reset();
        }
        self.tracker.drop_arena();
    }

    // ── Region query ────────────────────────────────────────

    /// Get the region where a variable is stored.
    pub fn get_region(&self, name: &str) -> Option<MemRegion> {
        self.tracker.find(name).map(|(_, rec)| rec.region)
    }

    // ── Stats ───────────────────────────────────────────────

    pub fn stats(&self) -> MemoryStats {
        MemoryStats {
            stack_used: self.stack.as_ref().map_or(0, |s| s.used()),
            stack_capacity: self.stack.as_ref().map_or(self.total_budget / EGYPTIAN_STACK, |s| s.capacity()),
            heap_used: self.heap.as_ref().map_or(0, |h| h.used()),
            heap_capacity: self.heap.as_ref().map_or(self.total_budget / EGYPTIAN_HEAP, |h| h.capacity()),
            heap_allocs: self.heap.as_ref().map_or(0, |h| h.alloc_count()),
            arena_used: self.arena.as_ref().map_or(0, |a| a.used()),
            arena_capacity: self.arena.as_ref().map_or(self.total_budget / EGYPTIAN_ARENA, |a| a.capacity()),
            arena_resets: self.arena.as_ref().map_or(0, |a| a.generation()),
            total_budget: self.total_budget,
            total_allocs: self.total_allocs,
            stack_frame_depth: self.stack.as_ref().map_or(0, |s| s.frame_depth()),
        }
    }

    pub fn total_budget(&self) -> usize {
        self.total_budget
    }

    /// Write bytes to a region at a given offset.
    pub fn write(&mut self, region: MemRegion, offset: usize, bytes: &[u8]) {
        match region {
            MemRegion::Stack => self.ensure_stack().write(offset, bytes),
            MemRegion::Heap => self.ensure_heap().write(offset, bytes),
            MemRegion::Arena => self.ensure_arena().write(offset, bytes),
        }
    }

    /// Read bytes from a region at a given offset.
    pub fn read(&self, region: MemRegion, offset: usize, len: usize) -> &[u8] {
        match region {
            MemRegion::Stack => self.stack.as_ref().expect("stack not initialized").read(offset, len),
            MemRegion::Heap => self.heap.as_ref().expect("heap not initialized").read(offset, len),
            MemRegion::Arena => self.arena.as_ref().expect("arena not initialized").read(offset, len),
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
        Value::Fn(_) => 64,
        Value::BuiltinFn(_) => 24,
        Value::Struct(_, fields) => 48 + fields.len() * 40,
        Value::Lambda(lam) => 64 + lam.2.len() * 40,
        Value::Map(m) => 48 + m.len() * 40,
        Value::Error(msg) => 24 + msg.len(),
        Value::EnumVariant(ev) => 48 + ev.2.as_ref().map(|d| estimate_value_size(d)).unwrap_or(0),
        Value::Intent(fields) => 48 + fields.len() * 40,
        #[cfg(not(target_arch = "wasm32"))]
        Value::Sender(_) => 32,
        #[cfg(not(target_arch = "wasm32"))]
        Value::Receiver(_) => 32,
        Value::Future(_) => 32,
        Value::Set(_) => 48,
        #[cfg(not(target_arch = "wasm32"))]
        Value::TcpListener(_) => 32,
        #[cfg(not(target_arch = "wasm32"))]
        Value::TcpStream(_) => 32,
        Value::EffectRequest(er) => 48 + er.2.len() * 16,
        Value::TraitObject(to) => 48 + estimate_value_size(&to.value),
        #[cfg(not(target_arch = "wasm32"))]
        Value::AsyncFuture(_) => 32,
        Value::Atomic(_) => 16,
        Value::Pointer(_) => 8,
        Value::BigInt(b) => 32 + (b.bits() as usize / 8),
        Value::Tensor(t) => 24 + t.data.len() * 8,
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
        Value::Lambda(_) | Value::Fn(_) |
        Value::Set(_) | Value::Intent(_) |
        Value::Future(_) => MemRegion::Heap,
        #[cfg(not(target_arch = "wasm32"))]
        Value::Sender(_) | Value::Receiver(_) => MemRegion::Heap,
        #[cfg(not(target_arch = "wasm32"))]
        Value::TcpListener(_) | Value::TcpStream(_) => MemRegion::Heap,

        // Small/temporary → Arena
        Value::Str(_) | Value::Tuple(_) | Value::BuiltinFn(_) |
        Value::Error(_) | Value::EnumVariant(_) |
        Value::EffectRequest(_) | Value::TraitObject(_) => MemRegion::Arena,
        #[cfg(not(target_arch = "wasm32"))]
        Value::AsyncFuture(_) => MemRegion::Heap,
        Value::Atomic(_) => MemRegion::Heap,
        Value::Pointer(_) => MemRegion::Stack,
        Value::BigInt(_) => MemRegion::Heap,
        Value::Tensor(_) => MemRegion::Heap,
    }
}

// ── Tests ───────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    #[test]
    fn test_egyptian_fraction_identity() {
        // 1/2 + 1/3 + 1/6 = 1
        let total = 600; // divisible by 6 for clean math
        let mem = EgyptianMemory::new_eager(total);
        let stats = mem.stats();
        assert_eq!(stats.stack_capacity, 300); // 1/2
        assert_eq!(stats.heap_capacity, 200);  // 1/3
        assert_eq!(stats.arena_capacity, 100); // 1/6
        assert_eq!(stats.stack_capacity + stats.heap_capacity + stats.arena_capacity, total);
    }

    #[test]
    fn test_stack_alloc_and_frame() {
        let mut mem = EgyptianMemory::new_eager(600);
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
        let mut mem = EgyptianMemory::new_eager(600);
        let off = mem.heap_alloc("arr", 32).unwrap();
        assert!(off < 200);
        assert!(mem.stats().heap_used > 0);
        assert_eq!(mem.get_region("arr"), Some(MemRegion::Heap));

        mem.heap_free("arr").unwrap();
        assert_eq!(mem.stats().heap_used, 0);
    }

    #[test]
    fn test_arena_bump_and_reset() {
        let mut mem = EgyptianMemory::new_eager(600);
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
        let mut mem = EgyptianMemory::new_eager(600); // stack = 300
        let result = mem.stack_alloc("big", 400);
        assert!(result.is_err());
        if let Err(MemoryError::StackOverflow { budget_fraction, .. }) = result {
            assert_eq!(budget_fraction, "1/2");
        }
    }

    #[test]
    fn test_heap_exhaustion_error() {
        let mut mem = EgyptianMemory::new_eager(600); // heap = 200
        let result = mem.heap_alloc("big", 300);
        assert!(result.is_err());
        if let Err(MemoryError::HeapExhausted { budget_fraction, .. }) = result {
            assert_eq!(budget_fraction, "1/3");
        }
    }

    #[test]
    fn test_arena_auto_reset_on_overflow() {
        let mut mem = EgyptianMemory::new_eager(600); // arena = 100
        // Fill arena
        mem.arena_alloc(90).unwrap();
        // This should auto-reset and succeed
        let result = mem.arena_alloc(20);
        assert!(result.is_ok());
        assert_eq!(mem.stats().arena_resets, 1);
    }

    #[test]
    fn test_total_allocs_tracking() {
        let mut mem = EgyptianMemory::new_eager(600);
        mem.stack_alloc("a", 8).unwrap();
        mem.heap_alloc("b", 16).unwrap();
        mem.arena_alloc(8).unwrap();
        assert_eq!(mem.stats().total_allocs, 3);
    }

    #[test]
    fn test_write_and_read() {
        let mut mem = EgyptianMemory::new_eager(600);
        let off = mem.stack_alloc("x", 8).unwrap();
        mem.write(MemRegion::Stack, off, &42i64.to_le_bytes());
        let bytes = mem.read(MemRegion::Stack, off, 8);
        let val = i64::from_le_bytes(bytes.try_into().unwrap());
        assert_eq!(val, 42);
    }

    #[test]
    fn test_heap_coalesce() {
        // With segregated free lists we no longer coalesce across the bump
        // arena, but freed blocks are reused within their size class.
        let mut mem = EgyptianMemory::new_eager(6000); // heap = 2000
        let _off1 = mem.heap_alloc("a", 8).unwrap();
        let _off2 = mem.heap_alloc("b", 8).unwrap();
        let _off3 = mem.heap_alloc("c", 8).unwrap();
        mem.heap_free("a").unwrap();
        mem.heap_free("c").unwrap();
        mem.heap_free("b").unwrap();
        assert_eq!(mem.stats().heap_used, 0);
        // Should be able to allocate from the same size class (8B aligned).
        let result = mem.heap_alloc("big", 8);
        assert!(result.is_ok());
    }

    #[test]
    fn test_classify_region() {
        use crate::env::Value;
        assert_eq!(classify_region(&Value::Int(42)), MemRegion::Stack);
        assert_eq!(classify_region(&Value::Float(3.14)), MemRegion::Stack);
        assert_eq!(classify_region(&Value::Bool(true)), MemRegion::Stack);
        assert_eq!(classify_region(&Value::Array(vec![])), MemRegion::Heap);
        assert_eq!(classify_region(&Value::Map(Box::new(HashMap::new()))), MemRegion::Heap);
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
        let mem = EgyptianMemory::new_eager(600);
        let output = format!("{}", mem.stats());
        assert!(output.contains("Egyptian Fraction"));
        assert!(output.contains("1/2"));
        assert!(output.contains("1/3"));
        assert!(output.contains("1/6"));
    }

    // ── New tests for HashMap-free invariants ──

    #[test]
    fn test_var_handle_pack_unpack() {
        let h = VarHandle::new(MemRegion::Heap, 12345);
        assert_eq!(h.region(), MemRegion::Heap);
        assert_eq!(h.index(), 12345);
        let h2 = VarHandle::new(MemRegion::Arena, 0);
        assert_eq!(h2.region(), MemRegion::Arena);
        assert_eq!(h2.index(), 0);
        let h3 = VarHandle::new(MemRegion::Stack, (1 << 30) - 1);
        assert_eq!(h3.region(), MemRegion::Stack);
        assert_eq!(h3.index(), (1 << 30) - 1);
    }

    #[test]
    fn test_heap_size_class_reuse() {
        let mut mem = EgyptianMemory::new_eager(60_000);
        // Small (16B class)
        let o1 = mem.heap_alloc("s1", 10).unwrap();
        mem.heap_free("s1").unwrap();
        let o2 = mem.heap_alloc("s2", 10).unwrap();
        assert_eq!(o1, o2, "same size class should reuse freed slot");
    }

    #[test]
    fn test_heap_stress_reuse() {
        let mut mem = EgyptianMemory::new_eager(600_000);
        for i in 0..1000 {
            let name = format!("v{}", i % 8);
            let _ = mem.heap_alloc(&name, 64).unwrap();
            if i % 2 == 1 {
                let _ = mem.heap_free(&name);
            }
        }
        let s = mem.stats();
        assert!(s.heap_used <= s.heap_capacity);
    }

    #[test]
    fn test_stack_frames_max_depth() {
        let mut mem = EgyptianMemory::new_eager(60_000);
        for i in 0..MAX_FRAMES {
            mem.push_frame(&format!("f{}", i));
            mem.stack_alloc(&format!("x{}", i), 8).unwrap();
        }
        assert_eq!(mem.stats().stack_frame_depth, MAX_FRAMES);
        for _ in 0..MAX_FRAMES {
            mem.pop_frame();
        }
        assert_eq!(mem.stats().stack_frame_depth, 0);
        assert_eq!(mem.stats().stack_used, 0);
    }
}

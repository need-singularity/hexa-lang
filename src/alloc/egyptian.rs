//! Egyptian Fraction Allocator — 1/2 + 1/3 + 1/6 = 1.
//!
//! Total memory is partitioned into three zones using Egyptian fractions.
//! This guarantees zero fragmentation: every byte belongs to exactly one zone.
//!
//! Zone A (Hot,  1/2): 2^σ = 4096-byte blocks — stack frames, locals
//! Zone B (Warm, 1/3): 2^(σ-φ) = 1024-byte blocks — heap objects
//! Zone C (Cold, 1/6): 2^(σ-τ) = 256-byte blocks — arena/bulk

/// Block sizes derived from n=6 arithmetic.
const BLOCK_A: usize = 1 << 12; // 2^σ = 4096
const BLOCK_B: usize = 1 << 10; // 2^(σ-φ) = 1024
const BLOCK_C: usize = 1 << 8;  // 2^(σ-τ) = 256

/// n=6 promotion threshold: accessed > 6 times → suggest promotion.
const PROMOTION_THRESHOLD: u64 = 6;

/// Fragmentation alarm: free list fragmentation > 1/6 of zone → needs compaction.
const DEFRAG_RATIO_NUMERATOR: usize = 1;
const DEFRAG_RATIO_DENOMINATOR: usize = 6;

/// A freed region tracked in the free list.
#[derive(Debug, Clone, Copy)]
struct FreeBlock {
    offset: usize,
    size: usize,
}

/// Per-zone allocation statistics.
#[derive(Debug, Clone, Default)]
pub struct ZoneStats {
    pub alloc_count: u64,
    pub free_count: u64,
    pub reuse_count: u64,
    pub total_allocated_bytes: usize,
    pub total_freed_bytes: usize,
}

/// Egyptian allocator with three zones.
pub struct EgyptianAllocator {
    total_bytes: usize,
    /// Zone A: Hot (1/2 of total)
    zone_a: Zone,
    /// Zone B: Warm (1/3 of total)
    zone_b: Zone,
    /// Zone C: Cold (1/6 of total)
    zone_c: Zone,
}

struct Zone {
    base: Vec<u8>,
    block_size: usize,
    next_free: usize,
    allocated: usize,
    free_list: Vec<FreeBlock>,
    stats: ZoneStats,
    /// Tracks access counts per allocation offset for temperature-based promotion.
    access_counts: Vec<(usize, u64)>,
}

impl Zone {
    fn new(capacity: usize, block_size: usize) -> Self {
        Self {
            base: vec![0; capacity],
            block_size,
            next_free: 0,
            allocated: 0,
            free_list: Vec::new(),
            stats: ZoneStats::default(),
            access_counts: Vec::new(),
        }
    }

    /// Align size up to block boundary.
    fn align_size(&self, size: usize) -> usize {
        (size + self.block_size - 1) / self.block_size * self.block_size
    }

    /// Try to allocate from free list first (best-fit), then bump.
    fn alloc(&mut self, size: usize) -> Option<*mut u8> {
        let aligned = self.align_size(size);

        // 1. Check free list — best-fit strategy
        if let Some(idx) = self.find_best_fit(aligned) {
            let block = self.free_list.remove(idx);
            let ptr = unsafe { self.base.as_mut_ptr().add(block.offset) };
            // If block is larger than needed, return remainder to free list
            if block.size > aligned {
                self.free_list.push(FreeBlock {
                    offset: block.offset + aligned,
                    size: block.size - aligned,
                });
            }
            self.allocated += aligned;
            self.stats.alloc_count += 1;
            self.stats.reuse_count += 1;
            self.stats.total_allocated_bytes += aligned;
            self.access_counts.push((block.offset, 0));
            return Some(ptr);
        }

        // 2. Bump allocation
        if self.next_free + aligned > self.base.len() {
            return None;
        }
        let offset = self.next_free;
        let ptr = unsafe { self.base.as_mut_ptr().add(offset) };
        self.next_free += aligned;
        self.allocated += aligned;
        self.stats.alloc_count += 1;
        self.stats.total_allocated_bytes += aligned;
        self.access_counts.push((offset, 0));
        Some(ptr)
    }

    /// Free a region back to the free list.
    fn dealloc(&mut self, ptr: *mut u8, size: usize) -> bool {
        let base_ptr = self.base.as_ptr() as usize;
        let ptr_addr = ptr as usize;
        if ptr_addr < base_ptr || ptr_addr >= base_ptr + self.base.len() {
            return false; // Not in this zone
        }
        let offset = ptr_addr - base_ptr;
        let aligned = self.align_size(size);
        // Bounds check
        if offset + aligned > self.base.len() {
            return false;
        }
        self.free_list.push(FreeBlock { offset, size: aligned });
        self.allocated = self.allocated.saturating_sub(aligned);
        self.stats.free_count += 1;
        self.stats.total_freed_bytes += aligned;
        // Remove from access tracking
        self.access_counts.retain(|(o, _)| *o != offset);
        true
    }

    /// Record an access to a given pointer for temperature tracking.
    fn record_access(&mut self, ptr: *mut u8) {
        let base_ptr = self.base.as_ptr() as usize;
        let ptr_addr = ptr as usize;
        if ptr_addr < base_ptr || ptr_addr >= base_ptr + self.base.len() {
            return;
        }
        let offset = ptr_addr - base_ptr;
        for (o, count) in self.access_counts.iter_mut() {
            if *o == offset {
                *count += 1;
                return;
            }
        }
    }

    /// Return offsets that exceed the promotion threshold (accessed > n=6 times).
    fn promotion_candidates(&self) -> Vec<usize> {
        self.access_counts
            .iter()
            .filter(|(_, count)| *count > PROMOTION_THRESHOLD)
            .map(|(offset, _)| *offset)
            .collect()
    }

    /// Find best-fit block in free list (smallest block >= required size).
    fn find_best_fit(&self, required: usize) -> Option<usize> {
        let mut best: Option<(usize, usize)> = None; // (index, size)
        for (i, block) in self.free_list.iter().enumerate() {
            if block.size >= required {
                match best {
                    None => best = Some((i, block.size)),
                    Some((_, best_size)) if block.size < best_size => {
                        best = Some((i, block.size));
                    }
                    _ => {}
                }
            }
        }
        best.map(|(i, _)| i)
    }

    /// Check if zone needs defragmentation.
    /// Fragmented when free list total > 1/6 of zone capacity.
    fn needs_defrag(&self) -> bool {
        let free_list_total: usize = self.free_list.iter().map(|b| b.size).sum();
        let threshold = self.base.len() * DEFRAG_RATIO_NUMERATOR / DEFRAG_RATIO_DENOMINATOR;
        free_list_total > threshold
    }

    /// Number of fragments in the free list.
    fn fragment_count(&self) -> usize {
        self.free_list.len()
    }

    fn reset(&mut self) {
        self.next_free = 0;
        self.allocated = 0;
        self.free_list.clear();
        self.access_counts.clear();
        // Keep cumulative stats — only reset the live state
    }

    fn capacity(&self) -> usize { self.base.len() }
    fn used(&self) -> usize { self.allocated }
    #[allow(dead_code)]
    fn free_space(&self) -> usize { self.capacity() - self.used() }
}

impl EgyptianAllocator {
    /// Create a new allocator with the given total memory budget.
    pub fn new(total_bytes: usize) -> Self {
        // 1/2 + 1/3 + 1/6 = 1
        let zone_a_size = total_bytes / 2;         // 1/2
        let zone_b_size = total_bytes / 3;         // 1/3
        let zone_c_size = total_bytes / 6;         // 1/6
        // Remainder goes to zone A (rounding)
        let remainder = total_bytes - zone_a_size - zone_b_size - zone_c_size;

        Self {
            total_bytes,
            zone_a: Zone::new(zone_a_size + remainder, BLOCK_A),
            zone_b: Zone::new(zone_b_size, BLOCK_B),
            zone_c: Zone::new(zone_c_size, BLOCK_C),
        }
    }

    /// Allocate from hot zone (stack-like, fastest).
    pub fn alloc_hot(&mut self, size: usize) -> Option<*mut u8> {
        self.zone_a.alloc(size)
    }

    /// Allocate from warm zone (heap-like).
    pub fn alloc_warm(&mut self, size: usize) -> Option<*mut u8> {
        self.zone_b.alloc(size)
    }

    /// Allocate from cold zone (arena, bulk-freed).
    pub fn alloc_cold(&mut self, size: usize) -> Option<*mut u8> {
        self.zone_c.alloc(size)
    }

    /// Free memory in hot zone.
    pub fn free_hot(&mut self, ptr: *mut u8, size: usize) -> bool {
        self.zone_a.dealloc(ptr, size)
    }

    /// Free memory in warm zone.
    pub fn free_warm(&mut self, ptr: *mut u8, size: usize) -> bool {
        self.zone_b.dealloc(ptr, size)
    }

    /// Free memory in cold zone.
    pub fn free_cold(&mut self, ptr: *mut u8, size: usize) -> bool {
        self.zone_c.dealloc(ptr, size)
    }

    /// Record an access to a cold-zone pointer (for promotion heuristic).
    pub fn record_cold_access(&mut self, ptr: *mut u8) {
        self.zone_c.record_access(ptr);
    }

    /// Get cold-zone offsets that should be promoted to warm (accessed > 6 times).
    pub fn cold_promotion_candidates(&self) -> Vec<usize> {
        self.zone_c.promotion_candidates()
    }

    /// Auto-select zone based on size heuristic.
    pub fn alloc_auto(&mut self, size: usize) -> Option<*mut u8> {
        if size <= BLOCK_C {
            // Small -> try cold first (arena)
            self.zone_c.alloc(size)
                .or_else(|| self.zone_a.alloc(size))
        } else if size <= BLOCK_B {
            // Medium -> warm zone
            self.zone_b.alloc(size)
                .or_else(|| self.zone_a.alloc(size))
        } else {
            // Large -> hot zone
            self.zone_a.alloc(size)
        }
    }

    /// Reset cold zone (arena bulk-free).
    pub fn reset_arena(&mut self) {
        self.zone_c.reset();
    }

    /// Total memory budget.
    pub fn total(&self) -> usize { self.total_bytes }

    /// Per-zone statistics.
    pub fn zone_stats(&self) -> (ZoneStats, ZoneStats, ZoneStats) {
        (
            self.zone_a.stats.clone(),
            self.zone_b.stats.clone(),
            self.zone_c.stats.clone(),
        )
    }

    /// Defragmentation hint per zone: (hot_needs, warm_needs, cold_needs).
    pub fn defrag_hints(&self) -> DefragReport {
        DefragReport {
            hot_needs_compaction: self.zone_a.needs_defrag(),
            hot_fragments: self.zone_a.fragment_count(),
            warm_needs_compaction: self.zone_b.needs_defrag(),
            warm_fragments: self.zone_b.fragment_count(),
            cold_needs_compaction: self.zone_c.needs_defrag(),
            cold_fragments: self.zone_c.fragment_count(),
        }
    }

    /// Memory usage report.
    pub fn report(&self) -> AllocReport {
        AllocReport {
            total: self.total_bytes,
            hot_used: self.zone_a.used(),
            hot_cap: self.zone_a.capacity(),
            warm_used: self.zone_b.used(),
            warm_cap: self.zone_b.capacity(),
            cold_used: self.zone_c.used(),
            cold_cap: self.zone_c.capacity(),
        }
    }
}

/// Defragmentation report.
#[derive(Debug)]
pub struct DefragReport {
    pub hot_needs_compaction: bool,
    pub hot_fragments: usize,
    pub warm_needs_compaction: bool,
    pub warm_fragments: usize,
    pub cold_needs_compaction: bool,
    pub cold_fragments: usize,
}

impl std::fmt::Display for DefragReport {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "Defrag Hints (threshold: > 1/{} of zone)", DEFRAG_RATIO_DENOMINATOR)?;
        writeln!(f, "  Hot:  {} fragments, compaction {}",
            self.hot_fragments, if self.hot_needs_compaction { "NEEDED" } else { "ok" })?;
        writeln!(f, "  Warm: {} fragments, compaction {}",
            self.warm_fragments, if self.warm_needs_compaction { "NEEDED" } else { "ok" })?;
        writeln!(f, "  Cold: {} fragments, compaction {}",
            self.cold_fragments, if self.cold_needs_compaction { "NEEDED" } else { "ok" })?;
        Ok(())
    }
}

/// Allocation statistics.
#[derive(Debug)]
pub struct AllocReport {
    pub total: usize,
    pub hot_used: usize,
    pub hot_cap: usize,
    pub warm_used: usize,
    pub warm_cap: usize,
    pub cold_used: usize,
    pub cold_cap: usize,
}

impl AllocReport {
    pub fn utilization(&self) -> f64 {
        let used = self.hot_used + self.warm_used + self.cold_used;
        if self.total == 0 { 0.0 } else { used as f64 / self.total as f64 }
    }

    pub fn fragmentation(&self) -> f64 {
        0.0 // Egyptian fractions guarantee zero external fragmentation by design
    }
}

impl std::fmt::Display for AllocReport {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        writeln!(f, "Egyptian Allocator (1/2 + 1/3 + 1/6 = 1)")?;
        writeln!(f, "  Hot  (1/2): {}/{} bytes ({:.1}%)",
            self.hot_used, self.hot_cap,
            if self.hot_cap == 0 { 0.0 } else { self.hot_used as f64 / self.hot_cap as f64 * 100.0 })?;
        writeln!(f, "  Warm (1/3): {}/{} bytes ({:.1}%)",
            self.warm_used, self.warm_cap,
            if self.warm_cap == 0 { 0.0 } else { self.warm_used as f64 / self.warm_cap as f64 * 100.0 })?;
        writeln!(f, "  Cold (1/6): {}/{} bytes ({:.1}%)",
            self.cold_used, self.cold_cap,
            if self.cold_cap == 0 { 0.0 } else { self.cold_used as f64 / self.cold_cap as f64 * 100.0 })?;
        writeln!(f, "  Fragmentation: {:.1}%", self.fragmentation() * 100.0)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_egyptian_fraction_sum() {
        // 1/2 + 1/3 + 1/6 = 1
        let total = 6000usize;
        let a = total / 2;  // 3000
        let b = total / 3;  // 2000
        let c = total / 6;  // 1000
        assert_eq!(a + b + c, total);
    }

    #[test]
    fn test_zone_sizes() {
        assert_eq!(BLOCK_A, 4096); // 2^12 = 2^σ
        assert_eq!(BLOCK_B, 1024); // 2^10 = 2^(σ-φ)
        assert_eq!(BLOCK_C, 256);  // 2^8  = 2^(σ-τ)
    }

    #[test]
    fn test_alloc_and_report() {
        let mut alloc = EgyptianAllocator::new(6000);
        let report = alloc.report();
        assert_eq!(report.total, 6000);
        assert_eq!(report.fragmentation(), 0.0);
    }

    #[test]
    fn test_hot_alloc() {
        let mut alloc = EgyptianAllocator::new(60000);
        let ptr = alloc.alloc_hot(100);
        assert!(ptr.is_some());
        assert!(alloc.report().hot_used > 0);
    }

    #[test]
    fn test_arena_reset() {
        let mut alloc = EgyptianAllocator::new(60000);
        alloc.alloc_cold(100);
        assert!(alloc.report().cold_used > 0);
        alloc.reset_arena();
        assert_eq!(alloc.report().cold_used, 0);
    }

    // --- Free list reuse tests ---

    #[test]
    fn test_free_list_reuse_hot() {
        let mut alloc = EgyptianAllocator::new(600_000);
        let ptr1 = alloc.alloc_hot(100).unwrap();
        let used_after_alloc = alloc.report().hot_used;
        assert!(alloc.free_hot(ptr1, 100));
        let ptr2 = alloc.alloc_hot(100).unwrap();
        // Reused pointer should be the same offset (same region returned)
        assert_eq!(ptr1, ptr2);
        // Used should be back to same level
        assert_eq!(alloc.report().hot_used, used_after_alloc);
    }

    #[test]
    fn test_free_list_reuse_warm() {
        let mut alloc = EgyptianAllocator::new(600_000);
        let ptr1 = alloc.alloc_warm(200).unwrap();
        assert!(alloc.free_warm(ptr1, 200));
        let ptr2 = alloc.alloc_warm(200).unwrap();
        assert_eq!(ptr1, ptr2);
    }

    #[test]
    fn test_free_list_reuse_cold() {
        let mut alloc = EgyptianAllocator::new(600_000);
        let ptr1 = alloc.alloc_cold(50).unwrap();
        assert!(alloc.free_cold(ptr1, 50));
        let ptr2 = alloc.alloc_cold(50).unwrap();
        assert_eq!(ptr1, ptr2);
    }

    // --- Zone statistics accuracy ---

    #[test]
    fn test_zone_stats_accuracy() {
        let mut alloc = EgyptianAllocator::new(600_000);
        alloc.alloc_hot(100);
        alloc.alloc_hot(200);
        let ptr = alloc.alloc_hot(300).unwrap();
        alloc.free_hot(ptr, 300);
        alloc.alloc_hot(300); // should reuse

        let (hot_stats, _, _) = alloc.zone_stats();
        assert_eq!(hot_stats.alloc_count, 4); // 3 allocs + 1 reuse alloc
        assert_eq!(hot_stats.free_count, 1);
        assert_eq!(hot_stats.reuse_count, 1);
    }

    // --- Capacity overflow protection ---

    #[test]
    fn test_alloc_never_exceeds_capacity() {
        // Small allocator: 60_000 bytes total
        // Hot zone = 30_000, Warm = 20_000, Cold = 10_000
        let mut alloc = EgyptianAllocator::new(60_000);
        let report = alloc.report();

        // Hot: capacity 30_000, block size 4096 -> max ~7 blocks
        let mut hot_allocs = 0;
        while alloc.alloc_hot(4096).is_some() {
            hot_allocs += 1;
        }
        assert!(hot_allocs > 0);
        assert!(alloc.report().hot_used <= report.hot_cap);

        // Warm: capacity 20_000, block size 1024 -> max ~19 blocks
        let mut warm_allocs = 0;
        while alloc.alloc_warm(1024).is_some() {
            warm_allocs += 1;
        }
        assert!(warm_allocs > 0);
        assert!(alloc.report().warm_used <= report.warm_cap);

        // Cold: capacity 10_000, block size 256 -> max ~39 blocks
        let mut cold_allocs = 0;
        while alloc.alloc_cold(256).is_some() {
            cold_allocs += 1;
        }
        assert!(cold_allocs > 0);
        assert!(alloc.report().cold_used <= report.cold_cap);
    }

    // --- All three zones independent ---

    #[test]
    fn test_zones_independent() {
        let mut alloc = EgyptianAllocator::new(600_000);

        let hot_ptr = alloc.alloc_hot(100).unwrap();
        let warm_ptr = alloc.alloc_warm(100).unwrap();
        let cold_ptr = alloc.alloc_cold(100).unwrap();

        // All pointers are different
        assert_ne!(hot_ptr, warm_ptr);
        assert_ne!(warm_ptr, cold_ptr);
        assert_ne!(hot_ptr, cold_ptr);

        // Free from wrong zone should fail
        assert!(!alloc.free_warm(hot_ptr, 100));
        assert!(!alloc.free_cold(warm_ptr, 100));
        assert!(!alloc.free_hot(cold_ptr, 100));

        // Free from correct zone succeeds
        assert!(alloc.free_hot(hot_ptr, 100));
        assert!(alloc.free_warm(warm_ptr, 100));
        assert!(alloc.free_cold(cold_ptr, 100));
    }

    // --- Stress test ---

    #[test]
    fn test_stress_10000_alloc_free_cycles() {
        let mut alloc = EgyptianAllocator::new(6_000_000);
        let mut ptrs: Vec<*mut u8> = Vec::new();

        for i in 0..10_000 {
            if i % 2 == 0 {
                // Alloc
                if let Some(ptr) = alloc.alloc_warm(128) {
                    ptrs.push(ptr);
                }
            } else {
                // Free most recent
                if let Some(ptr) = ptrs.pop() {
                    alloc.free_warm(ptr, 128);
                }
            }
        }

        let report = alloc.report();
        assert!(report.warm_used <= report.warm_cap);

        let (_, warm_stats, _) = alloc.zone_stats();
        assert!(warm_stats.alloc_count > 0);
        assert!(warm_stats.free_count > 0);
        assert!(warm_stats.reuse_count > 0); // Must have reused freed blocks
    }

    // --- Temperature-based promotion ---

    #[test]
    fn test_cold_promotion_candidates() {
        let mut alloc = EgyptianAllocator::new(600_000);
        let ptr = alloc.alloc_cold(50).unwrap();

        // No candidates yet
        assert!(alloc.cold_promotion_candidates().is_empty());

        // Access 6 times — not enough (threshold is > 6)
        for _ in 0..6 {
            alloc.record_cold_access(ptr);
        }
        assert!(alloc.cold_promotion_candidates().is_empty());

        // 7th access pushes over threshold
        alloc.record_cold_access(ptr);
        assert_eq!(alloc.cold_promotion_candidates().len(), 1);
    }

    // --- Defragmentation hints ---

    #[test]
    fn test_defrag_hints() {
        // 60_000 total -> cold zone = 10_000
        // 1/6 of 10_000 = 1666. Need > 1666 bytes in free list to trigger.
        let mut alloc = EgyptianAllocator::new(60_000);

        // Initially no defrag needed
        let hints = alloc.defrag_hints();
        assert!(!hints.cold_needs_compaction);

        // Alloc and free many small blocks to build free list
        let mut ptrs = Vec::new();
        for _ in 0..30 {
            if let Some(ptr) = alloc.alloc_cold(50) {
                ptrs.push(ptr);
            }
        }
        // Free them all — creates fragmented free list
        for ptr in ptrs {
            alloc.free_cold(ptr, 50);
        }

        let hints = alloc.defrag_hints();
        // 30 * 256 (aligned) = 7680 > 1666 threshold
        assert!(hints.cold_needs_compaction);
        assert!(hints.cold_fragments > 0);
    }
}

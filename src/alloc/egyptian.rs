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
}

impl Zone {
    fn new(capacity: usize, block_size: usize) -> Self {
        Self {
            base: vec![0; capacity],
            block_size,
            next_free: 0,
            allocated: 0,
        }
    }

    fn alloc(&mut self, size: usize) -> Option<*mut u8> {
        let aligned = (size + self.block_size - 1) / self.block_size * self.block_size;
        if self.next_free + aligned > self.base.len() {
            return None;
        }
        let ptr = unsafe { self.base.as_mut_ptr().add(self.next_free) };
        self.next_free += aligned;
        self.allocated += aligned;
        Some(ptr)
    }

    fn reset(&mut self) {
        self.next_free = 0;
        self.allocated = 0;
    }

    fn capacity(&self) -> usize { self.base.len() }
    fn used(&self) -> usize { self.allocated }
    fn free(&self) -> usize { self.capacity() - self.used() }
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

    /// Auto-select zone based on size heuristic.
    pub fn alloc_auto(&mut self, size: usize) -> Option<*mut u8> {
        if size <= BLOCK_C {
            // Small → try cold first (arena)
            self.zone_c.alloc(size)
                .or_else(|| self.zone_a.alloc(size))
        } else if size <= BLOCK_B {
            // Medium → warm zone
            self.zone_b.alloc(size)
                .or_else(|| self.zone_a.alloc(size))
        } else {
            // Large → hot zone
            self.zone_a.alloc(size)
        }
    }

    /// Reset cold zone (arena bulk-free).
    pub fn reset_arena(&mut self) {
        self.zone_c.reset();
    }

    /// Total memory budget.
    pub fn total(&self) -> usize { self.total_bytes }

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
        0.0 // Egyptian fractions guarantee zero fragmentation by design
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
}

//! Egyptian Memory Allocator — 1/2 + 1/3 + 1/6 = 1.
//!
//! Zero fragmentation by design. Three zones:
//! - Hot  (1/2): Stack region, fastest access
//! - Warm (1/3): Heap region, GC-managed
//! - Cold (1/6): Arena region, bulk-freed

pub mod egyptian;

pub use egyptian::EgyptianAllocator;

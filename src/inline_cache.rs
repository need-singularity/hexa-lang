//! Monomorphic inline caching for HEXA-LANG method dispatch.
//!
//! Each call site remembers the last type it saw.  If the next call is on
//! the same type, we skip the hash-map lookup in `type_methods` / `trait_impls`
//! and jump straight to the cached method body.  On a type mismatch we fall
//! back to normal dispatch and update the cache entry.
//!
//! This is a classic runtime optimisation that turns O(hash-lookup) dispatch
//! into O(1) for monomorphic sites (the vast majority in practice).

use std::collections::HashMap;

use crate::ast::Stmt;

// ── Public types ───────────────────────────────────────────────

/// A unique identifier for a call site, derived from source location.
/// We use `(line, col, method_name)` as the key so that distinct call
/// sites with the same method name get separate cache entries.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct CallSiteId {
    pub line: usize,
    pub col: usize,
    pub method: String,
}

impl CallSiteId {
    pub fn new(line: usize, col: usize, method: &str) -> Self {
        Self {
            line,
            col,
            method: method.to_string(),
        }
    }
}

/// A single inline-cache entry: remembers the last type and its resolved
/// method body so we can skip the lookup on a cache hit.
#[derive(Debug, Clone)]
pub struct InlineCacheEntry {
    /// The type name that was last dispatched here (e.g. "Point", "Vec").
    pub cached_type: String,
    /// The resolved method body: (param_names, body_stmts).
    pub cached_method: (Vec<String>, Vec<Stmt>),
    /// Hit / miss counters for profiling.
    pub hits: u64,
    pub misses: u64,
}

/// The top-level inline cache, shared across all call sites in an
/// interpreter session.
#[derive(Debug, Clone)]
pub struct InlineCache {
    entries: HashMap<CallSiteId, InlineCacheEntry>,
}

impl InlineCache {
    pub fn new() -> Self {
        Self {
            entries: HashMap::new(),
        }
    }

    /// Probe the cache for a call site.
    ///
    /// Returns `Some(&method_body)` on a cache hit (same type), or `None`
    /// on a miss.  On a miss the caller should resolve normally and then
    /// call `update()`.
    pub fn probe(
        &mut self,
        site: &CallSiteId,
        actual_type: &str,
    ) -> Option<(Vec<String>, Vec<Stmt>)> {
        if let Some(entry) = self.entries.get_mut(site) {
            if entry.cached_type == actual_type {
                entry.hits += 1;
                return Some(entry.cached_method.clone());
            }
            // Type changed — megamorphic transition.  Record the miss
            // and let the caller update.
            entry.misses += 1;
        }
        None
    }

    /// Update (or insert) a cache entry after a full method resolution.
    pub fn update(
        &mut self,
        site: CallSiteId,
        type_name: String,
        method: (Vec<String>, Vec<Stmt>),
    ) {
        let entry = self.entries.entry(site).or_insert_with(|| InlineCacheEntry {
            cached_type: String::new(),
            cached_method: (vec![], vec![]),
            hits: 0,
            misses: 0,
        });
        entry.cached_type = type_name;
        entry.cached_method = method;
    }

    /// Invalidate all entries for a given type (e.g. after hot-patching a
    /// method at runtime).
    pub fn invalidate_type(&mut self, type_name: &str) {
        self.entries.retain(|_, entry| entry.cached_type != type_name);
    }

    /// Clear the entire cache.
    pub fn clear(&mut self) {
        self.entries.clear();
    }

    /// Return total hits and misses across all call sites.
    pub fn stats(&self) -> (u64, u64) {
        let mut hits = 0u64;
        let mut misses = 0u64;
        for entry in self.entries.values() {
            hits += entry.hits;
            misses += entry.misses;
        }
        (hits, misses)
    }

    /// Number of cached call sites.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Whether the cache is empty.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

impl Default for InlineCache {
    fn default() -> Self {
        Self::new()
    }
}

// ── Tests ──────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    fn dummy_method() -> (Vec<String>, Vec<Stmt>) {
        (vec!["self".to_string()], vec![Stmt::Return(None)])
    }

    fn dummy_method_b() -> (Vec<String>, Vec<Stmt>) {
        (
            vec!["self".to_string(), "x".to_string()],
            vec![Stmt::Return(Some(crate::ast::Expr::IntLit(42)))],
        )
    }

    #[test]
    fn test_cache_miss_then_hit() {
        let mut cache = InlineCache::new();
        let site = CallSiteId::new(10, 5, "area");

        // First probe: miss (empty cache).
        assert!(cache.probe(&site, "Point").is_none());

        // Update with resolved method.
        cache.update(site.clone(), "Point".to_string(), dummy_method());

        // Second probe: hit.
        let result = cache.probe(&site, "Point");
        assert!(result.is_some());
        let (params, _body) = result.unwrap();
        assert_eq!(params, vec!["self".to_string()]);
    }

    #[test]
    fn test_type_change_causes_miss() {
        let mut cache = InlineCache::new();
        let site = CallSiteId::new(10, 5, "area");

        cache.update(site.clone(), "Point".to_string(), dummy_method());

        // Same type: hit.
        assert!(cache.probe(&site, "Point").is_some());

        // Different type: miss.
        assert!(cache.probe(&site, "Circle").is_none());
    }

    #[test]
    fn test_stats() {
        let mut cache = InlineCache::new();
        let site = CallSiteId::new(1, 1, "foo");

        cache.update(site.clone(), "A".to_string(), dummy_method());

        // 2 hits, 1 miss.
        cache.probe(&site, "A");
        cache.probe(&site, "A");
        cache.probe(&site, "B"); // miss

        let (hits, misses) = cache.stats();
        assert_eq!(hits, 2);
        assert_eq!(misses, 1);
    }

    #[test]
    fn test_invalidate_type() {
        let mut cache = InlineCache::new();
        let s1 = CallSiteId::new(1, 1, "foo");
        let s2 = CallSiteId::new(2, 1, "bar");

        cache.update(s1.clone(), "Point".to_string(), dummy_method());
        cache.update(s2.clone(), "Circle".to_string(), dummy_method_b());

        assert_eq!(cache.len(), 2);

        cache.invalidate_type("Point");
        assert_eq!(cache.len(), 1);

        // Circle entry should survive.
        assert!(cache.probe(&s2, "Circle").is_some());
    }

    #[test]
    fn test_clear() {
        let mut cache = InlineCache::new();
        cache.update(CallSiteId::new(1, 1, "a"), "X".into(), dummy_method());
        cache.update(CallSiteId::new(2, 1, "b"), "Y".into(), dummy_method());
        assert_eq!(cache.len(), 2);
        cache.clear();
        assert!(cache.is_empty());
    }

    #[test]
    fn test_separate_sites_same_method() {
        let mut cache = InlineCache::new();
        let s1 = CallSiteId::new(10, 1, "area");
        let s2 = CallSiteId::new(20, 1, "area");

        cache.update(s1.clone(), "Point".into(), dummy_method());
        cache.update(s2.clone(), "Circle".into(), dummy_method_b());

        // Each site has its own entry.
        assert!(cache.probe(&s1, "Point").is_some());
        assert!(cache.probe(&s2, "Circle").is_some());

        // Cross-type at site 1: miss.
        assert!(cache.probe(&s1, "Circle").is_none());
    }
}

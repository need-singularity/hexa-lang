//! Emergence Density (ED) — new convergence metric.
//! ED = (patterns_found × speedup) / cycles
//! where speedup = baseline_ns / current_ns (≥ 1.0 means improvement).

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct EdInput {
    pub patterns_found: usize,
    pub baseline_ns: u128,
    pub current_ns: u128,
    pub cycles: usize,
}

pub fn compute(input: EdInput) -> f64 {
    if input.cycles == 0 || input.current_ns == 0 {
        return 0.0;
    }
    let speedup = input.baseline_ns as f64 / input.current_ns as f64;
    (input.patterns_found as f64 * speedup) / input.cycles as f64
}

#[derive(Debug, Clone, Default)]
pub struct EdHistory {
    pub values: Vec<f64>,
}

impl EdHistory {
    pub fn push(&mut self, v: f64) { self.values.push(v); }

    /// True if last 3 values are monotonically non-decreasing.
    pub fn is_monotone_up(&self) -> bool {
        if self.values.len() < 3 { return true; }
        let n = self.values.len();
        self.values[n-3] <= self.values[n-2] && self.values[n-2] <= self.values[n-1]
    }

    /// True if last 3 values are strictly decreasing (regression).
    pub fn is_regressing(&self) -> bool {
        if self.values.len() < 3 { return false; }
        let n = self.values.len();
        self.values[n-3] > self.values[n-2] && self.values[n-2] > self.values[n-1]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_ed_zero_cycles() {
        let ed = compute(EdInput { patterns_found: 5, baseline_ns: 100, current_ns: 50, cycles: 0 });
        assert_eq!(ed, 0.0);
    }

    #[test]
    fn test_ed_positive_speedup() {
        // 2 patterns, 2x speedup, 1 cycle → 4.0
        let ed = compute(EdInput { patterns_found: 2, baseline_ns: 100, current_ns: 50, cycles: 1 });
        assert!((ed - 4.0).abs() < 1e-9);
    }

    #[test]
    fn test_history_monotone() {
        let mut h = EdHistory::default();
        h.push(1.0); h.push(2.0); h.push(3.0);
        assert!(h.is_monotone_up());
        assert!(!h.is_regressing());
    }

    #[test]
    fn test_history_regression() {
        let mut h = EdHistory::default();
        h.push(3.0); h.push(2.0); h.push(1.0);
        assert!(!h.is_monotone_up());
        assert!(h.is_regressing());
    }
}

//! Emergence Density (ED) — convergence quality metric.
//! ED = (1 - change_ratio) × speedup
//! where change_ratio = current_changes / baseline_changes (0.0 = fully converged),
//! and speedup = baseline_ns / current_ns (≥ 1.0 = faster).
//! ED increases monotonically as optimization converges and speed improves.

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct EdInput {
    pub baseline_changes: i64,
    pub current_changes: i64,
    pub baseline_ns: u128,
    pub current_ns: u128,
}

pub fn compute(input: EdInput) -> f64 {
    if input.current_ns == 0 || input.baseline_changes == 0 {
        return 0.0;
    }
    let change_ratio = input.current_changes as f64 / input.baseline_changes as f64;
    let convergence = (1.0 - change_ratio).max(0.0);
    let speedup = input.baseline_ns as f64 / input.current_ns as f64;
    convergence * speedup
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
    fn test_ed_zero_baseline() {
        let ed = compute(EdInput { baseline_changes: 0, current_changes: 0, baseline_ns: 100, current_ns: 50 });
        assert_eq!(ed, 0.0);
    }

    #[test]
    fn test_ed_full_convergence() {
        // 0/55 changes, 2x speedup → (1-0) × 2.0 = 2.0
        let ed = compute(EdInput { baseline_changes: 55, current_changes: 0, baseline_ns: 100, current_ns: 50 });
        assert!((ed - 2.0).abs() < 1e-9);
    }

    #[test]
    fn test_ed_no_convergence() {
        // 55/55 changes, 1x speedup → (1-1) × 1.0 = 0.0
        let ed = compute(EdInput { baseline_changes: 55, current_changes: 55, baseline_ns: 100, current_ns: 100 });
        assert!((ed - 0.0).abs() < 1e-9);
    }

    #[test]
    fn test_ed_partial_convergence() {
        // 30/55 changes, 1.5x speedup → (1-30/55) × 1.5 ≈ 0.6818
        let ed = compute(EdInput { baseline_changes: 55, current_changes: 30, baseline_ns: 150, current_ns: 100 });
        let expected = (1.0 - 30.0/55.0) * 1.5;
        assert!((ed - expected).abs() < 1e-9);
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

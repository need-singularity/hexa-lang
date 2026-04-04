//! Pass Selector Policy — decides opt pass order per round.

use crate::opt::ir_stats::PassMetric;

/// 12 pass names, in fixed sigma=12 order.
pub const FIXED_ORDER: &[&str] = &[
    "type_infer", "ownership_proof", "dead_store", "const_fold",
    "inlining", "licm", "cse", "strength_reduction",
    "code_sinking", "coalesce", "final_dce", "verify",
];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Policy {
    Fixed,
    Adaptive,
    Hybrid,
}

/// Decide next-round pass order based on policy + last-round metrics.
/// Adaptive: move slowest passes earlier (so they run on smaller IR later
///           after reductions from fast passes). Verify always stays last.
pub fn next_order(policy: Policy, round: usize, last: &[PassMetric]) -> Vec<String> {
    match policy {
        Policy::Fixed => FIXED_ORDER.iter().map(|s| s.to_string()).collect(),
        Policy::Adaptive => adaptive_order(last),
        Policy::Hybrid => {
            if round % 2 == 0 {
                FIXED_ORDER.iter().map(|s| s.to_string()).collect()
            } else {
                adaptive_order(last)
            }
        }
    }
}

fn adaptive_order(last: &[PassMetric]) -> Vec<String> {
    if last.is_empty() {
        return FIXED_ORDER.iter().map(|s| s.to_string()).collect();
    }
    // Sort by impact (instr reduction) descending; break ties by elapsed_ns desc.
    // verify MUST stay last.
    let mut reducers: Vec<&PassMetric> = last.iter()
        .filter(|m| m.pass_name != "verify")
        .collect();
    reducers.sort_by(|a, b| {
        let impact_a = -(a.delta_instr());
        let impact_b = -(b.delta_instr());
        impact_b.cmp(&impact_a)
            .then(b.elapsed_ns.cmp(&a.elapsed_ns))
    });
    let mut order: Vec<String> = reducers.iter().map(|m| m.pass_name.clone()).collect();
    order.push("verify".into());
    order
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::opt::ir_stats::Stats;

    fn mk(name: &str, delta: i64, ns: u128) -> PassMetric {
        let before = 100usize;
        let after = (before as i64 + delta).max(0) as usize;
        PassMetric {
            pass_name: name.into(),
            before: Stats { instr_count: before, block_count: 1, func_count: 1 },
            after: Stats { instr_count: after, block_count: 1, func_count: 1 },
            elapsed_ns: ns,
        }
    }

    #[test]
    fn test_fixed_policy_always_same() {
        let a = next_order(Policy::Fixed, 0, &[]);
        let b = next_order(Policy::Fixed, 5, &[mk("cse", -5, 100)]);
        assert_eq!(a, b);
        assert_eq!(a.len(), 12);
        assert_eq!(a[0], "type_infer");
        assert_eq!(a[11], "verify");
    }

    #[test]
    fn test_adaptive_verify_last() {
        let metrics = vec![
            mk("const_fold", -20, 500),
            mk("cse", -10, 800),
            mk("verify", 0, 200),
        ];
        let order = next_order(Policy::Adaptive, 1, &metrics);
        assert_eq!(order.last().unwrap(), "verify");
    }

    #[test]
    fn test_adaptive_high_impact_first() {
        let metrics = vec![
            mk("low", -1, 100),
            mk("high", -50, 100),
            mk("verify", 0, 10),
        ];
        let order = next_order(Policy::Adaptive, 1, &metrics);
        assert_eq!(order[0], "high");
    }

    #[test]
    fn test_hybrid_alternates() {
        let m = vec![mk("cse", -5, 100), mk("verify", 0, 10)];
        let even = next_order(Policy::Hybrid, 0, &m);
        let odd = next_order(Policy::Hybrid, 1, &m);
        assert_eq!(even.len(), 12);
        assert_ne!(even, odd);
    }
}

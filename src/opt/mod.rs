//! σ=12 Optimization Pipeline — 3 waves × 4 passes, fixed order.
//!
//! Front (P1-P4): TypeInfer → OwnershipProof → DeadStoreElim → ConstFold
//! Mid   (P5-P8): Inlining → LICM → CSE → StrengthReduction
//! Back  (P9-P12): CodeSinking → Coalesce → FinalDCE → Verify

mod p01_type_infer;
mod p02_ownership;
mod p03_dead_store;
mod p04_const_fold;
mod p05_inlining;
mod p06_licm;
mod p07_cse;
mod p08_strength;
mod p09_sinking;
mod p10_coalesce;
mod p11_final_dce;
mod p12_verify;
pub mod ir_stats;
pub mod eso_bridge;
pub mod pass_policy;
pub mod emergence_density;

use crate::ir::IrModule;

/// Pass trait — every optimization pass implements this.
pub trait Pass {
    fn name(&self) -> &'static str;
    fn run(&self, module: &mut IrModule) -> PassResult;
}

/// Result of running a pass.
#[derive(Debug, Default)]
pub struct PassResult {
    pub changed: bool,
    pub stats: Vec<(String, usize)>,
}

/// Run the full σ=12 optimization pipeline.
pub fn run_pipeline(module: &mut IrModule) -> PipelineResult {
    let passes: Vec<Box<dyn Pass>> = vec![
        // Front wave (P1-P4)
        Box::new(p01_type_infer::TypeInferPass),
        Box::new(p02_ownership::OwnershipProofPass),
        Box::new(p03_dead_store::DeadStoreElimPass),
        Box::new(p04_const_fold::ConstFoldPass),
        // Mid wave (P5-P8)
        Box::new(p05_inlining::InliningPass),
        Box::new(p06_licm::LicmPass),
        Box::new(p07_cse::CsePass),
        Box::new(p08_strength::StrengthReductionPass),
        // Back wave (P9-P12)
        Box::new(p09_sinking::CodeSinkingPass),
        Box::new(p10_coalesce::CoalescePass),
        Box::new(p11_final_dce::FinalDcePass),
        Box::new(p12_verify::VerifyPass),
    ];

    assert_eq!(passes.len(), 12, "σ(6) = 12 passes");

    let mut results = Vec::new();
    for pass in &passes {
        let result = pass.run(module);
        results.push((pass.name().to_string(), result));
    }

    PipelineResult { pass_results: results }
}

/// Summary of the full pipeline run.
#[derive(Debug)]
pub struct PipelineResult {
    pub pass_results: Vec<(String, PassResult)>,
}

impl PipelineResult {
    pub fn total_changes(&self) -> usize {
        self.pass_results.iter().filter(|(_, r)| r.changed).count()
    }

    pub fn summary(&self) -> String {
        let mut out = String::new();
        out.push_str(&format!("σ=12 Pipeline: {}/12 passes made changes\n", self.total_changes()));
        for (name, result) in &self.pass_results {
            let status = if result.changed { "✓" } else { "·" };
            out.push_str(&format!("  {} {}", status, name));
            for (k, v) in &result.stats {
                out.push_str(&format!(" [{}={}]", k, v));
            }
            out.push('\n');
        }
        out
    }
}

use crate::opt::ir_stats::{capture, PassMetric};
use crate::opt::pass_policy::Policy;

pub struct EsoPipelineResult {
    pub pipeline: PipelineResult,
    pub metrics: Vec<PassMetric>,
}

fn passes_by_name() -> Vec<(&'static str, Box<dyn Pass>)> {
    vec![
        ("type_infer", Box::new(p01_type_infer::TypeInferPass)),
        ("ownership_proof", Box::new(p02_ownership::OwnershipProofPass)),
        ("dead_store", Box::new(p03_dead_store::DeadStoreElimPass)),
        ("const_fold", Box::new(p04_const_fold::ConstFoldPass)),
        ("inlining", Box::new(p05_inlining::InliningPass)),
        ("licm", Box::new(p06_licm::LicmPass)),
        ("cse", Box::new(p07_cse::CsePass)),
        ("strength_reduction", Box::new(p08_strength::StrengthReductionPass)),
        ("code_sinking", Box::new(p09_sinking::CodeSinkingPass)),
        ("coalesce", Box::new(p10_coalesce::CoalescePass)),
        ("final_dce", Box::new(p11_final_dce::FinalDcePass)),
        ("verify", Box::new(p12_verify::VerifyPass)),
    ]
}

pub fn run_pipeline_with_policy(
    module: &mut IrModule,
    policy: Policy,
    round: usize,
    last_metrics: &[PassMetric],
) -> EsoPipelineResult {
    let order = crate::opt::pass_policy::next_order(policy, round, last_metrics);
    let registry = passes_by_name();

    let mut results = Vec::new();
    let mut metrics = Vec::new();

    for name in &order {
        let entry = registry.iter().find(|(n, _)| *n == name.as_str());
        if let Some((_, pass)) = entry {
            let before = capture(module);
            let start = std::time::Instant::now();
            let result = pass.run(module);
            let elapsed_ns = start.elapsed().as_nanos();
            let after = capture(module);
            let result_name = pass.name().to_string();
            metrics.push(PassMetric {
                pass_name: name.clone(),
                before,
                after,
                elapsed_ns,
            });
            results.push((result_name, result));
        }
    }

    EsoPipelineResult {
        pipeline: PipelineResult { pass_results: results },
        metrics,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_sigma_12_passes() {
        let mut module = IrModule::new("test");
        let result = run_pipeline(&mut module);
        assert_eq!(result.pass_results.len(), 12);
    }

    #[test]
    fn test_pass_order() {
        let mut module = IrModule::new("test");
        let result = run_pipeline(&mut module);
        let names: Vec<&str> = result.pass_results.iter().map(|(n, _)| n.as_str()).collect();
        assert_eq!(names[0], "type_infer");
        assert_eq!(names[3], "const_fold");
        assert_eq!(names[7], "strength_reduction");
        assert_eq!(names[11], "verify");
    }

    #[test]
    fn test_run_pipeline_with_policy_fixed_matches_default() {
        use crate::opt::pass_policy::Policy;
        let mut m1 = IrModule::new("t");
        let mut m2 = IrModule::new("t");
        let r1 = run_pipeline(&mut m1);
        let r2 = run_pipeline_with_policy(&mut m2, Policy::Fixed, 0, &[]);
        let names1: Vec<&str> = r1.pass_results.iter().map(|(n,_)| n.as_str()).collect();
        let names2: Vec<&str> = r2.pipeline.pass_results.iter().map(|(n,_)| n.as_str()).collect();
        assert_eq!(names1, names2);
    }

    #[test]
    fn test_run_pipeline_with_policy_returns_metrics() {
        use crate::opt::pass_policy::Policy;
        let mut m = IrModule::new("t");
        let r = run_pipeline_with_policy(&mut m, Policy::Fixed, 0, &[]);
        assert_eq!(r.metrics.len(), 12);
        assert!(r.metrics.iter().all(|pm| !pm.pass_name.is_empty()));
    }

    #[test]
    fn test_eso_10_rounds_converges() {
        use crate::opt::pass_policy::Policy;
        let mut module = IrModule::new("converge");
        let mut last_metrics: Vec<crate::opt::ir_stats::PassMetric> = Vec::new();
        let mut result = None;
        for round in 0..10 {
            let r = run_pipeline_with_policy(&mut module, Policy::Hybrid, round, &last_metrics);
            last_metrics = r.metrics.clone();
            result = Some(r);
        }
        let final_result = result.expect("should have run at least one round");
        assert_eq!(final_result.pipeline.total_changes(), 0, "pipeline should converge to 0 changes");
    }

    #[test]
    fn test_eso_adaptive_reorders_after_metrics() {
        use crate::opt::pass_policy::{Policy, FIXED_ORDER};
        use crate::opt::ir_stats::{PassMetric, Stats};

        // Build synthetic metrics with known deltas so adaptive reorders predictably.
        // Give "cse" a large reduction and "const_fold" a small one — adaptive should
        // move "cse" before "const_fold" (opposite of fixed order).
        let synthetic: Vec<PassMetric> = FIXED_ORDER.iter().map(|&name| {
            let delta: i64 = match name {
                "cse"        => -50,  // high impact → should move earlier
                "const_fold" => -1,   // low impact  → should move later
                "verify"     =>  0,   // must stay last
                _            => -5,
            };
            let before = 100usize;
            let after = (before as i64 + delta).max(0) as usize;
            PassMetric {
                pass_name: name.into(),
                before: Stats { instr_count: before, block_count: 1, func_count: 1 },
                after:  Stats { instr_count: after,  block_count: 1, func_count: 1 },
                elapsed_ns: 100,
            }
        }).collect();

        // Fixed order
        let mut m1 = IrModule::new("fixed");
        let r_fixed = run_pipeline_with_policy(&mut m1, Policy::Fixed, 0, &synthetic);
        let fixed_names: Vec<&str> = r_fixed.pipeline.pass_results.iter().map(|(n,_)| n.as_str()).collect();

        // Adaptive order using synthetic metrics
        let mut m2 = IrModule::new("adaptive");
        let r_adaptive = run_pipeline_with_policy(&mut m2, Policy::Adaptive, 1, &synthetic);
        let adaptive_names: Vec<&str> = r_adaptive.pipeline.pass_results.iter().map(|(n,_)| n.as_str()).collect();

        // Both should have 12 passes
        assert_eq!(fixed_names.len(), 12);
        assert_eq!(adaptive_names.len(), 12);

        // Adaptive must differ from fixed (cse should move ahead of const_fold)
        assert_ne!(fixed_names, adaptive_names, "adaptive should reorder passes vs fixed");

        // Verify always stays last in both
        assert_eq!(fixed_names.last().unwrap(), &"verify");
        assert_eq!(adaptive_names.last().unwrap(), &"verify");

        // In adaptive order, cse (high impact) should appear before const_fold (low impact)
        let cse_pos = adaptive_names.iter().position(|n| *n == "cse").unwrap();
        let cf_pos  = adaptive_names.iter().position(|n| *n == "const_fold").unwrap();
        assert!(cse_pos < cf_pos, "adaptive should place high-impact cse before const_fold");
    }

    #[test]
    fn test_eso_metrics_have_valid_pass_names() {
        use crate::opt::pass_policy::{Policy, FIXED_ORDER};
        let mut module = IrModule::new("names");
        let r = run_pipeline_with_policy(&mut module, Policy::Fixed, 0, &[]);
        assert_eq!(r.metrics.len(), 12);
        for pm in &r.metrics {
            assert!(!pm.pass_name.is_empty(), "pass_name must not be empty");
            assert!(
                FIXED_ORDER.contains(&pm.pass_name.as_str()),
                "pass_name '{}' not found in FIXED_ORDER",
                pm.pass_name,
            );
        }
    }
}

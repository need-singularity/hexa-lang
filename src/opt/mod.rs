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
}

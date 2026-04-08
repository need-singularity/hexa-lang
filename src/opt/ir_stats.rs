//! IR Stats Extractor — pre/post pass IR snapshot.

use crate::ir::IrModule;

#[derive(Debug, Clone, Default, PartialEq)]
pub struct Stats {
    pub instr_count: usize,
    pub block_count: usize,
    pub func_count: usize,
}

pub fn capture(module: &IrModule) -> Stats {
    let mut s = Stats::default();
    s.func_count = module.functions.len();
    for f in &module.functions {
        s.block_count += f.blocks.len();
        for b in &f.blocks {
            s.instr_count += b.instructions.len();
        }
    }
    s
}

#[derive(Debug, Clone)]
pub struct PassMetric {
    pub pass_name: String,
    pub before: Stats,
    pub after: Stats,
    pub elapsed_ns: u128,
}

impl PassMetric {
    pub fn delta_instr(&self) -> i64 {
        self.after.instr_count as i64 - self.before.instr_count as i64
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::IrModule;

    #[test]
    fn test_capture_empty_module() {
        let m = IrModule::new("test");
        let s = capture(&m);
        assert_eq!(s.func_count, 0);
        assert_eq!(s.block_count, 0);
        assert_eq!(s.instr_count, 0);
    }

    #[test]
    fn test_pass_metric_delta() {
        let pm = PassMetric {
            pass_name: "dce".into(),
            before: Stats { instr_count: 100, block_count: 10, func_count: 1 },
            after: Stats { instr_count: 85, block_count: 10, func_count: 1 },
            elapsed_ns: 1000,
        };
        assert_eq!(pm.delta_instr(), -15);
    }

    #[test]
    fn test_stats_equality() {
        let a = Stats { instr_count: 1, block_count: 1, func_count: 1 };
        let b = Stats { instr_count: 1, block_count: 1, func_count: 1 };
        assert_eq!(a, b);
    }
}

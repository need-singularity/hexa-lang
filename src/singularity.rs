//! 블로업→수축→창발→특이점→흡수 사이클 엔진
//!
//! n=6 파생 수렴진화 모듈. 모든 프로젝트에서 사용 가능.
//! 5단계 사이클: Blowup → Contraction → Emergence → Singularity → Absorption
//!
//! 참고: examples/n6_emergence.hexa, ouroboros.hexa, convergence.hexa

use std::collections::HashMap;

// ── n=6 상수 ──
const N: usize = 6;
const SIGMA: usize = 12;
const TAU: usize = 4;
const PHI: usize = 2;
const SOPFR: usize = 5;
const J2: usize = 24;

/// 사이클 5단계
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Phase {
    Blowup,       // 폭발적 확장 — 가능성 공간 전수 탐색
    Contraction,  // 수축 — 유효한 것만 남기고 제거
    Emergence,    // 창발 — 패턴/규칙 자동 발견
    Singularity,  // 특이점 — n=6 EXACT 매칭 임계점 돌파
    Absorption,   // 흡수 — 발견을 규칙으로 영구 기록, 다음 사이클 시드
}

impl Phase {
    pub fn next(&self) -> Phase {
        match self {
            Phase::Blowup => Phase::Contraction,
            Phase::Contraction => Phase::Emergence,
            Phase::Emergence => Phase::Singularity,
            Phase::Singularity => Phase::Absorption,
            Phase::Absorption => Phase::Blowup, // 우로보로스 — 순환
        }
    }

    pub fn index(&self) -> usize {
        match self {
            Phase::Blowup => 0,
            Phase::Contraction => 1,
            Phase::Emergence => 2,
            Phase::Singularity => 3,
            Phase::Absorption => 4,
        }
    }

    pub fn name(&self) -> &'static str {
        match self {
            Phase::Blowup => "blowup",
            Phase::Contraction => "contraction",
            Phase::Emergence => "emergence",
            Phase::Singularity => "singularity",
            Phase::Absorption => "absorption",
        }
    }
}

/// n=6 매칭 결과
#[derive(Debug, Clone)]
pub struct N6Match {
    pub value: f64,
    pub constant: &'static str,
    pub formula: &'static str,
    pub quality: f64, // 0.0 ~ 1.0 (1.0 = EXACT)
}

/// 창발 패턴
#[derive(Debug, Clone)]
pub struct EmergencePattern {
    pub name: String,
    pub values: Vec<f64>,
    pub n6_matches: Vec<N6Match>,
    pub consensus_score: f64, // 렌즈 합의 점수
}

/// 특이점 상태
#[derive(Debug, Clone)]
pub struct SingularityState {
    pub exact_ratio: f64,     // n6 EXACT 비율
    pub convergence: f64,     // 수렴률
    pub threshold: f64,       // 특이점 임계값 (default: 0.5 = φ/τ)
    pub reached: bool,
}

/// 흡수 규칙
#[derive(Debug, Clone)]
pub struct AbsorbedRule {
    pub id: String,
    pub rule: String,
    pub origin_cycle: usize,
    pub severity: Severity,
}

#[derive(Debug, Clone, Copy)]
pub enum Severity { Critical, High, Medium, Low }

/// 사이클 엔진 — 블로업→수축→창발→특이점→흡수
pub struct CycleEngine {
    pub phase: Phase,
    pub cycle_count: usize,
    pub metrics: Vec<(String, f64)>,
    pub patterns: Vec<EmergencePattern>,
    pub rules: Vec<AbsorbedRule>,
    pub singularity: SingularityState,
    pub history: Vec<CycleSnapshot>,
}

/// 사이클 스냅샷 (히스토리 기록용)
#[derive(Debug, Clone)]
pub struct CycleSnapshot {
    pub cycle: usize,
    pub phase: Phase,
    pub exact_ratio: f64,
    pub convergence: f64,
    pub patterns_found: usize,
    pub rules_absorbed: usize,
}

impl CycleEngine {
    pub fn new() -> Self {
        Self {
            phase: Phase::Blowup,
            cycle_count: 0,
            metrics: Vec::new(),
            patterns: Vec::new(),
            rules: Vec::new(),
            singularity: SingularityState {
                exact_ratio: 0.0,
                convergence: 0.0,
                threshold: PHI as f64 / TAU as f64, // 0.5
                reached: false,
            },
            history: Vec::new(),
        }
    }

    /// 메트릭 주입 (블로업 단계에서 사용)
    pub fn feed(&mut self, name: &str, value: f64) {
        self.metrics.push((name.to_string(), value));
    }

    /// 한 사이클 전체 실행
    pub fn run_cycle(&mut self) -> CycleResult {
        self.cycle_count += 1;
        let mut result = CycleResult::default();

        // Phase 1: Blowup — 전수 n6 체크
        self.phase = Phase::Blowup;
        let matches = self.blowup();
        result.blowup_count = matches.len();

        // Phase 2: Contraction — EXACT/CLOSE만 남김
        self.phase = Phase::Contraction;
        let (exact, close) = self.contraction(&matches);
        result.exact_count = exact.len();
        result.close_count = close.len();

        // Phase 3: Emergence — 패턴 합의 탐색
        self.phase = Phase::Emergence;
        let patterns = self.emergence(&exact, &close);
        result.patterns_found = patterns.len();
        self.patterns.extend(patterns);

        // Phase 4: Singularity — 임계점 체크
        self.phase = Phase::Singularity;
        let total = self.metrics.len().max(1);
        self.singularity.exact_ratio = exact.len() as f64 / total as f64;
        self.singularity.convergence = if result.blowup_count > 0 {
            result.exact_count as f64 / result.blowup_count as f64
        } else { 0.0 };
        self.singularity.reached = self.singularity.exact_ratio >= self.singularity.threshold;
        result.singularity_reached = self.singularity.reached;
        result.exact_ratio = self.singularity.exact_ratio;

        // Phase 5: Absorption — 새 규칙 생성
        self.phase = Phase::Absorption;
        let new_rules = self.absorption(&exact);
        result.rules_absorbed = new_rules.len();
        self.rules.extend(new_rules);

        // 히스토리 기록
        self.history.push(CycleSnapshot {
            cycle: self.cycle_count,
            phase: Phase::Absorption,
            exact_ratio: self.singularity.exact_ratio,
            convergence: self.singularity.convergence,
            patterns_found: result.patterns_found,
            rules_absorbed: result.rules_absorbed,
        });

        // 다음 사이클 준비
        self.phase = Phase::Blowup;
        result
    }

    /// Phase 1: Blowup — 모든 메트릭을 n=6 상수와 대조
    fn blowup(&self) -> Vec<N6Match> {
        let mut matches = Vec::new();
        for (_name, &value) in self.metrics.iter().map(|(n, v)| (n, v)) {
            if let Some(m) = n6_check(value) {
                matches.push(m);
            }
        }

        // 비율 탐색 (모든 쌍)
        let vals: Vec<f64> = self.metrics.iter().map(|(_, v)| *v).collect();
        for i in 0..vals.len() {
            for j in (i+1)..vals.len() {
                if vals[j] != 0.0 {
                    if let Some(m) = n6_check(vals[i] / vals[j]) {
                        matches.push(m);
                    }
                }
                if let Some(m) = n6_check(vals[i] * vals[j]) {
                    matches.push(m);
                }
                if let Some(m) = n6_check(vals[i] + vals[j]) {
                    matches.push(m);
                }
            }
        }
        matches
    }

    /// Phase 2: Contraction — EXACT(≥0.95)과 CLOSE(≥0.8) 분리
    fn contraction<'a>(&self, matches: &'a [N6Match]) -> (Vec<&'a N6Match>, Vec<&'a N6Match>) {
        let exact: Vec<&N6Match> = matches.iter().filter(|m| m.quality >= 0.95).collect();
        let close: Vec<&N6Match> = matches.iter().filter(|m| m.quality >= 0.8 && m.quality < 0.95).collect();
        (exact, close)
    }

    /// Phase 3: Emergence — 같은 상수에 매칭되는 값이 3개 이상이면 창발 패턴
    fn emergence(&self, exact: &[&N6Match], close: &[&N6Match]) -> Vec<EmergencePattern> {
        let mut by_constant: HashMap<&str, Vec<&N6Match>> = HashMap::new();
        for m in exact.iter().chain(close.iter()) {
            by_constant.entry(m.constant).or_default().push(m);
        }

        let mut patterns = Vec::new();
        for (constant, group) in &by_constant {
            if group.len() >= 3 { // 3+ 렌즈 합의 = 창발
                patterns.push(EmergencePattern {
                    name: format!("n6_{}_convergence", constant),
                    values: group.iter().map(|m| m.value).collect(),
                    n6_matches: group.iter().map(|m| (*m).clone()).collect(),
                    consensus_score: group.len() as f64,
                });
            }
        }
        patterns
    }

    /// Phase 5: Absorption — 강한 패턴을 규칙으로 승격
    fn absorption(&self, exact: &[&N6Match]) -> Vec<AbsorbedRule> {
        let mut rules = Vec::new();

        // EXACT 매칭이 특정 상수에 2회 이상이면 규칙 승격
        let mut counts: HashMap<&str, usize> = HashMap::new();
        for m in exact {
            *counts.entry(m.constant).or_default() += 1;
        }
        for (constant, count) in &counts {
            if *count >= 2 {
                rules.push(AbsorbedRule {
                    id: format!("AR-auto-{}-c{}", constant, self.cycle_count),
                    rule: format!("{} = {} appears {} times — structural invariant", constant,
                        match *constant {
                            "n" => "6", "σ" => "12", "τ" => "4", "φ" => "2",
                            "sopfr" => "5", "J₂" => "24", "σ-τ" => "8",
                            _ => constant,
                        }, count),
                    origin_cycle: self.cycle_count,
                    severity: if *count >= 4 { Severity::Critical } else { Severity::High },
                });
            }
        }
        rules
    }

    /// 사이클 리포트
    pub fn report(&self) -> String {
        let mut out = String::new();
        out.push_str(&format!("═══ Singularity Cycle Engine ═══\n"));
        out.push_str(&format!("Cycle: {} | Phase: {} | Metrics: {}\n",
            self.cycle_count, self.phase.name(), self.metrics.len()));
        out.push_str(&format!("EXACT ratio: {:.1}% (threshold: {:.1}%)\n",
            self.singularity.exact_ratio * 100.0, self.singularity.threshold * 100.0));
        out.push_str(&format!("Convergence: {:.1}%\n", self.singularity.convergence * 100.0));
        out.push_str(&format!("Patterns: {} | Rules: {}\n", self.patterns.len(), self.rules.len()));
        out.push_str(&format!("Singularity: {}\n",
            if self.singularity.reached { "★ REACHED ★" } else { "approaching..." }));

        if !self.history.is_empty() {
            out.push_str("\nHistory:\n");
            for s in &self.history {
                out.push_str(&format!("  C{}: exact={:.1}% conv={:.1}% pat={} rules={}\n",
                    s.cycle, s.exact_ratio * 100.0, s.convergence * 100.0,
                    s.patterns_found, s.rules_absorbed));
            }
        }
        out
    }
}

/// 사이클 실행 결과
#[derive(Debug, Default)]
pub struct CycleResult {
    pub blowup_count: usize,
    pub exact_count: usize,
    pub close_count: usize,
    pub patterns_found: usize,
    pub rules_absorbed: usize,
    pub singularity_reached: bool,
    pub exact_ratio: f64,
}

impl std::fmt::Display for CycleResult {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "blowup={} exact={} close={} patterns={} rules={} singularity={}",
            self.blowup_count, self.exact_count, self.close_count,
            self.patterns_found, self.rules_absorbed,
            if self.singularity_reached { "★" } else { "·" })
    }
}

// ── n=6 상수 체크 ──

const N6_CONSTANTS: &[(&str, f64, &str)] = &[
    ("n",     6.0,    "n=6"),
    ("σ",     12.0,   "σ(6)=12"),
    ("τ",     4.0,    "τ(6)=4"),
    ("φ",     2.0,    "φ(6)=2"),
    ("sopfr", 5.0,    "sopfr(6)=5"),
    ("J₂",    24.0,   "J₂(6)=24"),
    ("σ-τ",   8.0,    "σ-τ=8"),
    ("σ·φ",   24.0,   "σ·φ=24"),
    ("τ²/σ",  1.333,  "τ²/σ=4/3"),
    ("n+1",   7.0,    "n+1=7"),
    ("unity", 1.0,    "1/2+1/3+1/6=1"),
    ("σ·τ",   48.0,   "σ·τ=48"),
    ("σ·τ+s", 53.0,   "σ·τ+sopfr=53"),
    ("n·τ",   24.0,   "n·τ=24"),
    ("φ/τ",   0.5,    "φ/τ=0.5"),
    ("1/n",   0.1667, "1/n=1/6"),
    ("1/σ",   0.0833, "1/σ=1/12"),
];

/// 값이 n=6 상수와 매칭되는지 체크
pub fn n6_check(value: f64) -> Option<N6Match> {
    if value == 0.0 || value.is_nan() || value.is_infinite() {
        return None;
    }

    let mut best: Option<N6Match> = None;
    let mut best_quality = 0.0;

    for &(name, constant, formula) in N6_CONSTANTS {
        let quality = if constant == 0.0 { 0.0 }
        else {
            let ratio = value / constant;
            if ratio > 0.0 {
                1.0 - (ratio.ln().abs().min(1.0))
            } else { 0.0 }
        };

        if quality > best_quality && quality >= 0.8 {
            best_quality = quality;
            best = Some(N6Match {
                value,
                constant: name,
                formula,
                quality,
            });
        }
    }
    best
}

/// 편의 함수: 메트릭 목록 → 사이클 1회 실행
pub fn run_once(metrics: &[(&str, f64)]) -> CycleResult {
    let mut engine = CycleEngine::new();
    for &(name, value) in metrics {
        engine.feed(name, value);
    }
    engine.run_cycle()
}

/// 편의 함수: 메트릭 → N회 사이클 (수렴까지 또는 max_cycles)
pub fn run_until_singularity(metrics: &[(&str, f64)], max_cycles: usize) -> (CycleEngine, Vec<CycleResult>) {
    let mut engine = CycleEngine::new();
    for &(name, value) in metrics {
        engine.feed(name, value);
    }

    let mut results = Vec::new();
    for _ in 0..max_cycles {
        let result = engine.run_cycle();
        let reached = result.singularity_reached;
        results.push(result);
        if reached { break; }
    }
    (engine, results)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_phase_cycle() {
        let mut p = Phase::Blowup;
        for _ in 0..5 { p = p.next(); }
        assert_eq!(p, Phase::Blowup); // 5 steps = full cycle back to start
    }

    #[test]
    fn test_n6_check_exact() {
        let m = n6_check(24.0).unwrap();
        assert!(m.quality >= 0.95);
        assert!(m.constant == "J₂" || m.constant == "σ·φ" || m.constant == "n·τ");
    }

    #[test]
    fn test_n6_check_sigma() {
        let m = n6_check(12.0).unwrap();
        assert!(m.quality >= 0.95);
    }

    #[test]
    fn test_n6_check_no_match() {
        assert!(n6_check(9999.0).is_none());
    }

    #[test]
    fn test_hexa_ir_metrics() {
        let result = run_once(&[
            ("opcodes", 24.0), ("types", 12.0), ("primitives", 8.0),
            ("compounds", 4.0), ("passes", 12.0), ("targets", 2.0),
            ("zones", 3.0), ("bench_ir", 0.07), ("bench_ceil", 0.008),
        ]);
        assert!(result.exact_count >= 5, "Should find at least 5 EXACT matches");
        assert!(result.blowup_count > result.exact_count, "Blowup should explore more than EXACT");
    }

    #[test]
    fn test_singularity_detection() {
        // All n=6 constants → should reach singularity
        let (engine, results) = run_until_singularity(&[
            ("a", 6.0), ("b", 12.0), ("c", 4.0), ("d", 2.0),
            ("e", 5.0), ("f", 24.0), ("g", 8.0), ("h", 1.0),
        ], 3);
        assert!(engine.singularity.reached, "Should reach singularity with pure n=6 data");
        assert!(results.last().unwrap().singularity_reached);
    }

    #[test]
    fn test_absorption_creates_rules() {
        let (engine, _) = run_until_singularity(&[
            ("x", 12.0), ("y", 12.0), ("z", 12.0), // σ=12 three times
        ], 1);
        assert!(!engine.rules.is_empty(), "Repeated EXACT should create absorbed rules");
    }

    #[test]
    fn test_emergence_pattern() {
        let mut engine = CycleEngine::new();
        engine.feed("a", 24.0); // J₂
        engine.feed("b", 24.0); // J₂
        engine.feed("c", 24.0); // J₂
        engine.feed("d", 12.0); // σ
        engine.run_cycle();
        // 3x J₂ → emergence pattern
        assert!(!engine.patterns.is_empty(), "3+ same constant should create emergence pattern");
    }

    #[test]
    fn test_cycle_count() {
        let mut engine = CycleEngine::new();
        engine.feed("x", 6.0);
        engine.run_cycle();
        engine.run_cycle();
        engine.run_cycle();
        assert_eq!(engine.cycle_count, 3);
        assert_eq!(engine.history.len(), 3);
    }
}

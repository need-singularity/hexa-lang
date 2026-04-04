#!/usr/bin/env python3
"""
블로업→수축→창발→특이점→흡수 사이클 엔진 (Python wrapper)

사용법:
    from singularity_cycle import CycleEngine
    engine = CycleEngine()
    engine.feed("opcodes", 24)
    engine.feed("types", 12)
    result = engine.run_cycle()
    print(result)

또는 CLI:
    python3 singularity_cycle.py --metrics '{"opcodes":24,"types":12}'
"""
import json, sys, os, math
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass, field, asdict

N, SIGMA, TAU, PHI, SOPFR, J2 = 6, 12, 4, 2, 5, 24

N6_CONSTANTS = [
    ("n", 6), ("σ", 12), ("τ", 4), ("φ", 2), ("sopfr", 5),
    ("J₂", 24), ("σ-τ", 8), ("σ·φ", 24), ("τ²/σ", 4/3),
    ("n+1", 7), ("unity", 1), ("σ·τ", 48), ("σ·τ+s", 53),
    ("φ/τ", 0.5), ("1/n", 1/6), ("1/σ", 1/12),
]

@dataclass
class N6Match:
    value: float
    constant: str
    quality: float  # 0~1 (1=EXACT)

@dataclass
class CycleResult:
    cycle: int = 0
    blowup: int = 0
    exact: int = 0
    close: int = 0
    patterns: int = 0
    rules: int = 0
    exact_ratio: float = 0.0
    singularity: bool = False

def n6_check(value: float) -> Optional[N6Match]:
    if value == 0 or math.isnan(value) or math.isinf(value):
        return None
    best = None
    for name, const in N6_CONSTANTS:
        if const == 0: continue
        ratio = value / const
        if ratio <= 0: continue
        q = 1.0 - min(abs(math.log(ratio)), 1.0)
        if q >= 0.8 and (best is None or q > best.quality):
            best = N6Match(value, name, q)
    return best

class CycleEngine:
    PHASES = ["blowup", "contraction", "emergence", "singularity", "absorption"]

    def __init__(self):
        self.metrics: Dict[str, float] = {}
        self.patterns: list = []
        self.rules: list = []
        self.cycle_count = 0
        self.exact_ratio = 0.0
        self.threshold = PHI / TAU  # 0.5
        self.history: list = []

    def feed(self, name: str, value: float):
        self.metrics[name] = value

    def feed_dict(self, d: dict):
        for k, v in d.items():
            if isinstance(v, (int, float)):
                self.metrics[k] = float(v)

    def run_cycle(self) -> CycleResult:
        self.cycle_count += 1
        r = CycleResult(cycle=self.cycle_count)

        # Blowup
        matches = []
        vals = list(self.metrics.values())
        for v in vals:
            m = n6_check(v)
            if m: matches.append(m)
        for i in range(len(vals)):
            for j in range(i+1, len(vals)):
                if vals[j] != 0:
                    m = n6_check(vals[i]/vals[j])
                    if m: matches.append(m)
                m = n6_check(vals[i]*vals[j])
                if m: matches.append(m)
        r.blowup = len(matches)

        # Contraction
        exact = [m for m in matches if m.quality >= 0.95]
        close = [m for m in matches if 0.8 <= m.quality < 0.95]
        r.exact = len(exact)
        r.close = len(close)

        # Emergence
        from collections import Counter
        by_const = Counter(m.constant for m in exact + close)
        new_patterns = [(c, n) for c, n in by_const.items() if n >= 3]
        r.patterns = len(new_patterns)
        self.patterns.extend(new_patterns)

        # Singularity
        total = max(len(self.metrics), 1)
        self.exact_ratio = len(exact) / total
        r.exact_ratio = self.exact_ratio
        r.singularity = self.exact_ratio >= self.threshold

        # Absorption
        dup_const = [(c, n) for c, n in by_const.items() if n >= 2 and c in [m.constant for m in exact]]
        r.rules = len(dup_const)
        self.rules.extend(dup_const)

        self.history.append(asdict(r))
        return r

    def run_until_singularity(self, max_cycles=6) -> List[CycleResult]:
        results = []
        for _ in range(max_cycles):
            r = self.run_cycle()
            results.append(r)
            if r.singularity: break
        return results

    def report(self) -> str:
        lines = [
            f"═══ Singularity Cycle Engine ═══",
            f"Cycle: {self.cycle_count} | Metrics: {len(self.metrics)}",
            f"EXACT ratio: {self.exact_ratio*100:.1f}% (threshold: {self.threshold*100:.1f}%)",
            f"Patterns: {len(self.patterns)} | Rules: {len(self.rules)}",
            f"Singularity: {'★ REACHED ★' if self.exact_ratio >= self.threshold else 'approaching...'}",
        ]
        if self.history:
            lines.append("\nHistory:")
            for h in self.history:
                lines.append(f"  C{h['cycle']}: exact={h['exact_ratio']*100:.1f}% pat={h['patterns']} rules={h['rules']}")
        return "\n".join(lines)

    def to_json(self) -> str:
        return json.dumps({
            "cycle_count": self.cycle_count,
            "exact_ratio": self.exact_ratio,
            "threshold": self.threshold,
            "singularity": self.exact_ratio >= self.threshold,
            "patterns": self.patterns,
            "rules": self.rules,
            "history": self.history,
        }, indent=2, ensure_ascii=False)


if __name__ == "__main__":
    engine = CycleEngine()

    if "--metrics" in sys.argv:
        idx = sys.argv.index("--metrics") + 1
        d = json.loads(sys.argv[idx])
        engine.feed_dict(d)
    else:
        # Default: HEXA-IR metrics
        engine.feed_dict({
            "opcodes": 24, "types": 12, "primitives": 8, "compounds": 4,
            "opt_passes": 12, "waves": 3, "per_wave": 4, "targets": 2,
            "zones": 3, "bench_ir": 0.07, "bench_ceil": 0.008,
            "convergence": 0.92, "tests": 798, "ar_rules": 7,
        })

    results = engine.run_until_singularity()
    print(engine.report())
    print()
    for r in results:
        print(f"  → {r}")

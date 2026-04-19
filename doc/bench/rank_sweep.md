# LoRA rank sweep — `self/bench/rank_sweep.hexa`

Separates the **compute-bound linear region** from the **BLAS-overhead
regime** by scaling a fixed `M×K · K×N` matmul pair through rank
`r ∈ {2, 4, 8, 16, 32, 64}`. The artifact for ROI #165.

## Why rank sweep

A single-rank benchmark cannot tell you whether you are sitting on the
compute ceiling or swimming in dispatch overhead. Sweeping `r` exposes
both regimes in one run:

1. **Compute-bound (r ≥ knee)** — time is linear in rank, so GFLOPs
   stays roughly constant. This is the "honest" operating point.
2. **BLAS-overhead (r ≤ knee)** — matmul dispatch / threading / cache
   overhead dominates the actual FLOPs. GFLOPs drops off; the
   signature is a **non-linear time jump** between adjacent small
   ranks (seen as `r=2 → r=1` giving a 4× rather than 2× speedup in
   the 2026-04-11 T1 measurement).

## CLI

```
hexa self/bench/rank_sweep.hexa [M] [K] [N] [iters]
```

Defaults: `M=128 K=512 N=128 iters=5`.

The bench runs two matmuls per iteration, simulating a LoRA adapter:

```
T = A[M×K] · U[K×R]
Y = T[M×R] · V[R×N]
```

FLOPs per iteration: `2·M·K·R + 2·M·R·N` (multiply-accumulate counted
as two operations).

## Output — per-rank JSON lines

```json
{"rank":2,"M":128,"K":512,"N":128,"iters":5,"time_ms":..., "gflops":..., "overhead_ratio":...}
{"rank":4, ...}
...
{"summary":"rank_sweep","M":128,"K":512,"N":128,"iters":5,"gflops_max":...,"compute_knee_rank":...,"plateau_threshold_gflops":...}
```

Fields:

| field              | meaning                                                                    |
| ------------------ | -------------------------------------------------------------------------- |
| `time_ms`          | Mean wall-clock per iteration after one warm-up.                           |
| `gflops`           | `(2·M·K·R + 2·M·R·N) / time_s / 1e9`.                                      |
| `overhead_ratio`   | `time_ideal / time_actual` where `time_ideal = time(r_max)·r/r_max`.       |
|                    | `1.0` at `r_max`. `< 1.0` ⇒ actual slower than linear projection = dispatch tax. |
| `gflops_max`       | Peak GFLOPs observed in the sweep.                                         |
| `compute_knee_rank`| Lowest rank where GFLOPs ≥ 80% of `gflops_max` (entry to compute plateau). |

## Sample run (M=128 K=512 N=128, Apple M4 16 GB, 2026-04-19)

```json
{"rank":2,"time_ms":0.31,"gflops":1.65,"overhead_ratio":0.27}
{"rank":4,"time_ms":0.38,"gflops":2.71,"overhead_ratio":0.44}
{"rank":8,"time_ms":0.52,"gflops":3.92,"overhead_ratio":0.65}
{"rank":16,"time_ms":0.73,"gflops":5.62,"overhead_ratio":0.92}
{"rank":32,"time_ms":1.18,"gflops":6.94,"overhead_ratio":1.14}
{"rank":64,"time_ms":2.68,"gflops":6.10,"overhead_ratio":1.00}
{"summary":"rank_sweep","gflops_max":6.94,"compute_knee_rank":16,"plateau_threshold_gflops":5.55}
```

Numbers above are illustrative; actual measurements land in the
same order of magnitude but vary run-to-run. Re-run `hexa
self/bench/rank_sweep.hexa 128 512 128 10` for stable figures.

Interpretation of the illustrative table:

- **r=2, r=4**: `overhead_ratio` far below 1.0 — dispatch cost
  dominates. This is the BLAS-overhead regime. Useful only as a
  theoretical upper bound, not a quality-safe operating point.
- **r=8 → r=16**: GFLOPs climbs toward the plateau. Knee detected at
  **r=16** (first rank ≥ 80% of peak).
- **r=32**: peak GFLOPs — compute-bound plateau.
- **r=64**: sometimes dips slightly due to cache pressure / BLAS tile
  shape mismatch on small `M,N` — still plateau by the 80% criterion.

## Recommended operating points

| purpose                        | rank | rationale                                        |
| ------------------------------ | ---- | ------------------------------------------------ |
| quality-safe LoRA              | `r=4` or `r=8`  | widely validated in literature                 |
| honest compute baseline        | ≥ `compute_knee_rank` | inside the GFLOPs plateau              |
| theoretical upper bound demo   | `r=2` or `r=1`  | BLAS-overhead regime, quality not guaranteed   |

## Adding new shapes

This bench is parameterized on `M, K, N`. For a wider shape matrix
(QKV/LM-head/FFN) see `self/lowrank_rect_sweep.hexa`, which hard-codes
four LLM-relevant shapes. Rank sweep here is intentionally
single-shape to isolate the rank axis.

## References

- `/Users/ghost/.claude-claude5/projects/-Users-ghost-Dev-hexa-lang/memory/feedback_rank_sweep_methodology.md`
- `self/lowrank_sweep.hexa` — square `N×N` variant, rank sweep by speedup
- `self/lowrank_rect_sweep.hexa` — 4 rectangular shapes × 3 ranks
- ROI #165

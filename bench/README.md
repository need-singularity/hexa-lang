# bench/ — hexa-lang benchmark fixtures + regression gate

Fixtures here exercise compiler/runtime hot paths (dispatch, parse, sort,
matmul, alloc, OOP, etc.). Results are appended to `bench_results.jsonl`
by the harness `tool/ai_native_bench.hexa`.

## Files

- `*.hexa` — bench fixtures. Each is run as a whole-process invocation
  via `~/.hx/bin/hexa` (or `build/hexa_interp` directly inside the
  harness). Wall-clock is measured outside the process by the harness.
- `bench_results.jsonl` — append-only JSONL of every run. Schema:
  ```
  {"file": "<path>", "iters": N, "warmup": K,
   "avg_ns": <int|void>, "median_ns": <int|void>,
   "min_ns": <int|void>, "max_ns": <int|void>}
  ```
- `check_regress.sh` — N01 regression gate (this doc).
- `snapshots/` — pinned reference outputs for select fixtures.
- `rust_baseline/` — comparable Rust impls for a small subset.

## Running benches

```sh
# Single fixture, default iters=3 warmup=1
hexa tool/ai_native_bench.hexa bench/probe.hexa

# Override iters/warmup
hexa tool/ai_native_bench.hexa bench/probe.hexa --iters=20 --warmup=3

# All bench/*.hexa files
hexa tool/ai_native_bench.hexa --all
```

Each invocation appends a row to `bench/bench_results.jsonl`.

## What `void` entries mean (Q6 from speedup brainstorm 2026-05-06)

About 10 of the first 13 rows in the current jsonl have
`"avg_ns": void`. Two distinct causes were identified:

1. **argv parser fall-through** in `tool/ai_native_bench.hexa` `main()`:
   `--warmup=0` is rejected by `wv > 0` (line 162), so the literal token
   `"--warmup=0"` falls through to `files.push(a)` and is bench'd as a
   nonexistent path. Resulting timer returns void (no process to time).
   Workaround: pass `--warmup=1` (or any nonzero), or fix the parser to
   treat any `--warmup=...` prefix as consumed.

2. **timer cascade returns 0/void** when the inner `hexa_interp` call
   crashes silently before producing wall-clock delta. Schema then
   serializes `to_string(void)` literally as `void`, producing invalid
   JSON. Fixtures observed in this state: `bench/fib_rec.hexa` (early
   runs — recursion-depth runtime bug), `bench/probe.hexa` (first 2
   attempts only, before resolver-bypass landed).

The gate (`check_regress.sh`) treats any row containing `"avg_ns":
void` or `"median_ns": void` as **harness-broken** and skips it for
baseline calculation. The total skipped count is reported as `void=N`
in the gate header.

## Regression gate — `check_regress.sh`

Local gate. Compares each fixture's most recent valid `median_ns` to
the median of its prior `--window` (default 7) valid runs. Flags any
fixture exceeding `--threshold` (default `1.10` = +10%).

```sh
# Default (HEXA_LANG/bench/bench_results.jsonl, threshold 1.10, window 7)
bench/check_regress.sh

# Show every fixture line, not just regressions
bench/check_regress.sh --print

# 20% slack threshold
bench/check_regress.sh --threshold 1.20

# Different baseline window (e.g. 3-run trailing median)
bench/check_regress.sh --window 3

# Custom jsonl (e.g. CI artifact, snapshot)
bench/check_regress.sh --jsonl /path/to/bench_results.jsonl
```

Exit codes:
- `0` — PASS (or insufficient data — no fixture has ≥2 valid runs yet)
- `1` — FAIL — at least one fixture regressed
- `2` — gate itself errored (missing jsonl, missing python3)

Sample output (current state, 2026-05-06):

```
check_regress: rows_total=13 void=10 parse_fail=0 fixtures_with_valid=2 threshold=1.1 window=7
check_regress: WARN — 10 void/invalid row(s); see bench/README.md
  [skip] bench/bubble_sort_n16.hexa: only 1 valid sample(s) — no baseline
  [OK] bench/probe.hexa: latest=6573000ns base_med=6309000ns ratio=1.042x (n_base=1)
check_regress: checked=1 no_baseline=1 regressed=0
check_regress: PASS
```

### Why ratio threshold (not t-test) for v0

N02 in `docs/hexa_speedup_brainstorm_2026_05_06.ai.md` calls for a
proper Welch's t-test once we have `iters>=20` per row. Today most
rows are `iters=3 warmup=1`, so per-row variance is high and the
t-test would either be uninformative or require pooling many rows.
Ratio-against-median is the conservative falsifier we can ship today.

### Not yet wired to CI

The repo has no CI config at this commit (status d8cf2f40). Run the
gate locally before commits that touch the interp / runtime / parser.
A future PR can wire it into a GitHub Actions or local `pre-push` hook.

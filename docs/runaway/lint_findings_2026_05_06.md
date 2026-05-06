# RFC-010 lint findings — sweep 2026-05-06

Initial cross-tool lint sweep across `self/*.hexa` (first 25 files; full
sweep blocked by 768 MB hexa-runtime mem cap — needs `HEXA_MEM_UNLIMITED=1`
or smaller batches).

## Summary by tool

| Lint | Hits | Hotspot |
|---|---|---|
| `runaway_pattern_lint` | 2 | `self/ai_native_pass.hexa:18380` (R3 + R6 dual-fire on `while anp_cc_contains(used, c)`) |
| `bounded_loop_lint` (B2) | 19+ | `self/ai_native_pass.hexa` (15 unique sites + 3 in `_smoke_*.hexa`) |
| `total_fn_lint` (T1) | 20+ | `self/ai_native_pass.hexa` (all AST-walk recursion fns) |

**~70% of all hits land in `self/ai_native_pass.hexa`** — single file is
the dominant target for Stage 1 annotation work.

## Categorized findings

### Class A — AST-structural recursion (T1; `self/ai_native_pass.hexa`)

All of these are well-founded by structural recursion on AST nodes
(each call passes a child of the input). Annotation: `// decreases node-depth`
or `@total` once SCC-aware checker lands.

Sites (line numbers, fn names):
- 95 `check_purity_m1`
- 1449 `tco_rewrite_body`
- 1523 `count_var_assigns`
- 1550 `expr_reads_var`
- 1666 `subst_expr`
- 1751 `rewrite_approx_body`
- 1821 `collect_used_names_expr` / 1848 `collect_used_names_body`
- 1881 `expr_has_side_effect`
- 1898 `eliminate_unused_lets`
- 2018 `max_dp_lookback`
- 2053 `detect_stack_alloc_candidate`
- 2257 `inline_always_expr`
- 2414 `count_iv_mul_in_expr`
- 2861-2982 `count_underscore_bindings` / `count_increments` / `count_decrements`
  / `count_accumulators` / `expr_has_intlit_binop` / `count_binops_with_intlit`

Action: bulk-annotate with `// decreases <node>` (or class-level
`@total` modifier) once parser recognizes the modifier. Stage-1
acceptance target: zero T1 hits in `ai_native_pass.hexa`.

### Class B — `while` lacking decreases witness (B2)

`self/ai_native_pass.hexa` lines 156, 179, 200, 333, 1294-1295, 1448,
1506, 2015, 2081, 2723, 2802, 2830 (×2), 3114, 3185, 3487 — 17 unique sites.

Smoke tests: `self/_smoke_exec_stream_async_sleep.hexa:2,19` and
`_smoke_exec_stream_async.hexa:26` — these test fixtures use `while ready`
polling patterns intentionally; should be marked `@partial` or moved.

Action: spot-fix the smokes (use `@partial`); annotate ai_native_pass
sites with `// decreases <expr>` per case.

### Class C — Polling-shape true positives (R3+R6)

`self/ai_native_pass.hexa:18380` — `while anp_cc_contains(used, c) { c = c + 1 }`
The body increments `c`, so termination IS guaranteed (used array is
finite, c will hit a value not-contained). However:

- R3 (busy-wait without sleep): conservative true-positive; this is
  a CPU-bound search loop, not a polling wait. Acceptable.
- R6 (side-effecting call in cond): `anp_cc_contains` is a pure read.
  The lint conservatively flags any non-pure-helper-named call. Either
  rename `anp_cc_contains` → `anp_cc_has_` (matches `is_*` / `has_*`
  pure-helper convention recognized by R6), OR add an `@pure` attribute
  + extend R6 to honor it.

Action recommended: rename to `anp_cc_has_color` (semantic match with
the call site).

### Class D — Out-of-scope (sibling repos)

Vestigial `HEXA_RESOLVER_NO_REROUTE=1` exports in `~/.hx/bin/{nexus,hxq,
hexa-url-handler.sh}` and `~/.hx/packages/hive-resource/bin/hive-resource`
— belong to nexus/hive sibling repos. Already documented in
`docker_usage_audit.md` Top-5 #5 as deferrable cleanup.

### Class E — Spurious

`stdlib/math.hexa:107` (R3): inner factorization loop `while x % d == 0`.
Termination held by `x = x / d` div-step; lint heuristic doesn't model
this. Acceptable warn — annotate with `// decreases x`.

## Stage 1 acceptance criteria proposal

For lint -E gate (`--strict-new` against future PRs), require:
1. Zero new R1/R2/R5 (kw / empty-cond / self-recurse-only) — these are
   never legitimate. (Currently 0 in entire sweep.)
2. Zero new T1 unannotated recursion — opt-in to `@partial` if needed.
3. Zero new B1 unbounded for — must declare `.take(N)` or
   `// decreases`.
4. R3/R6/B2 remain warn-only (Stage 0 data collection).

## Memory cap note

Running the lints across all `self/*.hexa` (~80 files) hit the 768 MB
hexa-runtime cap mid-scan. Workarounds:
- `HEXA_MEM_UNLIMITED=1 hexa run tool/<lint>.hexa ...` (current run used this)
- Split into batches of ≤25 files each
- Long-term: lint can stream files (read+process+drop instead of holding
  all at once); deferred — RFC-010 follow-on.

# B5 — `@selftest` contract enforcement

- **Status**: spec proposed (additive, no migration)
- **Date**: 2026-05-02
- **Bundle**: composability A1+A5+B2+B5 (BG-HX2)
- **Cross-refs**:
  - precedent: ad-hoc `_selftest_*()` fns scattered across `core/{grammar,attr}_format/*.hexa` (no enforcement)
  - precedent: byte-identical 2-run selftest convention (e.g. `attr_format_main.hexa` aggregator selftest sha)
  - precedent: `tool/_bench_runner_selftest_fixtures/` + `tool/_spec_runner_selftest_fixtures/` — fixture dir convention
  - sister: B2 `@sentinel` (this RFC composes naturally with B2 — selftest result emits a sentinel)

## §0 TL;DR

Today, selftest is **convention-only**:
- A fn named `_selftest_<x>()` is run by an aggregator that scans for the prefix.
- The aggregator asserts ad-hoc; failures may print or may exit(1) inconsistently.
- Byte-identical 2-run is verified manually by re-running and `diff`-ing stdout.

This RFC introduces an enforced `@selftest` attr that:
1. Declares a fn IS a selftest (replaces / parallels the `_selftest_` name prefix).
2. Specifies expected case count (`cases=N`) — aggregator asserts the fn touches that many.
3. Specifies byte-identical run count (`byte_identical=true`) — aggregator runs it twice
   and asserts stdout sha256 matches.
4. Specifies cost / time bounds (`max_ms=500`) — aggregator fails if exceeded.
5. Composes with B2 `@sentinel` — selftest pass/fail emits a structured sentinel.

**Additive only**: existing `_selftest_*()` name-prefix convention stays. New `@selftest`
is opt-in.

## §1 Current behavior (baseline)

```hexa
fn _selftest_grammar_v1() {
    let r1 = grammar_v1_parse("...")
    if r1.ok != 1 { println("FAIL grammar_v1 case 1"); exit(1) }
    let r2 = grammar_v1_validate(r1.decls)
    if r2.ok != 1 { println("FAIL grammar_v1 case 2"); exit(1) }
    println("PASS grammar_v1 selftest 2/2")
}

fn main() {
    _selftest_grammar_v1()
    // ... ad-hoc 2-run sha check externally ...
}
```

Issues:
- Case count drift: doc says "2/2" but the fn might silently grow/shrink.
- Byte-identical must be verified by hand (`hexa <file> | shasum -a 256` × 2).
- No max-time enforcement; a selftest that takes 5 min is silently accepted.
- No machine-readable result; downstream automation must regex stdout.

## §2 Proposed contract (additive)

### §2.1 Attr declaration

```hexa
@selftest(
    cases=2,
    byte_identical=true,
    max_ms=500,
    sentinel="__GRAMMAR_V1_SELFTEST__"        // optional; composes with B2
)
fn _selftest_grammar_v1() {
    selftest_case("parse_ok", || {
        let r = grammar_v1_parse("...")
        selftest_assert(r.ok == 1, "parse failed")
    })
    selftest_case("validate_ok", || {
        let r = grammar_v1_validate(decls)
        selftest_assert(r.ok == 1, "validate failed")
    })
}
```

Required attr fields:
- `cases`: int, count of `selftest_case(...)` invocations the fn must make. Aggregator
  fails if the actual count differs.

Optional:
- `byte_identical`: bool (default false). If true, aggregator runs the fn twice in
  sub-process isolation and asserts stdout sha256 matches.
- `max_ms`: int, soft cap. Warn if exceeded; fail if `2*max_ms` exceeded.
- `sentinel`: str, B2-compliant sentinel name. On completion, aggregator emits
  `__<NAME>__ verdict=PASS|FAIL cases=N elapsed_ms=M` (composes with B2 `@sentinel`).
- `cost_usd`: float, declared cost. Aggregator scrapes a `state/cost_log.jsonl` ledger;
  warns if actual exceeds 1.5× declared.

### §2.2 Stdlib helpers

```hexa
fn selftest_case(name: str, body: fn() -> ())          // increments case counter; runs body; on fail, records and continues
fn selftest_assert(cond: bool, msg: str)                // raises selftest_fail
fn selftest_subprocess_run(file: str) -> SelftestResult // for byte-identical aggregator
```

### §2.3 Aggregator

```hexa
@selftest_aggregator
fn run_all_selftests() {
    for f in scope_fns_with_attr("@selftest") {
        let result = selftest_run(f)
        if result.byte_identical_required {
            let r2 = selftest_subprocess_run(__file__)  // re-run in fresh process
            if r2.stdout_sha != result.stdout_sha {
                emit_fail("byte-identical violated", f)
            }
        }
        emit_sentinel(f.attr.sentinel, result)
    }
}
```

The aggregator is itself an attr `@selftest_aggregator` (singleton per process); v1
implementation can be a regular fn, attr is sugar.

## §3 Compatibility

- `_selftest_<x>()` name convention — unchanged. Fns without `@selftest` attr are NOT
  validated by the new aggregator (legacy path).
- `selftest_case()` / `selftest_assert()` are new stdlib fns — verified absence:
  `grep -rn "fn selftest_case" /Users/<user>/core/hexa-lang` → empty.
- Existing aggregators (e.g. `attr_format_main.hexa` runs 6-category checks via
  hand-rolled if/else) are unchanged. They MAY adopt `@selftest` but are not required to.
- Composes with B2 — if B2 is not landed, `sentinel=` field is informational only.

## §4 Impl plan (additive, no migration)

**Attr registry side** (`hexa-lang/modules/attr_format/attr_v3.hexa` extension):
- Add `selftest` to the typed-attr schema set:
  ```
  attr selftest {
      cases:          int  required
      byte_identical: bool optional default=false
      max_ms:         int  optional default=5000
      sentinel:       str  optional pattern="^__.+__$"
      cost_usd:       f64  optional default=0.0
  }
  ```
- Effort: ~25 LoC (schema entry + selftest case for the attr itself — meta-recursive).

**Stdlib side** (`hexa-lang/stdlib/selftest.hexa`, NEW):
- Public API:
  - `selftest_case(name, body)` — counter + try/catch around body.
  - `selftest_assert(cond, msg)` — throws on false.
  - `selftest_run(fn_ref) -> SelftestResult` — invokes, captures stdout, times it.
  - `selftest_subprocess_run(file) -> SelftestResult` — fork+exec the file again, capture stdout.
- Effort: ~200 LoC.

**Aggregator side** (`hexa-lang/tool/selftest_aggregator.hexa`, NEW):
- Reads attr metadata from compiled module (link-time scan, same mechanism as B2 registry).
- Runs each `@selftest` fn; verifies cases count, byte-identical (if required), max_ms.
- Emits per-fn sentinel (B2-form if available, else free-form println).
- Exit code: 0 if all PASS, 1 if any FAIL.
- Effort: ~180 LoC.

**Lint side** (extend `hexa-lang/tool/ai_native_lint.hexa`):
- Warn when a fn named `_selftest_*` lacks `@selftest` attr (suggest migration).
- Warn when an `@selftest(cases=N)` attr's actual `selftest_case(...)` count differs
  (static count via parser).
- Effort: ~50 LoC additive in existing lint.

**Total estimated impl scope**: ~455 LoC across 4 files.

## §5 Falsifiers (raw#71)

1. `@selftest(cases=2)` fn calls `selftest_case(...)` once → aggregator must FAIL
   with "case count mismatch: declared 2, observed 1".
2. `@selftest(byte_identical=true)` fn whose stdout includes `now()` (changes per run)
   → aggregator must FAIL with byte-identical violation (and clear msg).
3. `@selftest(max_ms=100)` fn that takes 500ms → warn (1×–2×) or fail (≥2×).
4. `@selftest` fn with `selftest_assert(false, "x")` → aggregator records as FAIL
   case but continues to next case (does NOT exit aggregator).
5. `@selftest_aggregator` finds 0 `@selftest` fns in module → exit 0 with msg
   "no selftests declared" (NOT exit 1 — empty is allowed).
6. Two `@selftest` fns in same module — aggregator must run both, not short-circuit
   on first.

## §6 Selftest fixture

See `proposals/composability_a1_a5_b2_b5/fixtures/b5_selftest_contract.hexa` —
9 cases covering case-count match, case-count mismatch (negative), byte-identical
pass, byte-identical fail (negative), max_ms warn, max_ms fail, sentinel emit (B2
composition), aggregator empty-module, aggregator multi-fn.

## §7 raw#10 caveats

1. Static case count requires parser to count `selftest_case(...)` calls — if the
   call is dynamic (`if cond { selftest_case(...) }`), static count is approximate
   (count of textual occurrences). Document explicitly.
2. Byte-identical requires deterministic stdout — any `now()`, `random()`, env-dep
   value violates. Aggregator gives a clear msg pointing at sha mismatch but does NOT
   pinpoint the offending line; debugging that is on the author.
3. Sub-process re-run for byte-identical doubles wall-time of the selftest. Aggregator
   should report "selftest took 200ms (×2 = 400ms wall)" so authors aren't surprised.
4. `cost_usd` field is for AI-native ledger scraping (raw 270/271/272/273) — actual
   cost tracking is out-of-scope for this RFC; field is forward-compat metadata.
5. Aggregator is per-module by default; cross-module aggregation is a sister RFC
   (deferred). v1: each module runs its own aggregator; CI calls each in turn.
6. Lint warn (not fail) when `_selftest_*` lacks `@selftest` — additive policy means
   existing legacy sites must keep working without forced edits. Promotion to fail
   is a future governance event.
7. Composition with B2 — if B2 attr-registry is not yet land, the `sentinel=` field
   is parsed but ignored at runtime; aggregator falls back to free-form `println()`.
8. `@selftest_aggregator` attr is informational in v1 (the fn it decorates runs
   normally); enforcement (single-aggregator-per-module check) is deferred.
9. Mac-local strict — sub-process re-run uses `exec("hexa", file)` darwin-bypass.

## §8 Effort + retire

- Effort: spec ~3h. Impl ~2-3 days (stdlib + aggregator + lint warn).
- Retire when: §6 fixture passes 4 sessions AND at least one existing aggregator
  (candidate: `core/attr_format/attr_format_main.hexa`'s 6-category checker) opts
  in to `@selftest` enforcement.

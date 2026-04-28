# RFC 009 — AOT bool coercion bug for user-defined int-returning functions

- **Status**: proposed
- **Date**: 2026-04-28
- **Severity**: correctness (interpreter/AOT semantic divergence; silent-wrong class — same family as RFC-005)
- **Priority**: P0 (silent-wrong)
- **Source**: T17 agent discovery during anima-clm-eeg/tool/mk_xii_eeg_corroboration.hexa authoring 2026-04-28
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (gap-9 NEW)
- **Family**: shares root-cause class with RFC-005 (ends_with / slice-eq AOT codegen)

## Problem

Under AOT mode, the pattern `int_returning_fn() == int_literal` evaluates to **silent false** when the function genuinely returned that literal value. The interpreter mode evaluates correctly. There is no type error, no warning — the comparison silently produces the wrong boolean.

### Concrete reproducer

```hexa
fn returns_one() -> int { return 1 }

fn main() {
    if returns_one() == 1 {        // AOT: ALWAYS FALSE; interpreter: TRUE
        println("expected")
    } else {
        println("got")             // AOT actually prints this
    }
}
```

### Workaround pattern (T17 discovery)

Replace `fn() == N` with a comparison that does NOT directly equate the return value to the literal. For boolean-ish int returns (0/1/-1 sentinels), the proven workaround is:

```hexa
fn digit_val(c: string) -> int { /* returns 0..9 or -1 */ }

// BUG (AOT silent-wrong):
//   if digit_val(c) == 1 { ... }

// WORKAROUND (T17 form — works in both modes):
if digit_val(s[i]) >= 0 { return 1 }
```

Inequality (`>=`, `<`, `>`, `<=`) and the alternative comparison surfaces appear unaffected in current witness; only direct `==` to int literal is silent-wrong.

### Root-cause hypothesis (RFC-005 family)

AOT codegen for `==` between an int-returning call expression and an int literal does not emit value comparison. Most-likely shapes:

1. The call's return slot is treated as a boxed/tagged value while the literal is an unboxed int — `==` dispatches on identity / box-pointer rather than unboxing both sides.
2. Implicit bool coercion fires somewhere on the path: the call result gets coerced to a bool first (testing nonzero), then compared to the literal under int-context, producing `bool(1) == int(1) → some-other-result`.
3. The int return value is not properly extracted from the call ABI under AOT (struct-return / register-passing mismatch), and comparison sees a stale slot.

This is the same family as RFC-005: both are AOT codegen `==`-dispatch divergences from interpreter semantics. Gap-9 is the **int-result** sibling of gap-5's **string-slice** form.

## Affected sites

T17 evidence point — directly observed in the wild during 2026-04-28 authoring of mk_xii_eeg_corroboration.hexa; bug forced the workaround pattern (`digit_val(s[i]) >= 0` instead of `... == 1`).

Broader anima audit 2026-04-28:
- 18,975 .hexa files in /Users/ghost/core/anima match the pattern `[ident]([args]) == [int]` (broad pattern, includes built-ins like `len()`, `file_bytes()`, `argv()` indices, etc.).
- Of those, the **at-risk subset** is calls to user-defined `fn() -> int` returning small sentinels {-1, 0, 1, 2}. Static identification of that subset requires a parser-pass / type-aware audit and is out of scope for this RFC. Conservative working assumption: any anima 24+ verifier or T-agent witness builder that uses `fn() == 0` / `fn() == 1` sentinel-check pattern is potentially affected.
- Confirmed-affected (already worked-around): T17 mk_xii_eeg_corroboration.hexa.
- High-suspicion (uses `== 1` / `== 0` against locally-defined int-return fns): tool/an11_c_verifier.hexa, tool/mk_xii_hard_pass_composite.hexa, tool/anima_g_gate.hexa, tool/inference_cert_gate.hexa, tool/numerical_bound_test.hexa, tool/an11_c_jsd_h_last.hexa (sample from the 18,975 file list — full triage deferred to downstream witness cycle).

## Proposed Change

AOT codegen (`self/codegen_c2.hexa`) for `==` between an int-returning call expression and an int literal MUST produce value-equality semantics identical to the interpreter.

Two-track (mirrors RFC-005):

### TRACK A (root fix) — call-result lowering produces a plain int
Audit slice-expression / call-expression lowering in `self/codegen_c2.hexa`:
- Locate the codegen for `Call(callee, args) == IntLiteral`.
- Verify that the call result is materialized into an int slot (not a boxed/tagged value or bool-coerced sentinel).
- Verify that `==` dispatches on integer value comparison, not pointer/identity.
- Fix the divergence so AOT and interpreter share value-compare semantics.

### TRACK B (defensive lint) — detect the pattern
Even after Track A lands, add a parser/lint pass that flags `int_returning_fn() == int_literal` and suggests storing the result first (`let r = fn(); if r == 1 { ... }`) — the let-bind form is hypothesized to be AOT-safe because the slot materializes through a named binding before comparison.

Recommend Track A as the root fix; Track B as defense-in-depth and as a regression-canary for future AOT codegen rewrites.

## Compatibility

Track A is a bug fix. Code that worked in interpreter starts working in AOT. Code that compensated with `>= 0` / `let r = fn(); if r == 1` continues to work. No breaking change. Track B is opt-in lint; default off until anima sweep completes to avoid noisy diagnostics on files already migrated to workaround forms.

## Implementation Scope

- **Investigate + fix** `self/codegen_c2.hexa` (~50-80 LoC)
  - Locate `Call(...) == IntLiteral` codegen path
  - Locate `==`-dispatch for int operands derived from call results
  - Ensure value-equality semantics (integer-compare, not box-compare)
- **Regression test**: `test/regression/aot_int_call_eq.hexa` (~50 LoC)
  - `fn returns_one() -> int { return 1 }` ; assert `returns_one() == 1` is true under interpreter AND AOT
  - Sentinel cases: `fn() == 0`, `fn() == -1`, `fn() == 2`
  - Indirect form: `let r = fn(); r == 1` (works today; regression canary)
  - Negative form: `fn() != 1`, `fn() >= 1`, `fn() < 1` parity
- **raw#18 self-host fixpoint re-proof**: codegen change implies a 3-stage rebuild
  - stage1 = current bootstrap compiler builds new compiler
  - stage2 = new compiler builds itself
  - stage3 = stage2-compiler builds itself; binary-equal stage2 ⇒ fixpoint
  - run full hexa-lang regression suite to catch any compiler-side site that depended on the bug behavior

## Falsifier (raw#71)

INVALIDATED iff for all (fn, k) pairs in a fuzzer-corpus where `fn() -> int` and `fn()` returns int value `v`, the expression `fn() == k` evaluates to `(v == k)` under BOTH interpreter and AOT modes.

Three concrete falsifier instances (raw#71 plurality):

1. **Direct sentinel**: `fn returns_one() -> int { return 1 }` ; assert `returns_one() == 1` is true under both modes.
2. **Computed value**: `fn count_chars(s: string, c: string) -> int { /* count occurrences */ }` ; assert `count_chars("aaa", "a") == 3` is true under both modes.
3. **Negative sentinel**: `fn digit_val(c: string) -> int { /* returns 0..9 or -1 */ }` ; assert `digit_val("x") == -1` is true under both modes.

**Anti-falsifier**: a single (fn, k) where interpreter says true and AOT says false (or vice versa) fails the fix. The T17 reproducer (`returns_one() == 1`) is the canonical anti-falsifier — if it still prints "got" under AOT after the fix, RFC-009 has not landed.

## Effort Estimate

- LoC: ~50-80 (codegen) + ~50 (regression test) = **~100-130 LoC**
- Hours: **8-14h** (codegen surface investigation + AOT test harness + raw#18 3-stage fixpoint re-proof)
- Higher than RFC-005 because of raw#18 fixpoint re-proof requirement (compiler-side rebuild + binary-equal verification).

## Retire Conditions

- Falsifier passes (all 3 anti-falsifiers + fuzzer corpus) → status `done`
- raw#18 fixpoint re-proof PASS (stage3 binary-equal stage2) → status `done`
- Track A blocked on codegen rewrite cost → fall back to Track B + document Track A as `parked` (Track B alone is partial-mitigation, not full fix; user must remember the let-bind workaround)

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g9-aot-bool-coercion-int-call-eq`

Family relation: same root-cause class as gap-5 (RFC-005). Recommend bundling the codegen-investigation cycle so both `==` dispatch divergences (string slice + int call) land in a single `self/codegen_c2.hexa` fix-cycle.

## Honesty Disclosures (raw#91 C3)

- T17's mk_xii_eeg_corroboration.hexa already deploys the `>= 0` workaround; this RFC documents the upstream gap so the next int-sentinel site does not silently drift.
- The "broader anima audit 18,975 files" number is a UPPER BOUND — most of those matches are built-in calls (`len()`, `file_bytes()`, `argv()` indices) which are NOT confirmed affected. The TRUE at-risk count requires a parser-pass / type-aware audit deferred to a downstream cycle.
- Three plausible root-causes are listed (boxed-vs-unboxed, implicit-bool-coercion, ABI-extract-mismatch); the actual fix may resolve one, two, or all three. RFC body assumes Track A's "audit + ensure value-compare" terminology covers any of the three.
- Track B (lint) is defense-in-depth, not a fix; without Track A, AOT remains silent-wrong on the exact pattern users naturally write.

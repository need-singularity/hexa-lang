# RFC 005 — ends_with() / starts_with() AOT codegen + slice-equality parity

- **Status**: proposed
- **Date**: 2026-04-28
- **Severity**: correctness (interpreter/AOT semantic divergence; silent-wrong class)
- **Priority**: P0 (silent-wrong bug class)
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (gap 5)

## Problem

`s[(n-5)..n] == ".hexa"` works correctly under hexa interpreter mode but fails (returns false) under the AOT-compiled binary. Method-form `s.ends_with(".hexa")` is the only reliable form across modes.

Root cause hypothesis: AOT codegen for slice expression produces a different string-type representation (pointer+len view vs heap-string) than the literal RHS, and `==` dispatches on identity / pointer comparison instead of value comparison.

**Bug class**: ANY string slice compared with `==` is a silent-wrong landmine under AOT.

Reproducer (anima-eeg `calibrate.hexa` v1):
```hexa
let argv0 = argv()[0]
let n = argv0.len()
if argv0[(n-5)..n] == ".hexa" {   // interpreter: true; AOT: false (silent)
    // ...
}
```

Patched form:
```hexa
if argv0.ends_with(".hexa") { ... }   // works in both modes
```

Patched across all 5 anima-eeg files: `calibrate.hexa`, `eeg_recorder.hexa`, `experiment.hexa`, `board_health_check.hexa`, `electrode_adjustment_helper.hexa`.

## Proposed Change

AOT codegen (`self/codegen_c2.hexa`) for string slice equality MUST produce the same semantics as interpreter — value comparison of bytes, NOT pointer/identity.

Two-track:

### TRACK A (root fix) — slice-expression lowering produces a string value
Slice produces a string (or a slice-view that overloads `==` to byte-compare), not a raw pointer-pair that compares by identity.

### TRACK B (defensive) — `ends_with()` / `starts_with()` AOT codegen path
Ensure `s.ends_with(t)` and `s.starts_with(t)` lower to the same byte-compare under AOT as under interpreter, even if the slice-expression root cause persists. (Currently the method-form is the sole working path; this RFC formalizes that as a stable contract regardless of slice-eq fix.)

Recommend Track A; Track B is the fallback if slice-eq lowering proves expensive.

## Compatibility

Track A is a bug fix; existing code that worked in interpreter starts working in AOT. Code that compensated with `ends_with()` continues to work. No breaking change.

## Implementation Scope

- **Investigate + fix** `self/codegen_c2.hexa` (~50 LoC)
  - Locate slice-expression codegen
  - Locate `==`-dispatch for string operands
  - Ensure value-equality semantics (memcmp-equivalent)
- **Regression test**: `test/regression/aot_string_slice_eq.hexa` (~30 LoC)
  - `"hello.hexa"[5..10] == ".hexa"` in both interpreter and AOT
  - Edge cases: empty slice, full slice, off-by-one
  - `s.ends_with(t)` / `s.starts_with(t)` parity

## Falsifier (raw#71)

INVALIDATED iff for all (s, lo, hi, t) pairs in a 1k-fuzzer-corpus, `(s[lo..hi] == t)` returns identical boolean under interpreter and AOT modes.

**Anti-falsifier**: a single (s, lo, hi, t) where interpreter says true and AOT says false (or vice versa) fails the fix.

## Effort Estimate

- LoC: ~50 (codegen) + ~30 (test) = **~80 LoC**
- Hours: **6-10h** (codegen surface investigation + AOT test harness)

## Retire Conditions

- Falsifier passes → status `done`
- Track A blocked → fall back to Track B + document Track A as `parked`

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g5-aot-string-slice-eq`

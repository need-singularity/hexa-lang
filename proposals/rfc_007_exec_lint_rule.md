# RFC 007 — Lint rule: detect `exec(...) == int_literal` silent-fail pattern

- **Status**: proposed
- **Date**: 2026-04-28
- **Severity**: correctness (silent-fail; user-mental-model mismatch)
- **Priority**: P1 (high signal-to-LoC; ~20 LoC catches a bug class found in the wild via anima-eeg audit)
- **Source**: split out from RFC-006 (exec() return-type semantics) after fact-correction discovered the proposed builtins (`exec_status`, `exec_capture`, `exec_both`) already exist as `exec_with_status` / `exec_capture` / `exec_stream`. RFC-006 narrowed to lint+docs; this RFC formalizes the lint rule as a standalone deliverable so it can land independently.

## Problem

hexa runtime `exec(cmd: string)` returns the captured stdout as a string. Common user expectation (POSIX C `system(3)` mental model) is `exec(cmd) -> int` returning the exit code.

Result: the natural-looking pattern silently fails forever:
```hexa
if exec("some-command") == 0 {
    // success path — NEVER taken; exec returns string, not int
}
```
- `string == int` is always `false` in hexa (no type error raised).
- The bug is observed only when the success branch has a visible UI/state change that doesn't happen.
- Class: silent-wrong correctness bug. Same severity class as RFC-005 (AOT slice equality silent-fail).

## Live-fire evidence (raw#159 concrete-evidence preference)

anima-eeg B11 audit (2026-04-28) flagged **9 BUG-class instances** of this pattern across helper modules (calibrate.hexa, eeg_recorder.hexa, experiment.hexa, etc.). All were patched to the correct form `let r = exec_with_status(cmd); if r[1] == 0 { ... }` after the audit caught them — but a hexa-level lint would have caught them at parse time.

## Proposed Change

Add a parser/lint pass that detects:

```
BinaryOp(==|!=, Call(exec, [...]), IntLiteral)
```

Emits a warning at parse time:

```
warning: comparing exec() result with integer — exec() returns stdout (string), not exit code.
  hint: use `let r = exec_with_status(cmd); if r[1] == 0 { ... }` instead.
  ref: proposals/rfc_006_exec_return_type.md (decision tree for exec / exec_with_status / exec_stream / exec_capture / exec_replace)
```

### Coverage scope (in priority order)

1. **Direct comparison** (load-bearing — ~80% of the bug class):
   ```hexa
   if exec(cmd) == 0 { ... }
   if exec(cmd) != 0 { ... }
   exec(cmd) == 1   // any int literal
   ```
2. **Through one let binding** (medium — constant-prop a single hop):
   ```hexa
   let rc = exec(cmd)
   if rc == 0 { ... }   // rc inferred as string from RHS; flag the comparison
   ```
3. **Inverse direction** (`0 == exec(cmd)`) — same rule, commutative.

Out of scope (defer to T3 of RFC-006 or a future RFC):
- Comparison with `bool` (`exec(cmd) == true`) — separate type-error class.
- Comparison through 2+ let hops (data-flow analysis cost > value at this stage).
- Comparison after string concatenation (`exec(cmd) + "x" == ...`) — semantically correct usage of exec-as-string.

### Implementation seam

Two candidate locations:
- **Preferred**: parser post-pass in `self/parser_pass.hexa` (or wherever the existing AST-walk lints live — check `self/lint.hexa`). Walk all `BinaryOp` nodes; if `op in {==, !=}` and one side is `Call(exec, ...)` and the other is `IntLiteral`, emit warning.
- **Alternative**: type-checker pass — once exec()'s return type is in the type table as `string`, the `string == int` comparison is already a type error on paper; surfacing it as a non-fatal warning instead of silent-true-becomes-false is the actual fix.

## Compatibility

Strictly additive (warning-only). No build break. Existing code with the pattern continues to compile; users see a warning prompting the migration.

Optional follow-up: promote warning to error after one cycle of dogfood + fix-up sweep across hive/anima/nexus.

## Implementation Scope

- **Lint rule**: ~20 LoC in self/parser_pass.hexa (or self/lint.hexa)
- **Regression test**: ~20 LoC in test/regression/lint_exec_eq_int.hexa
  - Positive cases: `exec("true") == 0`, `exec("false") != 0`, `let rc = exec(...); rc == 0`
  - Negative cases (must NOT fire): `let body = exec("ls")` (string usage), `exec_with_status(cmd)[1] == 0` (correct form)
- **Total**: ~40 LoC, **1-2h**

## Falsifier (raw#71)

INVALIDATED iff:
1. Lint fires on every positive test case (3+ patterns above).
2. Lint does NOT fire on any negative test case (correct exec_with_status usage, exec used as string).
3. Lint runs across the anima-eeg corpus (9 helpers) and reports 0 instances after they were patched in commit 3c2dc93f (post-audit clean state).
4. Lint runs across hexa-lang's own tree and reports 0 instances (self/main.hexa already uses `exec_with_status` correctly at lines 1700, 1966).

**Anti-falsifier**: any false positive on a correct usage, or any false negative on the canonical bug pattern, fails the proposal.

## Effort Estimate

- LoC: **~40 LoC**
- Hours: **1-2h**

## Retire Conditions

- Lint lands + 1 cycle dogfood with zero false positives → status `done`.
- If lint warning is later promoted to a hard error (after fix-up sweep complete) → file separate "exec() == int as compile error" RFC; this one closes once warning lands.

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g6b-exec-eq-int-lint-rule`

## Cross-references

- RFC-006 (revised): the documentation + decision-tree counterpart to this lint. Both should land together; doc tells users which API to use, lint catches the cases they got wrong.
- raw#159 hexa-lang upstream-proposal-mandate: this RFC is direct downstream of an in-the-wild correctness bug found across 9 production hexa files.

## Honesty Disclosure (raw#91 C3)

This RFC was split out from RFC-006 after RFC-006's original "add 3 new builtins" framing was retracted (the proposed builtins already exist). The lint rule is the most actionable, smallest-surface deliverable from the original RFC and benefits from standing as its own document — independent priority (P1, vs RFC-006's P2 for docs), independent landing, independent falsifier.

No new runtime functionality required; this is pure compile-time analysis.

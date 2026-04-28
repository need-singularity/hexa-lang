# RFC 006 — exec() return-type semantics: clarify existing 3-API surface + add lint

- **Status**: proposed (CORRECTED 2026-04-28; see Correction Notice below)
- **Date**: 2026-04-28 (initial); 2026-04-28 (corrected — same day)
- **Severity**: correctness (silent-fail; user-mental-model mismatch)
- **Priority**: P2 (was P1; lowered after correction — implementation cost dropped from ~90 LoC new builtins to lint+docs only)
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (6th gap, surfaced during anima-eeg B11 eeg_setup unified menu work)

## Correction Notice (raw#91 C3 — honest disclosure)

**Original RFC (2026-04-28 commit 2cb2b95c) PROPOSED 3 NEW BUILTINS** — `exec_capture(cmd) -> string`, `exec_status(cmd) -> int`, `exec_both(cmd) -> (int, string)`.

**Fact discovered after landing the RFC**: the hexa runtime ALREADY exposes the equivalent surface:

| Existing builtin | Returns | Source |
|------------------|---------|--------|
| `exec(cmd)` | `string` (stdout only) | runtime.c:3145 / self/rt/proc.hexa:111 |
| `exec_with_status(cmd)` | `[stdout: string, exit_code: int]` array | runtime.c:3235 / self/rt/proc.hexa:66 |
| `exec_stream(cmd, on_line)` | `int` (exit code) — line-by-line callback | runtime.c:3182 |
| `exec_capture(cmd)` | `[stdout: string, stderr: string, exit_code: int]` — separate stderr | runtime.c:6373 |
| `exec_replace(cmd)` | replaces current process (no return on success) | runtime.c:3277 |

Live evidence — `exec_with_status` is already in production use across **9 anima-eeg helpers** (calibrate.hexa:340, eeg_recorder.hexa:367, experiment.hexa:416, collect.hexa:316, realtime.hexa:414, eeg_brainflow_sanity.hexa:282, eeg_ftdi_latency_fix.hexa:270, etc.) and inside hexa-lang itself (self/main.hexa:1700, 1966 for AOT preprocessor invocation).

**Reason the original RFC missed this**: the author audited the user-facing `exec()` surface and the recently-added `exec_stream()` but did not search the runtime.c symbol table for `hexa_exec_*` companions. raw#91 C3: this is a research-discipline failure, disclosed and corrected here rather than silently rewriting history.

**RFC scope therefore narrows from "add 3 new APIs" to "prevent mis-use of the existing 3-API surface via lint + docs"**. See Proposed Change below.

## Problem (UNCHANGED — still valid)

hexa runtime `exec(cmd: string)` returns the captured stdout as a string. Common user expectation (POSIX C `system(3)` mental model) is `exec(cmd) -> int` returning the exit code.

Result: the natural-looking pattern
```hexa
if exec("some-command") == 0 {
    // success path
}
```
**silently fails forever** — `string == int` is always false in hexa, so the success branch is never taken. No type error is raised; the bug is observed only when the success path is a visible UI/state change that doesn't happen.

This is the bug-class the RFC targets. It is real and reproduced in the anima-eeg B11 eeg_setup early draft.

## Proposed Change (CORRECTED — was "add APIs", now "lint + document")

Three sub-tracks. T1 is the load-bearing landing; T2/T3 are companions.

### T1 — Lint rule: detect `exec(...) == int_literal` pattern (NEW; was T-A in original RFC)

Hexa parser/lint emits a warning at parse time when it sees:
```hexa
exec(...) == 0       // warning: exec() returns string; did you mean exec_with_status(cmd)[1] == 0?
exec(...) == 1
exec(...) != 0       // (also flagged)
let rc = exec(...); if rc == 0 { ... }   // simple flow-through case (constant-prop, optional)
```
Fix-it suggestion in the warning text:
```
warning: comparing exec() result with integer — exec() returns stdout (string), not exit code.
  hint: use `let r = exec_with_status(cmd); if r[1] == 0 { ... }` instead.
  ref: proposals/rfc_006_exec_return_type.md
```

Implementation seam: parse-tree post-pass over all `BinaryOp(==|!=, Call(exec, ...), IntLiteral)` nodes. ~20 LoC in self/lint or self/parser_pass.hexa.

### T2 — Documentation: stdlib/proc.hexa header comment + cheat-sheet

Extend the doc-comment block at the top of `self/rt/proc.hexa` (currently lines 1-15) with an explicit decision tree:

```
# Choose the right exec variant:
#   exec(cmd)              → string             — quick stdout grab; DON'T compare with == int
#   exec_with_status(cmd)  → [stdout, exit_code] — when you need both
#   exec_stream(cmd, cb)   → int (exit code)     — long-running / line-by-line stream
#   exec_capture(cmd)      → [out, err, rc]      — when stderr must stay separate
#   exec_replace(cmd)      → does not return     — final-tail handoff (e.g. lsp)
```

Co-locate this cheat-sheet in `doc/stdlib_proc.md` (NEW; ~30 lines).

### T3 — Future API consolidation (DEFERRED, no implementation in this RFC)

If post-lint dogfood still shows the pattern recurring, evaluate:
- Adding `exec_status(cmd) -> int` as a thin wrapper (`exec_with_status(cmd)[1]`) for ergonomic parity with the user's mental model.
- OR deprecating `exec()` entirely in favor of `exec_with_status` and `exec_stream` as the canonical surface.

Both are punted out of this RFC's landing scope. The existing 5-API surface is sufficient; the bug is documentation / lint, not API.

## Compatibility

Strictly additive. No breaking change. No new builtins. No runtime.c modification.

## Implementation Scope (LOWERED from original RFC)

| Item | Original RFC | Corrected RFC |
|------|--------------|---------------|
| New builtins (exec_status, exec_capture, exec_both) | ~30 LoC | **0 LoC** (already exist with different names) |
| AOT codegen parity | ~10 LoC | **0 LoC** (already wired) |
| Lint hook (`exec() == int_literal`) | ~20 LoC | **~20 LoC** (kept) |
| Doc updates (stdlib_proc.md + proc.hexa header) | not present | **~30 LoC** (added) |
| Regression test (lint warning fires; doc cheat-sheet sample compiles) | ~30 LoC | **~20 LoC** (lint-fires test only) |
| **TOTAL** | **~90 LoC** | **~70 LoC** |
| **Hours** | 4-6h | **2-3h** |

## Falsifier (raw#71) — UPDATED

INVALIDATED iff:
1. Lint emits a warning for every `exec(cmd) == 0` / `exec(cmd) != 0` / `exec(cmd) == N` pattern in the anima-eeg + hexa-lang corpus.
2. Lint does NOT fire for the correct pattern `exec_with_status(cmd)[1] == 0`.
3. Lint does NOT fire on unrelated code (no false positive on `exec("...")` used as string, e.g. `let body = exec("ls")`).
4. Documentation cheat-sheet examples are buildable as a hexa regression test (each variant invoked once, asserts shape of return).

**Anti-falsifier**: any case where the lint silently misses the pattern, or fires on a correct usage, fails the proposal.

## Effort Estimate

- LoC: ~20 (lint) + ~30 (docs) + ~20 (test) = **~70 LoC**
- Hours: **2-3h** (was 4-6h)

## Retire Conditions

- Lint lands + 1 cycle dogfood across anima-eeg with zero false positives → status `done`.
- If T3 (deprecate exec() or add exec_status alias) is later pursued, file as a separate RFC; this one closes once T1+T2 land.

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g6-exec-return-type-lint-and-docs` (renamed from `-semantics` after correction).

## Honesty Disclosure (raw#91 C3)

**Original RFC research gap**: I proposed adding `exec_status` / `exec_capture` / `exec_both` as new builtins without searching the runtime.c symbol table; had I done so, I would have found `hexa_exec_with_status` (runtime.c:3235), `hexa_exec_stream` (runtime.c:3182), and `hexa_exec_capture` (runtime.c:6373) — all 3 of the proposed builtins already exist (one with a different name: `exec_with_status` instead of `exec_both`; another with extra capability: `exec_capture` returns 3-tuple [out, err, rc] not 2-tuple).

**Live-use evidence I missed**: 9 anima-eeg helpers were already calling `exec_with_status` correctly at the time the original RFC was written. The pattern `let r = exec_with_status(cmd); if r[1] == 0 { ... }` is in production. The only thing missing is **discoverability** (no doc cheat-sheet, no lint to redirect users from `exec(cmd) == 0`).

**Why the correction is being filed as an in-place RFC rewrite, not a "supersedes" RFC**: the original RFC document landed 30 minutes ago; no downstream consumer has acted on it yet. raw#10 honest-disclosure: rewrite + correction-notice block at top is more truthful than leaving misleading content in proposals/. Original commit 2cb2b95c remains in git history for audit.

**Cross-link**: raw#91 C3 retraction filed; this RFC supersedes the original "add 3 builtins" framing. Convergence file gap 6 entry will be updated to match.

# RFC 006 — exec() return-type semantics: string capture vs int exit-code

- **Status**: proposed
- **Date**: 2026-04-28
- **Severity**: correctness (silent-fail; user-mental-model mismatch)
- **Priority**: P1
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (NEW — 6th gap, surfaced during anima-eeg B11 eeg_setup unified menu work)

## Problem

hexa runtime `exec(cmd: string)` returns the captured stdout as a string. Common user expectation (POSIX C `system(3)` mental model) is `exec(cmd) -> int` returning the exit code.

Result: the natural-looking pattern
```hexa
if exec("some-command") == 0 {
    // success path
}
```
**silently fails forever** — `string == int` is always false in hexa, so the success branch is never taken. No type error is raised; the bug is observed only when the success path is a visible UI/state change that doesn't happen.

Existing alternatives:
- `exec_stream(cmd, on_line) -> int` — line-by-line streaming + int exit code (introduced for B11 use-case)
- `exec(cmd) -> string` — captures stdout, ambiguous on exit code

Reproducer (anima-eeg `eeg_setup.hexa` early draft):
```hexa
// User-intent: run subcommand, branch on success
if exec("python3 calibrate.py --validate") == 0 {
    println("calibration OK")
}
// Observed: success message NEVER printed even when calibrate.py exits 0.
// Reason: exec() returns "calibration data: ..." (stdout), not 0.
```

## Proposed API

Two-track. Both can land; pick one as canonical.

### TRACK A (additive, non-breaking) — explicit alias
Add new builtins with unambiguous names:

| Builtin | Returns | Captures stdout | Captures exit-code |
|---------|---------|-----------------|--------------------|
| `exec_capture(cmd)` | `string` | YES | NO (use `exec_status` if needed) |
| `exec_status(cmd)` | `int` | NO (passthrough) | YES |
| `exec_both(cmd)` | `(int, string)` tuple | YES | YES |
| `exec_stream(cmd, on_line)` | `int` | line-by-line callback | YES |

Existing `exec(cmd) -> string` preserved for backward compat (status: deprecated; emit lint when used; remove in N+2 cycles).

### TRACK B (breaking, cleaner) — change exec() signature
Make `exec(cmd) -> (int, string)` (tuple of exit-code + captured stdout). Migrate all callers.

Recommend Track A — additive landing now; Track B optional cleanup later.

## Compatibility

Track A: strictly additive, no breaking change. Existing `exec()` callers unchanged. New `exec_status()` / `exec_capture()` opt-in by name.

Track B: breaking; requires migration sweep of every `let body = exec(cmd)` call site across hive/anima/nexus/hexa-lang.

## Implementation Scope

- **NEW builtins** in `self/main.hexa` (~30 LoC)
  - `exec_capture(cmd: string) -> string` (alias of current `exec`)
  - `exec_status(cmd: string) -> int` (passthrough stdout/stderr to inherited fds; return waitpid status)
  - `exec_both(cmd: string) -> (int, string)`
- **AOT codegen** parity (~10 LoC)
- **Lint hook**: deprecate raw `exec()` calls with mixed-mode comparison patterns (`exec(...) == int_literal`) — emit warning at parse time (~20 LoC)
- **Regression test**: `test/regression/exec_status_capture.hexa` (~30 LoC)
  - `exec_status("true") == 0`
  - `exec_status("false") != 0`
  - `exec_capture("echo hi") == "hi\n"`
  - `exec_both("echo x") == (0, "x\n")`

## Falsifier (raw#71)

INVALIDATED iff:
1. `exec_status(cmd)` returns the integer exit code (0 for success, nonzero for failure) for a 100-command corpus covering true/false/echo/grep/sh-c
2. `exec_capture(cmd)` returns the exact stdout bytes (matching current `exec(cmd)`)
3. Both behaviors identical in interpreter AND AOT

**Anti-falsifier**: any case where `exec_status` returns something other than the wait-status int, or `exec_capture` diverges from `exec`, fails the proposal.

## Effort Estimate

- LoC: ~30 (builtins) + ~10 (codegen) + ~20 (lint) + ~30 (test) = **~90 LoC**
- Hours: **4-6h**

## Retire Conditions

- Track A falsifier passes + 1 cycle of dogfood across anima-eeg → status `done`
- Track B chosen instead → status `superseded by Track B`
- `exec()` deprecated and removed (Track B-2) → this RFC closes its lifecycle

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g6-exec-return-type-semantics`

## Honesty Disclosure (raw#91 C3)

This is a NEW (6th) gap not in the original 5-gap convergence file. Surfaced 2026-04-28 during anima-eeg B11 eeg_setup unified-menu dispatcher work where the natural `if exec(cmd) == 0` pattern silently failed. Workaround `exec_stream(cmd, on_line) -> int` was used in production; this RFC formalizes the broader semantics fix.

# RFC 008 — Project-relative helper invocation primitive (`project_python()` / `project_helper_runner()`)

- **Status**: done (P1 Form A landed 2026-04-28)
- **Date**: 2026-04-28
- **Severity**: correctness (silent drift across helpers — venv-dependent project class)
- **Priority**: P1 (anima-eeg-class venv-dependent projects: critical; general projects: ergonomic)
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (gap 7 — added 2026-04-28 post-RFC-007 landing)
- **Source session**: anima-eeg D-day post-landing (2026-04-28) — clm_eeg_lz76_real.hexa + eeg_recorder.hexa helper-PATH gaps surfaced same day

## Problem

hexa runtime `exec()` / `exec_with_status()` / `exec_stream()` resolve subprocess commands via the inherited PATH. For projects that pin their helper interpreter to a project-local venv (Python virtualenv, Node nvm-managed runtime, etc.), there is **no canonical primitive to obtain the project's helper interpreter path**. Every helper hard-codes the venv path, or sets an env var, or falls back to system `python3` — and silently drifts when one helper is forgotten.

Concrete failure mode (anima-eeg, 2026-04-28):
- BrainFlow + supporting telemetry libs are installed only inside `.venv-eeg/bin/python` (project venv).
- A hexa helper that calls `exec("python3 helper.py ...")` finds the **system** `python3`, which lacks BrainFlow.
- Result: helper-py raises `ModuleNotFoundError: brainflow` and the tool's segment FAILs with no obvious cause from the hexa side (raw#10 honest-warning fires after the failure, not before).

## Live-fire evidence (raw#159 concrete-evidence preference)

Two same-day failures in anima-eeg (2026-04-28):

1. **clm_eeg_lz76_real.hexa**
   - Symptom: `python3 /Users/ghost/.hx/bin/python3` not found path.
   - Root: helper resolved a non-existent `python3` shim in the hexa toolchain dir.
   - Fix: `@resolver-bypass` annotation + hard-code `.venv-eeg/bin/python`.
   - Commit: a8392fdc.

2. **eeg_recorder.hexa**
   - Symptom: `python3 /tmp/anima_eeg_recorder_helper.hexa_tmp` resolved system `python3` → no BrainFlow → segment FAIL.
   - Root: same class — system `python3` chosen by PATH lookup, lacks venv libraries.
   - Fix: in progress (this RFC originates from that fix-cycle).

3. **B11 eeg_setup unified-menu dispatcher** (commit 3c2dc93f, same day)
   - All 6 dispatched helpers had to be patched individually to use `.venv-eeg/bin/python` rather than `python3`.
   - 6 distinct sites = 6 silent-drift surfaces if one is missed in future edits.

Common root cause: hexa runtime `exec()` PATH lookup for helper interpreters has no project-aware indirection. Each helper reinvents the venv-path lookup. **Drift is the default.**

## Proposed API

### Form A — Python-specific (smallest landing)

```hexa
use "self/stdlib/proc"

let py = project_python("/Users/ghost/core/anima")
let cmd = py + " /tmp/helper.py --flag"
let r = exec_with_status(cmd)
```

Resolution order (raw#10 fallback chain, fail-loud at the end):
1. `<project_root>/.venv-eeg/bin/python` — anima-eeg pattern (most specific)
2. `<project_root>/.venv/bin/python` — Python standard
3. `ENV ANIMA_EEG_VENV_PYTHON` — explicit override
4. `which python3` from PATH — last resort, **emits raw#10 honest-warning to stderr** noting that no project-local interpreter was found

### Form B — Generalized helper-kind dispatch (broader scope)

```hexa
use "self/stdlib/proc"

let runner = project_helper_runner("/Users/ghost/core/anima", "python")
// helper_kind: "python" | "node" | "lua" | "ruby" | ...
// returns absolute path to project's helper interpreter, fallback chain by kind
```

Recommend landing **Form A first** as a thin wrapper; promote to Form B in a follow-up only if a 2nd helper-kind (Node, Lua, …) materializes a concrete need.

## Compatibility

Strictly additive. Existing helpers using hard-coded `.venv-eeg/bin/python` continue to work unchanged. New helpers gain a single canonical resolution surface.

No runtime behavior change for `exec()` / `exec_with_status()` / `exec_stream()`. This RFC adds a stdlib helper that **returns a path string**; the caller still passes that string to the existing exec primitives.

## Implementation Scope

- **NEW (or extend)** `self/stdlib/proc.hexa` (or `self/stdlib/path.hexa` if proc.hexa is reserved for exec primitives): `~30 LoC`
  - `fn project_python(project_root: string) -> string`
  - Optional Form B: `fn project_helper_runner(project_root: string, kind: string) -> string`
- **Regression test**: `test/regression/project_python_resolution.hexa` (~30 LoC)
  - Positive: project_root contains `.venv-eeg/bin/python` → returns that path
  - Positive: project_root contains `.venv/bin/python` (no .venv-eeg) → returns that path
  - Positive: `ANIMA_EEG_VENV_PYTHON` env set, no venv dirs → returns env value
  - Fallback: no venv, no env → returns `which python3` result + raw#10 warning emitted to stderr
- **Total**: ~60 LoC, **1-2h**

No new builtin needed. No runtime.c change. Pure stdlib + filesystem stat.

## Falsifier (raw#71)

INVALIDATED iff:
1. `project_python(root)` returns the correct path under all 4 fallback-chain conditions on a synthetic test corpus (4 setups, 4 expected paths).
2. Fallback-4 (system `python3`) emits a raw#10 honest-warning to stderr (visible to caller) rather than silently returning the system path.
3. anima-eeg's 6 helpers (B11 eeg_setup dispatch family) can migrate from hard-coded `.venv-eeg/bin/python` to `project_python(...)` without any behavioral regression in selftest suite.
4. Across hive/anima/nexus tree, 0 false-positive resolutions (no project_root mistakenly resolving to an unrelated venv via path-walk surprises).

**Anti-falsifier**: any case where `project_python(root)` returns a path to an interpreter that lacks the project's pinned dependencies (i.e. silent drift continues), or any case where the system-python fallback fires without emitting the warning, fails the proposal.

## Falsifier — RFC-retire (raw#71 retire-rule)

Retire conditions (RFC becomes unnecessary, not just done):

- All projects in hive/anima/nexus that depend on a project-local interpreter use venv (`.venv-eeg/` or `.venv/`) consistently → fallback-1/-2 always fires → no need for the env override branch. Trim Form A.
- After 5+ projects have stabilized on the venv-pattern, audit shows **zero silent-drift bugs** for 1 month → RFC marked `retired` (the convention is enforced by code review + memory pointers, not by stdlib helper).
- Alternative: a hexa-lang-wide environment manifest convention (e.g. `hexa.toml [helpers]`) supersedes per-call resolution → mark `superseded`.

## Effort Estimate

- LoC: **~60 LoC** (30 stdlib + 30 regression test)
- Hours: **1-2h**

## Retire Conditions

- Falsifier passes + 6 anima-eeg helpers migrated → status `done`.
- 5+ project-corpus audit shows zero silent drift for 1 month → status `retired` (convention-only enforcement sufficient).
- Hexa-lang-wide manifest-based helper resolution lands → status `superseded`.

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g7-project-relative-helper-invocation`

## Cross-references

- RFC-001 (popen_lines): same `exec()` family. RFC-008 affects **which** binary is exec'd; RFC-001 affects **how** stdout is consumed. Orthogonal but co-resident in `self/stdlib/proc.hexa`.
- RFC-006 / RFC-007 (exec semantics + lint): pattern parallel — exec family has multiple silent-drift surfaces (return-type, helper-PATH). This RFC addresses the helper-PATH surface; RFCs 006/007 address the return-type surface.
- raw#159 hexa-lang upstream-proposal-mandate: same-day in-the-wild evidence (2 anima-eeg helpers + 1 dispatcher-family of 6) qualifies as concrete-evidence-preferred.

## Honesty Disclosure (raw#91 C3)

- **Scope honesty**: this RFC originates **mid-fix** for eeg_recorder.hexa (still in progress as of 2026-04-28T19:23). The fix landing in anima-eeg is independent of this RFC; anima-eeg can hard-code the venv path today and the RFC is the upstream mandate (raw#159) to prevent the next helper from drifting.
- **Generality honesty**: Form B (helper-kind dispatch) is speculative — only Python is in-evidence. Recommend landing Form A first; defer Form B until a 2nd helper-kind shows up in the wild.
- **Fallback-4 raw#10 surface**: the system-python fallback path **must** emit the warning; if implementation silently falls through without warning, the RFC is anti-falsified (the warning is the load-bearing safety net). Implementer must dogfood the warning surface in the regression test.
- **No silent-drift claim**: this RFC does not eliminate the possibility of project_python returning a venv that itself lacks the needed dependencies. It only eliminates the case where **the wrong interpreter** is chosen. Dependency-completeness in the venv is a project-side discipline (e.g. `.venv-eeg/requirements.txt` audit).

## Landing Record (2026-04-28)

- **Form A landed** in `self/stdlib/path.hexa` (was tentatively proposed for `self/stdlib/proc.hexa`; chose `path.hexa` because it is a path-resolution operation, not an exec primitive — `proc.hexa` does not yet exist as a separate module).
- **LoC added**: ~63 stdlib (function + doc-comment) + ~52 selftest = ~115 LoC total.
- **Selftest**: `__PATH_SELFTEST__ PASS n=25 fail=0` (was n=20 pre-RFC-008; +5 cases for project_python: .venv-eeg / .venv / preference-order / fallback-4 / trailing-slash idempotency).
- **Live evidence**:
  - `project_python("/Users/ghost/core/anima")` → `/Users/ghost/core/anima/.venv-eeg/bin/python` (correct).
  - No-venv → `python3` + raw 10 stderr warn fires (verified eyeball — fallback-4 anti-falsifier check satisfied).
  - `ANIMA_EEG_VENV_PYTHON=/custom/path` override → `/custom/path` (step-3 env override).
  - Generic `PROJECT_PYTHON=/generic/python` also honored (added per raw 91 generality concern — anima-eeg-only naming was too narrow).
- **raw 18 self-host fixpoint**: 0 impact. `self/stdlib/path.hexa` is not imported by the self-host compiler chain (verified via grep over `self/`); module is consumer-side only.
- **Form B (helper-kind dispatch) deferred**: as recommended in §"Proposed API". Will land iff a 2nd helper-kind (Node/Lua/Ruby) shows in-the-wild evidence.
- **anima-eeg helper migration**: deferred to anima-eeg side commits. Sample migration call form: `let py = project_python("/Users/ghost/core/anima")` then `exec(py + " /tmp/helper.py ...")` replaces hard-coded `.venv-eeg/bin/python`.
- **Cross-platform note**: implementation is POSIX-only (`bin/python` shim). Linux uses identical `.venv-eeg/bin/python` layout — verified by inspection of venv standard. Windows (`Scripts/python.exe`) is out of scope per RFC-008 §"Compatibility" (hexa runtime target is Mac + Linux per `path.hexa` header).

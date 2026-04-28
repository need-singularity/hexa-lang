# RFC 001 — popen_lines() / process_spawn() streaming subprocess stdlib

- **Status**: partial-landed (capture-and-split wrapper) — streaming form deferred
- **Date**: 2026-04-28
- **Severity**: BLOCKER
- **Priority**: P0
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (gap 1)
- **Source session**: anima-eeg D-day (electrode_adjustment_helper.hexa --live mode iter1-iter4)

## Implementation Status (2026-04-28 update)

- **`self/stdlib/proc.hexa`** landed: `popen_lines(cmd) -> [string]` and
  `popen_lines_with_status(cmd) -> [[string], int]` as hexa-only wrappers
  over the existing `exec_with_status` builtin. Selftest 5/5 PASS
  (`hexa run self/stdlib/proc.hexa --selftest`). Zero impact on raw#18
  self-host fixpoint (new file, not referenced from `self/main.hexa`
  bootstrap chain).
- **NOT landed (raw#10 honest C3)**: the streaming iterator form
  (`for line in popen_lines(...)` consuming stdout incrementally as
  the subprocess emits) and `process_spawn() -> Process` handle. The
  current wrapper buffers ALL stdout via `exec_with_status` and THEN
  splits — it does NOT satisfy this RFC's falsifier (sub-second per-line
  latency while subprocess still alive). True streaming requires a new
  C builtin (pipe + fork + readline loop) and is tracked as a separate
  cycle.
- **Anima-eeg gap status**: closed for the find/grep/git-log line-by-line
  capture use case (the dominant pattern). NOT closed for the
  `electrode_adjustment_helper.hexa --live` per-tick streaming case.

## Problem

hexa `exec(cmd)` blocks until the subprocess exits and captures all stdout into a single buffer. There is no way to consume stdout line-by-line as the subprocess emits it. No `popen_lines()` iterator. No Process handle exposing `stdout.read_line()`.

This forces real-time tools to either:
- Embed their entire render loop inside a helper Python (hexa wrapper becomes a thin invoker — defeats raw#9 hexa-only-strict spirit)
- Re-spawn a one-shot subprocess per tick (5-10s/tick overhead + flicker)

Reproducer (anima-eeg `electrode_adjustment_helper.hexa --live`):
```hexa
// Current — blocks until helper.py exits, returns all stdout at once
let body = exec("python3 helper.py --stream")
// Cannot consume body line-by-line as helper writes it.
```

Iteration log (parent convergence, lines 55-58):
- iter1: ANSI clear + per-tick re-spawn → 5-10s/tick + flicker
- iter2: single-session helper-py with internal loop (hexa wrapper just `exec()`s once)
- iter3-iter4: entirely inside Python because hexa cannot stream

## Proposed API

### Iterator form (preferred ergonomics)
```hexa
use "self/stdlib/process"

for line in popen_lines("python3 helper.py --stream") {
    handle(line)   // consumed as subprocess emits, no buffering
}
```

### Handle form (full control)
```hexa
use "self/stdlib/process"

let proc = process_spawn("python3 helper.py --stream")
while proc.is_alive() {
    let line = proc.stdout.read_line()   // returns "" on EOF
    if line == "" { break }
    handle(line)
}
let rc = proc.wait()   // collect exit code + ensure resources freed
```

## Compatibility

Strictly additive. Existing `exec(cmd)` semantics unchanged. New stdlib module `self/stdlib/process` is opt-in via `use`. No tool currently relies on streaming behavior (precisely the gap).

## Implementation Scope

- **NEW file**: `self/stdlib/process.hexa` (~150-300 LoC)
  - `popen_lines(cmd: string) -> Iterator<string>`
  - `process_spawn(cmd: string) -> Process`
  - `Process { stdout, stderr, is_alive(), wait(), kill() }`
- **Builtin support** in `self/main.hexa` (~50 LoC)
  - Interpreter: lower to `subprocess.Popen` with line-iterator
  - AOT: lower to `pipe(2)` + `fork(2)` + `readline` loop
- **AOT codegen** for fork/pipe primitives (~30 LoC)
- **Regression test** (interpreter + AOT dual-run): `test/regression/process_popen_lines.hexa` (~50 LoC)

## Falsifier (raw#71)

INVALIDATED iff a working hexa-only `popen_lines()` is demonstrated to:
1. Stream subprocess stdout line-by-line with sub-second per-line latency (< 1s observed gap between subprocess writeln and hexa for-body entry)
2. Survive the anima-eeg --live use-case: 1-second tick cadence, sustained ≥5min, no buffer accumulation

**Anti-falsifier**: any solution that buffers > 1 line or blocks until subprocess exit fails the proposal.

## Effort Estimate

- LoC: ~250 (stdlib) + ~80 (builtin/codegen) + ~50 (test) = **~380 LoC**
- Hours: **8-12h** (1 session including AOT debugging + regression harness)

## Retire Conditions

- Falsifier passes → status `done`
- Alternative landed (e.g., async I/O subsystem subsuming this) → status `superseded`
- 2 successive cycles without progress + no real-time tool blocked → status `parked` (revisit-when)

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g1-streaming-subprocess-popen-lines`

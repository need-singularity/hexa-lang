# `proc`

_supervised OS-process spawn + .resource registration_

**Source:** [`stdlib/proc.hexa`](../../stdlib/proc.hexa)  
**Module slug:** `proc`  
**Description:** spawn long-running OS processes with auto-register + lease-TTL supervision  

**Usage:**

```hexa
import stdlib/proc; let pid = proc_spawn_supervised("long_cmd", 600, "why this process exists")
```

## Overview

PROBLEM (2026-04-25 convergence event)
  Hexa's builtin `exec()` is synchronous (popen). Long-running spawns
  happen via `&` backgrounding in shell — when the hexa parent exits,
  the child becomes an orphan parented to launchd. Three concrete
  accidents this pattern produced:
    · vitest worker pools (2 × 9 procs each) hung after the parent
      claude session moved on — 18 orphaned workers.
    · hexa_interp compiles stuck, parented to nothing.
    · 11h hive coding-agent on /dev/ttys007 long after session close.

STRATEGY (lease-based supervision, Phase 2 of Plan B 2026-04-25)
  Every supervised spawn:
    1. Runs through `setsid` so the child + its worker tree sit in a
       fresh process-group (pgid == child pid).
    2. Is registered in the project's `.resource` SSOT with a short
       `lease-ttl` (default 300s). Owner renews via `proc_lease_renew`
       while alive; dies silently if owner crashes.
    3. The reaper (`tool/resource_reaper.hexa`) running on a cron or
       launchd timer SIGTERMs any entry whose lease expired. Because
       we recorded `pgid`, killpg wipes the whole worker tree in one
       signal — vitest-style fanout can't leak.

WHY NOT PDEATHSIG / kqueue NOTE_EXIT
  Would be tighter (zero-lag parent-death detection) but requires a
  new hexa builtin + runtime.c change. Phase 2 stays pure-hexa. A
  future enhancement can swap the shell launcher for a C-level
  spawn_supervised() builtin without changing this module's API.

── config / env ────────────────────────────────────────────────
Path to the .resource file to register/renew against. Defaults to
$PWD/.resource so hexa scripts inherit the project's SSOT. Override
with $HEXA_PROC_RESOURCE for non-cwd scripts (CI runners, launchd).

## Functions

| Function | Returns |
|---|---|
| [`proc_run_with_stdin`](#fn-proc-run-with-stdin) | `string` |
| [`proc_run_json_bridge`](#fn-proc-run-json-bridge) | `_inferred_` |

### <a id="fn-proc-run-with-stdin"></a>`fn proc_run_with_stdin`

```hexa
pub fn proc_run_with_stdin(cmd: string, stdin_str: string) -> string
```

**Parameters:** `cmd: string, stdin_str: string`  
**Returns:** `string`  

── structured-concurrency pattern (interpreter today, codegen TODO) ──

Hexa has `scope { }` and `defer { }` keywords parsed by the compiler.
Their runtime semantics are implemented in the interp interpreter
(HEXA_CACHE=0 or explicit interpreter mode) — `defer` is function-
scoped, LIFO, and runs on function return; `scope { }` is a nested
block that shares the enclosing function's defer stack.

AOT codegen (default hexa run / hexa build path) does NOT yet emit
cleanup code for DeferStmt / ScopeStmt — it errors with "unhandled
stmt kind: DeferStmt". Tracked as Phase 3 codegen follow-up of the
2026-04-25 Plan B spawn-safety initiative.

Recommended pattern (interpreter-mode today):

  fn run_workload() {
      let pid = proc_spawn_supervised("long_cmd", 300, "why this exists")
      defer { proc_reap(pid) }
      // ... do work while the spawned process runs ...
  } // defer fires → pid SIGTERMed + .resource entry removed

Codegen-mode fallback (until Phase 3 codegen lands):

  let pid = proc_spawn_supervised("long_cmd", 300, "why")
  do_work_using(pid)
  proc_reap(pid)

Even without defer, the Phase 1 reaper (`tool/resource_reaper.hexa`)
running on a cron / launchd timer is the backstop — lease-TTL
expiration SIGTERMs any entry whose owner forgot to reap.

── synchronous stdin-pipe + JSON bridge (P0 — qmirror substrate) ──

Source: anima/docs/hexa_lang_attr_review_for_qmirror_2026_05_03.md §5.1, §5.2

PROBLEM (qmirror engine_aer ↔ aer_runner.py)
  Hexa's builtin `exec(cmd)` is synchronous popen but provides NO
  stdin pipe. To send a QASM3 string (or any payload >256B) to a
  python subprocess today the caller must hand-build a heredoc inside
  the shell string — `exec("python3 ... <<'EOF'\n" + qasm + "\nEOF")`
  — which (a) blows past macOS ARG_MAX (~256KB) on big circuits,
  (b) requires escaping every single quote / dollar sign in payload,
  (c) leaks the payload into `ps` output (security smell).

FIX (additive, no runtime.c change)
  1. proc_run_with_stdin(cmd, stdin_str) — spill stdin_str to a temp
     file, exec `cmd < tmpfile`, unlink tmp. Capacity = filesystem.
  2. proc_run_json_bridge(cmd, payload) — json_stringify payload,
     pipe via #1, json_parse stdout. Returns hexa map. On parse
     failure returns {"ok": 0, "error": "..."} — Result-style stays
     compatible with nexus convention (`ok: int + message: str`).

Breaking risk: 0. Both functions are new symbols; no existing call
site is modified. `exec()` callers untouched.

Caveats (raw#91):
  · Temp-file write is not atomic against concurrent invokers; the
    nonce uses now_epoch + len(stdin_str) which collides only if two
    calls in the same wall-clock second happen with identical-length
    payloads. For nexus single-threaded sampler/engine flow this is
    adequate; a future C-level `popen3` builtin can supplant.
  · stdout is captured via `2>&1` — caller cannot distinguish stderr
    from stdout. Acceptable for JSON-bridge contract (python helpers
    write JSON to stdout, log to stderr, but the bridge expects stdout
    to BE the JSON; mixing is the helper's bug, not the wrapper's).
  · Returns the entire output as a single string — no streaming.
    Same trade-off as the existing `exec()` builtin.

proc_run_with_stdin(cmd, stdin_str) -> string

Run `cmd` synchronously with `stdin_str` piped on STDIN. Returns
stdout (merged with stderr via `2>&1`).

Implementation: spill stdin_str to /tmp/hexa_proc_stdin_<nonce>.txt,
exec `cmd < tmpfile`, unlink tmp. Sidesteps ARG_MAX + shell escaping.

On any internal failure the wrapper still returns whatever exec()
produced — caller inspects exit status via the JSON contract or by
re-invoking with explicit `; echo $?`.

### <a id="fn-proc-run-json-bridge"></a>`fn proc_run_json_bridge`

```hexa
pub fn proc_run_json_bridge(cmd: string, payload)
```

**Parameters:** `cmd: string, payload`  
**Returns:** _inferred_  

proc_run_json_bridge(cmd, payload) -> map

Serialize `payload` (any hexa value json_stringify accepts) to JSON,
pipe to `cmd` via proc_run_with_stdin, parse stdout as JSON. Returns
the parsed map.

Error contract (parse failure or non-map result):
  {"ok": 0, "error": "bridge: <reason>; got <head-of-stdout>"}
Caller can gate on `result["ok"] == 0` without crashing on missing
keys — has_key + index, the json_object_get_int idiom.

On success the returned map is whatever the python helper emitted;
the wrapper does NOT inject `ok: 1` (helpers commonly include their
own ok / status field already).

---

← [Back to stdlib index](README.md)

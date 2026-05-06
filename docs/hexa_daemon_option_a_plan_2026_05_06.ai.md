---
doc: hexa_lang.daemon_option_a_plan_2026_05_06
kind: design_plan
audience: [human, agent]
date: 2026-05-06
mk: 1
status: planning
parent: docs/hexa_speedup_brainstorm_2026_05_06.ai.md (commit 9cc0f685) §H.02
predecessor_mvp: scripts/hexa_daemon.sh (commit b5560a74)
---

# hexa daemon — Option A plan (native daemon-serve + AST cache)

## 1. Motivation

The MVP daemon (`scripts/hexa_daemon.sh` + `hexa_daemon_handler.sh`,
commit `b5560a74`) terminates one fork layer (the bash shim) by routing
`~/.hx/bin/hexa-cli` calls through a `socat UNIX-LISTEN ...,fork EXEC:`
pair. Caller-side CPU drops `0.31s -> 0.02s`, but wall time is
unchanged: every connection still spawns a fresh `hexa.real`, which
re-tokenises and re-parses the script each call. The persistent
listener exists, but the parse cost is exactly where it was.

Option A (this plan) shifts the listener inside `hexa.real` itself.
The process stays alive across requests, holds a parsed-AST LRU cache,
and dispatches each request through an in-process worker pool. The
expected wall-time gain on warm calls is roughly 80% (bottleneck is the
parse path, which collapses on cache hit), with no protocol change
visible to existing callers.

## 2. Architecture

- New subcommand: `hexa daemon-serve --sock <path>` in `hexa.real`.
- Long-running process: socket listener + worker pool dispatch.
- Parsed-module LRU cache keyed on canonical path:
  `canonical_path -> (ast_root*, mtime, size, last_used_at)`.
- Cache invalidation: `stat()` on each request; if `mtime` differs
  from the cached value (or `size` differs as a coarse second guard),
  invalidate and re-parse.
- Cache eviction: doubly-linked LRU + open-addressed hash. Defaults:
  `256` modules / `64 MB` total bytes; both configurable via env
  (`HEXA_DAEMON_CACHE_ENTRIES`, `HEXA_DAEMON_CACHE_BYTES`).

## 3. Protocol

The on-the-wire protocol from commit `b5560a74` is preserved
verbatim so the bash MVP and `~/.hx/bin/hexa-cli` keep working
unchanged:

```
request : <cwd>\x1f<arg1>\x1f<arg2>\x1f...\n
response: <stdout/stderr interleaved>
          __HEXA_DAEMON_RC__=<exit_code>\n
```

Extension (additive, optional): a leading `--mode=live|mock` arg lets
own 6 honesty thread the live-vs-mock distinction per request without
spawning a separate daemon per mode. Absent flag means "live" (the
existing default).

Special request: `__STATS__\n` returns a one-line JSON object with
`{cached, hits, misses, mem_bytes}` and exits the connection with
RC=0. Reserved for `hexa-cli --daemon-stats`.

## 4. Implementation phases

- **A1** — `hexa daemon-serve` subcommand stub.
  Wire argv parse in `self/main.hexa` dispatch. Open socket, accept
  loop, single-threaded dispatch using the existing `hexa.real`
  execution path. Behaviour: identical to the bash MVP except hosted
  inside `hexa.real`. Falsifier: `hexa-cli` calls produce identical
  stdout/rc compared to direct `hexa.real` invocation.

- **A2** — Parsed AST cache.
  Add `module_cache_t` struct in `self/runtime.c` (or split to
  `self/daemon.c`). On request, canonicalise `argv[file]`, `stat()`,
  consult cache, parse on miss. Cache hit skips the lex+parse phases.
  Falsifier: same output as A1; warm wall-time drops measurably.

- **A3** — Worker pool. (defer if cycle budget tight)
  N=4 default, configurable via `HEXA_DAEMON_WORKERS`. pthread +
  condvar + bounded request queue. A3 unlocks parallelism for batch
  callers (anima training, smoke gate spam). Single-worker A1+A2 is a
  legal stopping point.

- **A4** — Stats endpoint.
  `__STATS__\n` request handler. `hexa-cli --daemon-stats` printing.
  Used by L7-style sleep meta + manual debugging.

- **A5** — Graceful shutdown.
  `SIGTERM` handler stops accepting, drains in-flight workers, frees
  the cache, flushes a final stats line to the log file, and exits 0.
  Cycle order: A1 -> A2 -> A4 -> A5 -> A3 (A3 last because it touches
  threading, which is the highest blast-radius change).

## 5. Falsifier integration

- **HEXA_F-DAEMON-1** — Result identity: for a fixed corpus of `.hexa`
  scripts, daemon-path stdout + exit code equals direct-invocation
  output bit-for-bit. (parse + execute deterministic; cache must not
  alter observed behaviour.)
- **HEXA_F-DAEMON-2** — Cache hit ratio + memory footprint: after N
  warm calls of the same script, hit ratio approaches 1.0 and resident
  bytes stay under `HEXA_DAEMON_CACHE_BYTES`.
- **HEXA_F-DAEMON-3** — own 6 mode tagging: `--mode=mock` requests are
  tagged distinctly in the stats endpoint and (where relevant) in
  emitted stdout markers; `live` and `mock` traffic do not mutate one
  another's cache entries.

The 12 existing falsifier smokes (HEXA_F1..F6 + QM_F1..F6) must all
remain green when invoked through the daemon path.

## 6. Compatibility

- `~/.hx/bin/hexa-cli` (MVP client wrapper) unchanged: same socket
  request format, same `__HEXA_DAEMON_RC__=N` sentinel.
- `scripts/hexa_daemon.sh` (the bash MVP) is now redundant for the
  listener role — its `start/stop/status` UX can be retained, but the
  underlying process becomes `hexa.real daemon-serve` instead of
  `socat ... EXEC:hexa-daemon-handler`. Retirement is a follow-up.
- Direct `hexa run script.hexa` invocations are completely untouched.

## 7. Open questions

1. **own 6 mode tagging**: per-request flag (this plan) vs daemon-wide
   env (`HEXA_DAEMON_MODE`). Per-request is more precise but adds
   protocol surface; daemon-wide is simpler but forces operators to
   run two daemons.
2. **Cache key derivation**: canonical absolute path only, or
   `(canonical_path, content_hash)`? Path+mtime is cheap and matches
   the existing AOT cache style; content hash is more robust to
   filesystem timestamp anomalies but costs an extra read per call.
3. **Signal handling**: `SIGTERM` = drain, `SIGINT` = same? `SIGHUP` =
   reload config / flush cache? Defaults TBD.
4. **Error propagation**: parse error today exits with non-zero rc and
   prints to stderr. In daemon mode the handler must catch the error,
   format it through the response framing, and keep the daemon alive
   for the next request.
5. **Multi-tenant isolation**: a single daemon serves many `cwd`s.
   Cache keys are absolute paths so cross-cwd collisions are absent,
   but env-var leakage between requests must be audited (the handler
   should snapshot+restore env per request, not mutate process-global
   env).

## 8. Verification recipe

```
hexa daemon-serve --sock /tmp/hexa-daemon-A.sock &
~/.hx/bin/hexa-cli run /tmp/test.hexa     # cold
~/.hx/bin/hexa-cli run /tmp/test.hexa     # warm, expect cache hit
~/.hx/bin/hexa-cli --daemon-stats          # post-A4
```

Microbench targets:
- cold call wall: roughly `0.5s` (parse + execute, MVP-equivalent)
- warm call wall: roughly `0.05-0.10s` (cache hit, parse skipped)

Macrobench: `wraith vault setup-2fa --smoke` through the daemon path.
Regression: 12 falsifier smokes (HEXA_F1..F6 + QM_F1..F6) — daemon
path must produce identical observable output to the direct path.

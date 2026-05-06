---
doc: hexa-lang.doc.hexa_daemon
kind: design
audience: [human, agent]
date: 2026-05-06
status: mvp
parent: qmirror_speedup_brainstorm_2026_05_06 H.02
---

# hexa-daemon — persistent worker MVP

## Scope

H.02 from the qmirror speedup brainstorm: a long-running Unix-domain
socket process that fronts `hexa.real` and reduces per-invocation
shell-wrapper overhead. This MVP does **not** yet skip module parsing
or AST building — that requires changes inside `hexa.real` itself
(option A in the brainstorm, deferred to a separate cycle).

## Files

| path | role |
|---|---|
| `scripts/hexa_daemon.sh` | start / stop / status / restart driver |
| `scripts/hexa_daemon_handler.sh` | per-connection handler invoked by `socat` |
| `scripts/hexa_cli.sh` | client wrapper; dispatches to daemon or falls back to direct |

Installed at:

| symlink | target |
|---|---|
| `/Users/ghost/.hx/bin/hexa-daemon` | `scripts/hexa_daemon.sh` |
| `/Users/ghost/.hx/bin/hexa-daemon-handler` | `scripts/hexa_daemon_handler.sh` |
| `/Users/ghost/.hx/bin/hexa-cli` | `scripts/hexa_cli.sh` |

## Architecture

```
hexa-cli RUN ARGS...
   |
   |  encode(cwd \x1f arg1 \x1f arg2 ...)
   v
socat UNIX-CONNECT:/tmp/hexa-daemon.sock
   |
   v
socat UNIX-LISTEN (forks per connection)
   |
   v
hexa-daemon-handler (one per request)
   - read encoded line
   - cd cwd
   - exec hexa.real ARGS...
   - print output, then __HEXA_DAEMON_RC__=N
   v
hexa-cli decodes RC, replays output, exits with same code.
```

`socat` runs `UNIX-LISTEN:...,fork`, so each request still spawns a
fresh handler shell + a fresh `hexa.real`. The persistent piece is the
listener itself — and, crucially, this gives us a single attachment
point for future warm-cache / module-cache work without changing every
caller.

## Protocol

Request (one line, `\x1f` = US separator):

```
<cwd>\x1f<arg1>\x1f<arg2>\x1f...\n
```

Response:

```
<hexa.real stdout/stderr interleaved>
__HEXA_DAEMON_RC__=<exit code>
```

The trailing sentinel line is stripped by the client before passing
output through.

## Falsifiers (idempotency)

Verified equivalent behaviour vs direct shim (`/Users/ghost/.hx/bin/hexa`):

* trivial script — same wall time within noise (~0.45s)
* `setup_2fa --smoke` — same output (modulo intentional randomness),
  same `rc=0`
* args containing whitespace and quotes survive transport
* relative file paths resolve correctly via cwd field
* `exit(N)` propagates verbatim
* daemon down -> automatic fallback to direct invocation
* `HEXA_DAEMON=0` -> direct invocation, no socket touched

## Timing observations (2026-05-06, warm cache)

| invocation | wall (median of 3) |
|---|---|
| `hexa run trivial` (direct shim) | 0.45 s |
| `hexa-cli run trivial` (daemon path) | 0.46 s |
| `hexa run setup_2fa --smoke` (direct) | 0.46 s |
| `hexa-cli run setup_2fa --smoke` (daemon) | 0.45 s |

The daemon does **not** measurably accelerate the warm-cache path
because nearly all the cost is inside `hexa.real` startup itself, and
the MVP does not (yet) skip that startup. The `user`/`sys` CPU on the
caller side does drop to ~0 (vs ~0.13s/0.18s direct), since the bash
wrapper work moves into the daemon. The 60s+ figure cited in the
brainstorm appears to be a stale cold-cache or docker-fallback
scenario; current warm-cache cost is sub-second.

## Lifecycle

```bash
# start (idempotent)
hexa-daemon start

# health
hexa-daemon status

# stop
hexa-daemon stop

# restart
hexa-daemon restart
```

State files (override via env):

* `HEXA_DAEMON_SOCK` — `/tmp/hexa-daemon.sock`
* `HEXA_DAEMON_PIDFILE` — `/tmp/hexa-daemon.pid`
* `HEXA_DAEMON_LOG` — `/tmp/hexa-daemon.log`

## launchd (deferred)

Not auto-installed. To run on login, drop a user `LaunchAgent`:

```xml
<!-- ~/Library/LaunchAgents/com.hexa.daemon.plist -->
<plist version="1.0">
<dict>
  <key>Label</key><string>com.hexa.daemon</string>
  <key>ProgramArguments</key>
  <array>
    <string>/Users/ghost/.hx/bin/hexa-daemon</string>
    <string>start</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><false/>
  <key>StandardOutPath</key><string>/tmp/hexa-daemon.launchd.out</string>
  <key>StandardErrorPath</key><string>/tmp/hexa-daemon.launchd.err</string>
</dict>
</plist>
```

Then `launchctl load ~/Library/LaunchAgents/com.hexa.daemon.plist`.
Operator-driven; not part of this MVP.

## Next phase — option A (real leverage)

To get the brainstorm-projected 100× short-query latency, the daemon
must keep parsed ASTs / runtime state in memory across requests. That
requires `hexa.real` itself to grow a daemon mode — e.g.:

```
hexa daemon-serve --sock <path>
```

with the binary itself owning the socket and dispatching on a worker
pool that retains a module cache. The current bash MVP is the
attachment point so callers (`hexa-cli`) do not have to change again
when option A lands.

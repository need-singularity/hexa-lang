# gate/wrappers — mk1 compute-routing gate

Resurrected from airgenome v1. Intercepts `cargo` / `rustc` / `python3` /
`sh-run` / `hexa` invocations on the Mac host and (eventually) routes
heavy compute to `ubu`. Phase-1 runs local-only with a log entry at each
would-be routing decision.

## Install

```sh
export WS="$HOME/core"
hexa run $WS/hexa-lang/gate/wrappers/install.hexa
export PATH="$WS/hexa-lang/gate/wrappers/bin:$PATH"   # add to shell rc
```

The installer builds five binaries into `bin/` and drops a default
`config.jsonl` if one is not already present.

## Env overrides

| Var                     | Effect                                                 |
| ----------------------- | ------------------------------------------------------ |
| `CLAUDX_NO_WRAPPER=1`   | Bypass wrappers entirely (exec real binary directly).  |
| `AG_GATE_FORCE_LOCAL=1` | Force local execution; skip remote decision.           |
| `AG_GATE_FORCE_REMOTE=1`| Force remote dispatch; local fallback only on failure. |
| `AG_GATE_DEBUG=1`       | Verbose trace of the decide pipeline to stderr.        |
| `CLAUDX_NO_SANDBOX=1`   | Related but different layer — disables `raw#8` write-  |
|                         | block (sandbox-exec / LD_PRELOAD), not these wrappers. |

## Phase-1 deferred

- Remote routing (`rsync` + `ssh`) for `cargo` / `rustc` / `hexa` — currently
  falls back to local execution with a log entry; re-enable once the remote
  gate daemon lands.
- Full 11-layer filter pipeline — replaced here by a 6-rule minimum-viable
  `decide()` until the traffic shape is re-characterized.
- `vast.ai` fallback — dropped; Hetzner + ubu cover the phase-1 envelope.

## Layer map

Two enforcement layers, independent:

- **raw#8 (write-block)** — `sandbox-exec` (macOS) + `LD_PRELOAD` (Linux).
  Governed by `CLAUDX_NO_SANDBOX`. Not affected by anything in this dir.
- **compute-routing** — these wrappers. Governed by `CLAUDX_NO_WRAPPER` and
  the `AG_GATE_*` knobs above.

Disable them independently when debugging.

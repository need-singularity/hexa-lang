# gate/wrappers — mk1 compute-routing gate

Resurrected from airgenome v1. Intercepts `cargo` / `rustc` / `python3` /
`sh-run` / `hexa` invocations on the Mac host and routes heavy compute to
`ubu` (or the hetzner fallback) when the local box is loaded.

## Purpose

Give airgenome a single choke-point for "should this heavy command run on
the laptop or on a fatter box?" — answered per-call, with budget, filter,
and diagnostics state all cached in `/tmp/ag_gate_*` so repeat calls are
cheap.

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

## Status

### Phase 1 (done)

- Local exec path for all five wrappers.
- 6-rule filter (`ag_filter_decide`) covering sandbox-bypass, force-local,
  force-remote, mac-hot (load ≥ 5.00), and default.
- Budget counter (`ag_budget_enter` / `_leave` / `_count` / `_check`) —
  `/tmp/ag_gate_budget.count` tracks concurrent wrappers, capped by
  `max_budget` in `config.jsonl`.
- JSONL telemetry at `/tmp/ag_gate_filter.log` (`ag_filter_record`).
- ai_err-style diagnostics with `code / what / fix / ssot` quadruple.

### Phase 2 (done)

- Remote dispatch via `rsync` / `ssh` / `scp` for `cargo` / `rustc` /
  `python3` / `sh-run` / `hexa`.
- Core helpers in `src/_cache.hexa`:
    - `ag_remote_exec(host, cmd) -> [out, rc_str]` — streams combined
      stdout+stderr.
    - `ag_rsync(host, local_src) -> rc` — `rsync -az --delete` with
      optional `.gitignore` exclude-from; skips work when directory
      mtime-hash matches the stamp.
    - `ag_scp_files(host, files) -> rc` — per-file mtime dedup.
- State cache: `/tmp/ag_gate_rsync.stamp` (one line per workspace,
  `<src>|<basename>\t<hash>`) and `/tmp/ag_gate_scp_dedup/<basename>.mtime`.
- Fallback chain: `ubu` (ssh_alias) → `hetzner_host` → `""` (caller falls
  back to local).

## How remote routing works

**cargo.** Wrapper resolves the workspace root with
`git rev-parse --show-toplevel`, then `ag_rsync(host, root)` copies the
tree to `/tmp/ag_gate/<basename>/` on the remote. The actual `cargo …`
command is re-run via `ag_remote_exec` inside that directory, so
`Cargo.toml` / `target/` all live on the remote disk. The rsync skip-fast
path means repeat builds only pay for changed files.

**rustc.** Every `.rs` argv entry is collected and shipped via
`ag_scp_files(host, rs_files)` into `/tmp/ag_gate/files/`. Argv is then
rewritten so each `.rs` path points at the remote copy (basename under
`/tmp/ag_gate/files/`), and the resulting `rustc …` line is issued via
`ag_remote_exec`. Non-`.rs` args (flags, `-o`, target) pass through
unchanged.

**python3.** Same strategy as rustc — `.py` args are scp'd into
`/tmp/ag_gate/files/`, then the remote runs
`cd /tmp/ag_gate/files && python3 <rewritten argv>`. The `-c <code>`
fast-path is handled inline via `ag_remote_exec` without any file copy.

**sh-run.** Two sub-modes:
- `sh-run -c '<cmd>'` — direct `ag_remote_exec(host, cmd)`; no file
  transfer, no cwd change.
- `sh-run <script.sh> [args…]` — script is scp'd into
  `/tmp/ag_gate/files/`, then `bash /tmp/ag_gate/files/<basename> args…`
  via `ag_remote_exec`.

**hexa.** Subcommand-dependent:
- `hexa run <script>` / `hexa build <script>` — workspace-scoped. Wrapper
  resolves the nearest `hexa-lang` / `core` root and rsyncs it, then
  invokes `hexa <sub> <remote-path>` via `ag_remote_exec`.
- `hexa parse <file>` / `hexa test <file>` / `hexa --version` — single
  file or no file; scp the target (if any) and run via `ag_remote_exec`.
- `hexa --<flag>` short-circuits (no-arg flags like `--version`) go
  straight through `ag_remote_exec` with no transfer.

## Troubleshooting

When a remote call fails you will see one of the codes below on stderr,
prefixed `[wrapper_cache/<CODE>]` or `[<wrapper>/<CODE>]`:

| Code                    | Meaning                                                                 |
| ----------------------- | ----------------------------------------------------------------------- |
| `E_WS_UNSET`            | `$WS` is not exported — config and rsync roots can't resolve.            |
| `E_REMOTE_UNREACHABLE`  | `ag_resolve_host()` returned `""` — neither ubu, tailscale, nor hetzner responded on `remote_port`. |
| `E_RSYNC_FAIL`          | `rsync` exited non-zero (or src missing / host unresolvable); stamp not updated. |
| `E_SCP_FAIL`            | `scp` exited non-zero for at least one file, or a local file was missing. |
| `E_REMOTE_EXEC_FAIL`    | Remote command ran but exited non-zero — stdout/stderr still echoed locally. |
| `E_SSH_FAIL`            | `ssh` itself failed (auth, timeout, closed connection) — rc = 255.       |

Typical fixes:
- `export WS=$HOME/core` in your shell rc (cures `E_WS_UNSET`).
- `ssh ubu true` to confirm BatchMode auth (cures `E_SSH_FAIL` /
  `E_REMOTE_UNREACHABLE`).
- Check `wrappers/config.jsonl` for `ssh_alias` / `hetzner_host` /
  `remote_port` rows.
- Delete `/tmp/ag_gate_rsync.stamp` or `/tmp/ag_gate_scp_dedup/` to force
  a full resync if cache state is suspect.

## Layer map

Two enforcement layers, independent:

- **raw#8 (write-block)** — `sandbox-exec` (macOS) + `LD_PRELOAD` (Linux).
  Governed by `CLAUDX_NO_SANDBOX`. Not affected by anything in this dir.
- **compute-routing** — these wrappers. Governed by `CLAUDX_NO_WRAPPER` and
  the `AG_GATE_*` knobs above.

Disable them independently when debugging.

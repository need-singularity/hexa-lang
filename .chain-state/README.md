# .chain-state/ — forward-chain OS-level marker store

This directory holds the per-chain step markers that encode progress
through a `/.chain` forward-chain. Markers are written by
`tool/chain_runner.hexa` and are locked at the kernel level so users
cannot fake them by `touch`.

## Layout

```
.chain-state/
  <chain_id>/
    step_<step_id>_ok       # OS-locked on success
    ...
```

Example (`chain 1 "release"`):

```
.chain-state/1/step_build_ok
.chain-state/1/step_verify_ok
.chain-state/1/step_fixpoint_ok
.chain-state/1/step_archive_ok
```

## Marker file format

Plain text, four lines:

```
timestamp=2026-04-20T10:30:45Z
exit_code=0
cmd=./build/hexa_stage0 tool/raw_all.hexa
ttl=3600
```

## OS-level locking

| Platform | Mechanism     | Source of guarantee        |
|----------|---------------|----------------------------|
| macOS    | `chflags uchg`| Darwin VFS rejects writes with `EPERM` even for the owner until `nouchg` is set |
| Linux    | `chattr +i`   | ext/xfs VFS immutable bit; requires `CAP_LINUX_IMMUTABLE` (sudo) |
| Linux (sandbox, no sudo) | `chmod 0444` | soft fallback; `chain_runner` records `lock=soft` in `.raw-audit` so downstream tooling can detect unprivileged environments |

`chain_runner status <id>` explicitly flags any marker whose lock state
no longer matches the expected kernel-level lock (`[UNLOCKED — tamper
risk]`) so drift is surfaced, not silently tolerated.

## Reset procedure

Markers are append-only by design; to restart a chain you must invoke
the audited reset ceremony:

```sh
./hx chain reset <chain_id> --reason "<text>"
```

`--reason` is mandatory — the string is recorded in `.raw-audit` on a
`chain-reset` line together with the actor, the chain id, and the
number of markers cleared. The runner will:

1. `chflags nouchg` (or `chattr -i`) on each marker
2. `rm -f` the marker
3. append the audit line

After the reset, `./hx chain run <id>` will re-cascade from the first
step as if it had never run.

## Why OS-level rather than soft-check?

A soft filesystem flag (e.g. a JSON state file the runner consults)
would make it trivial for a downstream tool — or a distracted human —
to fabricate progress by dropping a file. Putting the "did step X
succeed?" signal behind a kernel-enforced immutable flag means the
answer is only "yes" if `chain_runner` itself wrote it and the OS
accepted the lock. This aligns with the raw#0 philosophy: the kernel,
not a script, is the source of truth.

## Relationship to other SSOT

- `/.chain` — **chain definitions** (source of truth for step graph)
- `/.chain-state/` — **marker store** (runtime progress; not in VCS except for this README)
- `/.raw-audit` — **event log** (every `chain-step` / `chain-reset` / `chain-blocked` event appended)

`.chain-state/` marker files themselves are intentionally not tracked
by git: they are machine-written, per-checkout, and recreated on demand
by re-running `./hx chain run <id> <first-step>`.

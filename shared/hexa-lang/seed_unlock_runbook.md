# Seed-Freeze Unlock Runbook — hexa-lang

Seed-freeze context (2026-04-17 ~ 2026-04-18): on-disk `build/hexa_stage0`
and `self/native/hexa_v2` are pre-fix for the IC-slot collapse regression.
The source fix landed in commit `fcf275f4` (self/codegen_c2.hexa — removed
`_ic_def_parts` module-level mutable + replaced with deterministic slot
decl emission driven by `_ic_counter`). A direct `FORCE=1 rebuild_stage0`
from the current tree would re-seed the regression because the on-disk
compiler itself produces corrupt C.

Three resumable paths below. Path (a) is recommended (fastest, already
has artifact on disk as of this runbook).

## Detection test (halts each binary at 30s)

Run against any candidate binary:

```
timeout 30 <BIN> tests/t33_repro/minimal.hexa /tmp/t33_probe.c
grep -c "static HexaIC" /tmp/t33_probe.c
#   38 → POST-FIX, safe to promote
#    0 → PRE-FIX, do not use
```

## Path (a) — Promote an existing post-fix worktree binary [PREFERRED]

Audit on 2026-04-18 21:30 UTC found 5 distinct post-fix hexa_v2 binaries
already sitting in `.claude/worktrees/`. Newest + largest = the safest
candidate. This skips Linux entirely.

Checklist (resumable):

- [ ] Re-run detection test on the chosen candidate to reconfirm post-fix
      just before promotion (hashes drift as worktrees churn):
      ```
      for b in .claude/worktrees/*/self/native/hexa_v2; do
          h=$(shasum -a 256 "$b" | awk '{print $1}' | cut -c1-12)
          echo "$h  $b"
      done | sort -u -k1,1
      ```
      Then pick one of: `b05cd2884ee1` (1061016 B, 2026-04-16 21:25) or
      `977dd884438b` (1122352 B, 2026-04-17 11:21) or `b2463ed648e2`.
      (Hashes may have rotated by the time you run this.)
- [ ] Back up current pre-fix binaries:
      ```
      cp self/native/hexa_v2 self/native/hexa_v2.prefix-$(date +%Y%m%d)
      cp build/hexa_stage0.real build/hexa_stage0.real.prefix-$(date +%Y%m%d)
      ```
- [ ] Promote to seed slot:
      ```
      cp .claude/worktrees/<AGENT>/self/native/hexa_v2 self/native/hexa_v2
      ```
- [ ] Regenerate `self/native/hexa_cc.c` using the promoted post-fix
      compiler (DO NOT run `rebuild_stage0.hexa` yet — first prove the
      post-fix compiler compiles the full codebase cleanly):
      ```
      self/native/hexa_v2 self/codegen_c2.hexa /tmp/codegen_c2_postfix.c
      diff -u self/native/hexa_cc.c /tmp/codegen_c2_postfix.c | head -60
      ```
- [ ] Only after the diff looks sane, run the wrapped rebuild:
      ```
      FORCE=1 hexa tool/rebuild_stage0.hexa
      ```
- [ ] Verify fixpoint: `self/native/hexa_v2` new size/hash changes; re-run
      detection test — must still report 38 on `minimal.hexa`.

## Path (b) — Cross-compile via Linux x86_64 post-fix host

Use only if path (a) fails verification. Tooling already present on Mac:

```
/opt/homebrew/bin/x86_64-linux-musl-gcc   # GCC 14.2.0 — confirmed 2026-04-18
```

Pipeline (`tool/build_hexa_v2_linux.hexa`):
input = `self/native/hexa_cc.c` + `self/runtime.c`;
tools = `x86_64-linux-musl-gcc` (override via `LINUX_CC`);
output = `build/hexa_v2_linux` (static ELF, `-O2 -static -lm`).

CRITICAL: this only helps if `self/native/hexa_cc.c` is POST-FIX. If
it is pre-fix (as of 2026-04-18 it is, `_ic_def_parts` still present
at lines 6777/7881/8039/9424/11787/11946/12097), the Linux binary
inherits the regression. Linux host cannot bootstrap out of this —
we need Path (a) first to regenerate `hexa_cc.c`, OR a Linux host
already holding a post-fix `hexa_cc.c`.

Minimal sequence when a post-fix Linux hexa_v2 arrives from a remote
host (anima pod / H100):

- [ ] `scp user@host:/opt/hexa/bin/hexa_v2 /tmp/hexa_v2_linux`
- [ ] `scp user@host:/path/to/postfix/hexa_cc.c /tmp/hexa_cc_postfix.c`
- [ ] `cp /tmp/hexa_cc_postfix.c self/native/hexa_cc.c` (POST-FIX seed C)
- [ ] `clang -O2 -I self self/native/hexa_cc.c -o self/native/hexa_v2
      -lm` (Mac-side build of the final arm64 seed)
- [ ] Run detection test on new `self/native/hexa_v2` — must report 38.
- [ ] `FORCE=1 hexa tool/rebuild_stage0.hexa` to close fixpoint.

## Path (c) — Mac interpreter transpile

Use only if paths (a)+(b) both fail. Blocked by
`feedback_interp_push_quadratic.md` — `flatten_imports.hexa` on
`hexa_full.hexa` OOMs at ~4.5GB RSS because `arr=arr.push(x)` in
interpreter is O(N^2) memory for N~4000. Workaround: stage a smaller
flattened source containing only codegen_c2 + lexer + parser +
type_check + hexa_build (drop ml/GPU/anima chains), then interpret
that subset to regenerate `hexa_cc.c`. Not scripted — manual.

## Constraints during unlock

- Do NOT run `FORCE=1 rebuild_stage0.hexa` until a post-fix `hexa_v2`
  is verified in `self/native/` via the detection test.
- Do NOT modify `self/codegen_c2.hexa` — source is already correct.
- Keep `.prefix-YYYYMMDD` backups until at least one clean fixpoint
  round completes.
- Commit only the post-fix binaries + regenerated `hexa_cc.c` atomically
  in a single commit with message referencing `fcf275f4` and this runbook.

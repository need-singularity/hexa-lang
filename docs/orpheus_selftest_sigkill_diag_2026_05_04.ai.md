# orpheus puzzle selftest SIGKILL diagnosis (2026-05-04)

Author: agent-a90de4cfca0dd2bb9 (worktree-isolated)
Branch: `diag/orpheus-selftest-sigkill`
Symptom: `HEXA_LOCAL=1 orpheus puzzle selftest` → SIGKILL (exit 9) on the
hexa_interp child running `/tmp/.hexa-runtime/hexa_expanded.*.tmp.hexa`.

## TL;DR

**Root cause: hypothesis #3 confirmed — genuine RSS growth past 2 GB inside
the interp's runtime.** The growth is dominated by the
`out = out + [x]` O(N²) array-rebuild anti-pattern in
`/Users/ghost/core/orpheus/modules/puzzle/secp256k1.hexa`, hit on the order of
~10^5 times during a single `priv_to_p2pkh_address` scalar multiplication.

The fix lives **outside this repo** (in `orpheus`), so per the task constraint
"DO NOT touch orpheus", no fix shipped this round. Recommended next step:
run the FU-9 concat-to-push migration on `orpheus/modules/puzzle/secp256k1.hexa`
in an orpheus-side diagnosis branch.

The two visible exit modes (graceful `exit(77)` vs hard SIGKILL exit 9) are
just the soft mem-cap firing vs the OS OOM-killer firing once `HEXA_MEM_UNLIMITED=1`
removes the soft cap.

## Repro

```sh
# Hits the soft cap → graceful exit 77 (orpheus reports FAIL):
HEXA_LOCAL=1 /Users/ghost/core/orpheus/bin/orpheus puzzle selftest

# Disables soft cap → OS OOM-killer SIGKILL (the originally reported symptom):
HEXA_LOCAL=1 HEXA_MEM_UNLIMITED=1 /Users/ghost/core/orpheus/bin/orpheus puzzle selftest

# Even with 4 GB / 8 GB caps, kills:
HEXA_LOCAL=1 HEXA_MEM_CAP_MB=4096 /Users/ghost/core/orpheus/bin/orpheus puzzle selftest

# Isolated secp256k1-only repro (kills in ~16s with 8 GB cap):
cat > /tmp/secp_only_repro.hexa <<'EOF'
import "/Users/ghost/core/orpheus/modules/puzzle/secp256k1.hexa" as ec
let _KNOWN_PRIV_HEX = "00...01"
println(ec::priv_to_p2pkh_address(_KNOWN_PRIV_HEX))
EOF
HEXA_LOCAL=1 HEXA_MEM_CAP_MB=8192 hexa run /tmp/secp_only_repro.hexa
```

Repro is deterministic; confirmed 4× in this session.

## Captured artifacts

| artifact | path | size | meaning |
|---|---|---|---|
| expanded selftest | `/tmp/.hexa-runtime/hexa_expanded.273613349401000.tmp.hexa` | 43 KB / 1313 lines | Tiny — rules out flatten/expansion explosion |
| expanded secp-only | `/tmp/.hexa-runtime/hexa_expanded.274216982711000.tmp.hexa` | 16 KB / 565 lines | Even smaller; SIGKILLs same as full selftest |
| interp binary | `/Users/ghost/.hx/packages/hexa/build/hexa_interp.real` | mtime 2026-05-04 13:50, size 2,861,408 | Post-merge (backup `pre_alias_merge_20260504_134917` is dated 13:49) |
| pre-alias backup | `/Users/ghost/.hx/packages/hexa/build/hexa_interp.real.pre_alias_merge_20260504_134917` | 2,844,016 | Sanity check — the live interp DID receive the alias merge |
| import-alias regression | `test/regression/import_alias_repro/` | — | PASSES — alias machinery itself is sound |

## Hypothesis ranking with evidence

### #1 Flatten / expansion explosion — REJECTED

Expanded `.tmp.hexa` files for both the full selftest (1313 lines) and
secp-only repro (565 lines) are tiny. No alias duplication occurring.
The `import_alias_repro` regression passes cleanly.

### #2 Infinite loop in expansion — REJECTED

Time-to-OOM is 13–16 s for secp-only and 28–38 s for full selftest. Both
print `[puzzle/selftest] orpheus puzzle module — F-PUZ-1, F-PUZ-2, F-PUZ-3`
(or `starting scalar mult...` for the isolated repro) BEFORE dying — i.e. the
process reached `main()` and was inside user code. This is run-time growth, not
expansion-time hang.

### #3 Genuine OOM in interp run-time — CONFIRMED

`self/runtime.c:56` sets a 2048 MB soft cap; on hit, `_hx_mem_cap_fire` does
`exit(77)` with the `[hexa-runtime] memory cap exceeded: rss=2049MB > cap=2048MB`
message we see in mode A. With `HEXA_MEM_UNLIMITED=1` the cap-disable branch
at line 63 short-circuits, the process keeps growing past 2 GB and macOS
jetsam delivers SIGKILL — exactly the originally reported exit 9.

The growth source is the `out = out + [x]` array-concat anti-pattern in
`orpheus/modules/puzzle/secp256k1.hexa`:

- `_bi_mulmod` line 196: `while i < wide_n { w = w + [0]; i = i + 1 }` — each
  step rebuilds the whole array. wide_n = 20 → 20×21/2 = 210 internal alloc
  ops PER mulmod call.
- `_bi_invmod` line 250: `bits = bits + [limb % 2]` accumulates 256 bits via
  per-iteration rebuild → 256×257/2 ≈ 33k alloc ops per call.
- `_bi_mulmod` itself called ~384× per scalar mul (256 doublings + ~128 adds
  × inner mulmods), each invocation triggering 256 `_bi_addmod`s in the
  shift-fold loop (line 229) which themselves return fresh arrays.

The hexa-lang side has already documented and partially fixed this
class of bug: commit `d5bf5c03 fix(stdlib): O(n²) concat→push migration in 5 hot loops (FU-9 partial)`
explicitly states:
> Per FIX-6 finding: 'out = out + [x]' is O(n²) rebuild that hits the
> interp's 2 GB cap; '.push' is amortised O(1).

Orpheus's `secp256k1.hexa` was not part of FU-9's sweep.

### #4 Stale stage-N binary — REJECTED

`hexa_interp.real` mtime 2026-05-04 13:50; backup snapshot
`pre_alias_merge_20260504_134917` mtime 13:49. The live binary was rebuilt AFTER
the alias merge. Verified by running the import-alias regression — it passes.

### #5 /tmp/.hexa-runtime/ cleanup race — REJECTED

The expanded `.tmp.hexa` file from each repro persists after the SIGKILL and
is readable. No cross-invocation tmp-path collision pattern in the failure
output. Runtime tmp dir contains ~58 stale files all sized identically (~31 KB,
1-minute cron cadence) — a separate hygiene observation, not implicated here.

## Recommended next step (orpheus-side fix)

Apply FU-9 concat→push migration to `orpheus/modules/puzzle/secp256k1.hexa`.
At minimum:

```hexa
// before (line 194-196):
let mut w: [int] = []
let mut i = 0
while i < wide_n { w = w + [0]; i = i + 1 }

// after:
let mut w: [int] = []
let mut i = 0
while i < wide_n { w.push(0); i = i + 1 }
```

Same migration in `_bi_invmod` (line 244 `bits` accumulator), and audit any
other `xs = xs + [y]` site in `_pt_*` helpers and `range_alloc.hexa`.

After the orpheus-side fix lands, re-run:
```sh
HEXA_LOCAL=1 /Users/ghost/core/orpheus/bin/orpheus puzzle selftest
```
Expected: PASS without needing `HEXA_MEM_UNLIMITED=1` or a raised mem-cap.

## Hexa-lang side: nothing to fix this round

The interp's behavior is correct: it caps RSS at 2 GB, diagnostic message is
clear, OS-level OOM only hits when the user explicitly disables the cap.
Import-alias machinery is sound (regression passes, expanded files are small).

If a follow-up wants to make this category of bug less footgun-prone on the
language side, candidates are:

- Lint pass (`tool/hot_loop_concat_lint.hexa`?) flagging `xs = xs + [y]` inside
  any `while`/`for` body, with auto-suggest `.push(y)`.
- Interp-side: detect `arr = arr + [single_elem]` reassignment-in-loop and
  rewrite to `push` at AST flatten time. Risky for semantic edge cases (alias
  sharing) but worth scoping.

Neither is blocking; both belong on a separate roadmap item, not in this
SIGKILL fix path.

## Status

- Reproduced: Y (4× in session, deterministic)
- Root cause: confirmed (hypothesis #3 + specific code-level cause)
- Fix shipped: N (orpheus-side fix required, out of scope per task constraints)
- Branch: `diag/orpheus-selftest-sigkill` (this doc only, no code changes)
- Pushed: N (per constraints)

# ROI #153 — P7-7 Fixpoint Closure Design (v3==v4 IDENTICAL)

**Date:** 2026-04-19
**Status:** design-only (no code edits)
**Scope:** Harden the `v2 → v3 → v4` self-host closure
**Prior art:** `doc/plans/roi_143_p7_7_fixpoint.md` (ROI #143, hexa_2==hexa_3)

## 1. What "v3 == v4 fixpoint" means

Chain (matches `tool/fixpoint_compare.hexa` and `tool/verify_fixpoint.hexa`):

```
v2 = build/hexa_stage0.real         (already on disk; self-hosted, 2.7MB, 2026-04-19)
v3 = flatten(hexa_full.hexa) | self/native/hexa_v2 -> .c | clang   (current tool output)
v4 = flatten(hexa_full.hexa) | v3                   -> .c | clang  (new — must match v3)
```

Gate: `SHA256(v3) == SHA256(v4)` AND `size(v3) == size(v4)` AND v4 smoke (`println("ok")`) passes.

Why v3==v4 (not v2==v3): v2 was clang-linked from an older toolchain snapshot, so LC_UUID/strtab pool can legitimately differ. The convergence invariant only requires that **two identical rebuilds** with today's pipeline produce byte-identical output.

## 2. Survey of current state

| Artifact | Path | Role | Health |
|---|---|---|---|
| Seed wrapper | `build/hexa_stage0` (6117B shim) | invokes .real | OK |
| Self-host seed | `build/hexa_stage0.real` (2.74MB) | = v2 in ROI #153 | OK |
| Transpiler | `self/native/hexa_v2` (1.24MB) | .hexa -> .c | OK |
| Runtime | `self/runtime.c` (4696 LOC) | clang link unit | OK |
| Source | `self/hexa_full.hexa` (17.3K LOC, 140+ `use`s) | SSOT | OK |
| Flatten tool | `tool/flatten_imports.hexa` | build the concat unit | known slow (push O(n²)) |
| Fixpoint check | `tool/fixpoint_compare.hexa` | v3==v4 gate | landed, never green-logged |
| Cache | `/tmp/hexa-cache/flatten/*.hexa` | 5 entries present | reusable |
| Log | `shared/hexa-lang/fixpoint_log.json` | per-run record | **missing** |

ROI #153 is marked `done` in `doc/roi.json` at 2026-04-19T17:28, target `self/tools/fixpoint_compare.hexa` (actual path is `tool/fixpoint_compare.hexa`). The tool exists; the **closure** (green run logged as an invariant) does not.

## 3. Gap analysis — what blocks v3==v4

Three classes of non-determinism exist today:

1. **Mach-O LC_UUID** — clang embeds a fresh UUID per link. `fixpoint_compare.hexa` prints v3/v4 UUID but doesn't neutralise it. On Darwin this is the typical first delta.
2. **flatten ordering** — `tool/flatten_imports.hexa` may walk `use` graph via dict iteration. If iteration order differs between runs, the concat unit differs → `.c` differs → binary differs.
3. **hexa_v2 codegen ordering** — `self/native/hexa_v2` may emit `__hexa_ic_N` / string-pool / decl order from a hashmap (ROI #152 IC-slot class).

Current `fixpoint_compare.hexa` only **reports** these; it does not **enforce** determinism.

## 4. Implementation plan (design only — do not edit yet)

### Phase P0 — Baseline capture (no code change, 30 min)
1. Run `./hexa tool/fixpoint_compare.hexa KEEP=1` once from clean cache.
2. Record: v3_sha, v4_sha, diff offset, LC_UUID v3/v4, size delta.
3. Persist full run log to `shared/hexa-lang/fixpoint_baseline_20260419.txt`.
4. Decision point: if v3==v4 already → skip to P3 (closure wiring).

### Phase P1 — Neutralise Mach-O UUID (2-3h)
1. **Option A (link flag):** add `-Wl,-no_uuid` to the Darwin `ldflags` branch in `fixpoint_compare.hexa` L177. Reruns will emit zero-UUID Mach-O. Minimal risk.
2. **Option B (post-link strip):** after clang, run `strip -u -r -no_uuid $v4_bin` equivalent, or `codesign --remove-signature` + UUID-patch via `printf` at LC_UUID offset. Heavier.
3. **Recommend A** — same flag already supported by ld64, zero code surface in codegen.
4. Add parallel flag to the (future) v3 producer; today v3 is `build/hexa_stage0.real`, rebuilt via a separate pipeline — document the requirement in `project_fixpoint_v3_v4_invariants.md`.

### Phase P2 — Lock flatten + transpile determinism (3-4h)
1. Audit `tool/flatten_imports.hexa` for dict iteration; replace with sorted key walk.
2. Run `./hexa tool/fixpoint_compare.hexa FORCE_FLATTEN=1` twice back-to-back, cmp the two `flat.hexa` outputs: must be byte-identical.
3. Run `self/native/hexa_v2 flat.hexa out1.c` twice, cmp the two `.c`: must be byte-identical.
4. If (2) fails → flatten non-determinism. If (2) passes but (3) fails → transpile non-determinism (ROI #152 IC-slot family).
5. For each failure, add a deterministic-emit pass with sorted keys.

### Phase P3 — Closure wiring (1h)
1. Ensure `shared/hexa-lang/fixpoint_log.json` path exists (mkdir on first run).
2. On IDENTICAL result, `fixpoint_compare.hexa` appends a JSON line:
   `{"ts":…, "v2_sha":…, "v3_sha":…, "v4_sha":…, "size":…, "identical":1}`.
3. Update ROI #153 note field with the green SHA triple + log pointer.
4. Rename target path in `doc/roi.json` from `self/tools/fixpoint_compare.hexa` to the actual `tool/fixpoint_compare.hexa` (one-char spec-reality drift).

### Phase P4 — CI gate (optional, 1h)
1. Add `tool/fixpoint_compare.hexa` to `.claude/hooks/pre-commit`-eligible invariants (run on tag only, not per-commit — takes minutes).
2. On any main-branch merge touching `self/hexa_full.hexa`, `self/native/hexa_v2`, `self/runtime.c`, `tool/flatten_imports.hexa`: gate must be green within 24h or roll back.

## 5. v3==v4 IDENTICAL verification procedure

Single-command sequence (read-only, safe re-run):

```
$ rm -rf /tmp/hexa_v4_fixpoint
$ KEEP=1 ./hexa tool/fixpoint_compare.hexa
# observe:
# [fixpoint] v3 sha256 = <A>  size=<N>
# [fixpoint] v4 sha256 = <A>  size=<N>
# [fixpoint] RESULT: v3 == v4  IDENTICAL  -> exit 0
```

Post-green invariants (written to `shared/hexa-lang/fixpoint_log.json`):

1. Run the gate **three times** in the same session. All three must be IDENTICAL and must share the same v4_sha (same-session reproducibility).
2. Delete `/tmp/hexa-cache/flatten/*`, rerun. Same v4_sha (cache-independence).
3. Reboot (or simulate: new shell, cleared env), rerun. Same v4_sha (environment-independence).
4. On ubu host, rerun with `--target=linux-x86_64`-equivalent. A different v4_sha is acceptable (cross-target), but the two **ubu-local** runs must be byte-identical to each other.

Failure triage:

| Delta shape | Likely cause | Fix |
|---|---|---|
| Single 16-byte run in middle | LC_UUID | P1 (`-Wl,-no_uuid`) |
| Entire binary differs, .c differs | flatten or transpile order | P2 |
| .c identical, binary differs | clang non-determinism | SOURCE_DATE_EPOCH=0 env |
| First delta in __TEXT | codegen reg-alloc order | n6 escalation (HX10) |

## 6. Risk register

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|---|---|---|
| R1 | flatten push-O(n²) causes cache-miss run to OOM | MED | blocked run | cache reuse (already in tool L87); cap `HEXA_VAL_ARENA=0` |
| R2 | ROI #152 IC-slot regression reappears on full `hexa_full.hexa` transpile | LOW (marked fixed fcf275f4) | v3!=v4 by construction | keep `tool/fixpoint_check.hexa --ic-scan` in the harness |
| R3 | `-Wl,-no_uuid` breaks codesign on macOS 15+ | LOW | v4 smoke exit!=0 | fallback: post-link UUID-zero patch at LC_UUID payload offset |
| R4 | seed-freeze tripwire if P2 tempts a stage0 rebuild | HIGH if bypassed | lost seed | plan explicitly forbids touching `build/hexa_stage0*` — all determinism fixes live in `tool/` and `self/native/hexa_v2` rebuild path |
| R5 | `shared/` directory is symlink to nexus — write path may differ on hosts | MED | log not written | check `shared/hexa-lang/` exists; fallback to `build/fixpoint_log.json` |
| R6 | hexa_v2 is itself clang-linked; its own UUID drift bleeds into v3==v4? | LOW (v3 built from hexa_v2 OUTPUT, not hexa_v2 itself) | none if v3/v4 both use same hexa_v2 binary | confirm hexa_v2 path is pinned via absolute path (tool already does L48) |

## 7. Time budget (12h ROI estimate)

| Phase | Hours | Cum | Confidence |
|---|---|---|---|
| P0 baseline capture | 0.5 | 0.5 | HIGH |
| P1 UUID neutralise | 3 | 3.5 | HIGH |
| P2 flatten + transpile determinism | 4 | 7.5 | MED |
| P3 closure wiring + log + doc | 1 | 8.5 | HIGH |
| P4 CI gate | 1 | 9.5 | MED |
| Slack | 2.5 | 12 | — |

Overall confidence 12h: **~60%**. Higher than ROI #143's 45% because the tool already exists and ROI #143/#149/#152 derisked the IC-slot and deep-import classes upstream.

## 8. Exit criteria (ROI #153 truly closed)

1. `tool/fixpoint_compare.hexa` exits 0 three consecutive sessions.
2. `shared/hexa-lang/fixpoint_log.json` contains ≥3 `"identical":1` entries with the same `v4_sha`.
3. `doc/roi.json` #153 `note` field references the log line and green SHA.
4. `doc/plans/self-hosting-roadmap.md` marks P7-7 `[x]` closed.
5. No new stage0 rebuild was performed (seed-freeze honored).

## 9. References

- `tool/fixpoint_compare.hexa` — the actual gate (285 LOC, lands 2026-04-19)
- `tool/verify_fixpoint.hexa` — P7-9 3-stage SHA variant (hexa_2==hexa_3)
- `tool/fixpoint_check.hexa` — IC-slot scanner scaffold (ROI #152 guard)
- `tool/flatten_imports.hexa` — concat unit producer (determinism target)
- `doc/plans/roi_143_p7_7_fixpoint.md` — prior P7-7 plan (this plan's parent)
- `doc/plans/roi_149_deep_import_chain.md` — upstream use-chain fix
- `doc/plans/runtime_c_purge.md` — downstream consumer (blocked on #152 + #153)
- `.own` rule 1 — "stage0 wrapper 경유" (seed invariant)

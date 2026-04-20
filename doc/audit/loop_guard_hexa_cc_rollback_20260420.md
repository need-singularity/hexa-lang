# Audit — "loop-guard rewrites hexa_cc.c back to pre-fix" (2026-04-20)

Status: **NOT-REPRODUCIBLE**. Recommend memory cleanup.

## Claim under investigation

From `feedback_seed_freeze.md`:
> `self/native/hexa_cc.c` on-disk — loop-guard 가 계속 pre-fix 로 재작성 (원인 미추적)

Paired with `project_hexa_v2_circular_rebuild_20260417.md` (ROI #152): the on-disk hexa_cc.c was allegedly reverting a T33 `_ic_def_parts` freelist-aliasing fix each time something ran — blocking `FORCE=1 tool/rebuild_stage0.hexa` re-activation.

## Investigation — Phase 1

### 1. "loop-guard" does not exist as a C-rewriter

- `tool/loop_lint.hexa` is the only `*loop*.hexa` in tool/. It validates `.loop` SSOT (raw#16 recurring-task registry — interval/cmd/why keys). It neither reads nor writes `*.c` files. Confirmed by grep on the full file (217 LOC, pure string validation + exit).
- No `tool/loop_guard.hexa` exists. No `*guard*.hexa` exists in tool/.
- No shell script references `hexa_cc.c`. No pre-commit hook installed (`.git/hooks/` only has `.sample` files).
- No launchd agent touches hexa_cc.c: `com.hexa-lang.relock.plist` runs `relock_daemon.hexa` (raw#1 P0d relock watcher — no C I/O); `io.hexa.watchd` also has zero `hexa_cc` references.
- Only 5 tool/ `*.hexa` files reference `hexa_cc.c` by name: `rebuild_stage0.hexa`, `build_stage0.hexa`, `build_hexa_v2_linux.hexa`, `cross_compile_linux.hexa`, `check_ssot_sync.hexa`. None is a "lint --fix" that rewrites the file; they only **build from** or **hash** it.

### 2. Current hexa_cc.c state = post-fix

- `self/native/hexa_cc.c`: 13,389 lines, sha256 `12e1b43c…3a647fe`, Apr 20 05:20, uchg **not** set (`ls -lO` shows `-` after perms), working tree clean.
- `grep _ic_def_parts self/native/hexa_cc.c` → 1 hit only, inside a block comment explaining the FIX-B follow-up to ROI #152 (lines 6921–6926). Quote:
  > FIX-B (T33 aliasing follow-up, 2026-04-19): per-lambda param-sig slots. Replaces _lambda_fwd_parts module-array push (same T33 freelist aliasing that bit _ic_def_parts / _strlit_def_parts).
- `grep _ic_def_parts self/hexa_full.hexa` → **0 hits** (canonical source is clean).
- Remaining `_strlit_def_parts` occurrences (7 in hexa_cc.c, 5 in codegen_c2.hexa) are either (a) comments describing the rt#32-SL fix or (b) the preserved empty array init `= hexa_array_new()`. No unsafe `.push()` sites remain — counter-driven deterministic emission replaced them.

### 3. Git log shows forward-only progression

`git log --oneline -- self/native/hexa_cc.c` since the freeze (2026-04-18):
- `bf054c4b` fix(bootstrap): hexa_cc.c 21K LOC 고정 — post-fix hexa_v2 (977dd884) 트랜스파일 결과 승격
- `b5c21ad8` fix(codegen): __hexa_sl_* + __hexa_ic_* emission — Blocker #5 해제
- `c461f60a` fix(codegen): T33 _lambda_fwd_parts aliasing — follow-up to fcf275f4
- ... 15 additional fix/feat commits (eprint, println variadic, FIX-A batches, tensor builtins, libm mangling, etc.)

Every commit is additive / forward. No "revert to pre-fix" pattern. ROI #152 agent `aa5615066c` landed `fcf275f4` + `b5c21ad8` + `c461f60a` + `c91cfcf3` and the state has held since.

## Hypothesis

The `seed_freeze` memory note (`feedback_seed_freeze.md`) was written **during** the ROI #152 investigation window (~2026-04-17) when the bug was live. After the four fix commits landed on 2026-04-19, the rollback no longer occurs — because the "rollback" was never an external rewriter, it was `hexa_v2 → hexa_cc.c` regen from a contaminated codegen_c2.hexa SSOT. With codegen_c2 fixed, subsequent regens preserve the fix. The memory note was simply never updated to reflect the ROI #152 resolution.

## Decision

**NOT-REPRODUCIBLE / AUDIT-ONLY.** No fix applied to source (nothing to fix — no errant rewriter exists).

## Reproduction attempt evidence

- No tool in the repo writes to hexa_cc.c except bootstrap regen (rebuild_stage0, which the user forbade from running this turn).
- Editing hexa_cc.c would only be reverted by an explicit `./hexa tool/rebuild_stage0.hexa` invocation — and that is the very command the user is waiting to re-enable. There is no background rewriter.
- File flags clean, git clean, no launchd watcher touches it.

## Next-step recommendation

1. **Memory hygiene**: update `feedback_seed_freeze.md` to note ROI #152 RESOLVED and this rollback concern stale. Keep the seed-freeze block on `FORCE=1 rebuild_stage0` only until the other remaining blocker is cleared.
2. **Re-activation gating**: the real remaining blocker per handoff notes is the FIX-A activation set (abort 충돌 / empty-LHS ident / swiglu_vec / tensor_ones). Once those close, run a **single** controlled `FORCE=1 tool/rebuild_stage0.hexa` with hash capture before/after to confirm no regression. If hexa_cc.c survives with `_ic_def_parts.push()` still 0, close the freeze.
3. **Optional belt-and-suspenders**: add a one-line SSOT check to `tool/check_ssot_sync.hexa` asserting `grep -c "_ic_def_parts\.push\|_strlit_def_parts\.push\|_lambda_fwd_parts\.push" self/native/hexa_cc.c == 0` after regen. Would catch any future freelist-aliasing regression immediately.

## Files inspected

- /Users/ghost/Dev/hexa-lang/tool/loop_lint.hexa (217 LOC — pure `.loop` SSOT validator)
- /Users/ghost/Dev/hexa-lang/self/native/hexa_cc.c (13,389 LOC, sha256 `12e1b43c…`)
- /Users/ghost/Dev/hexa-lang/self/hexa_full.hexa (0 `_ic_def_parts` hits)
- /Users/ghost/Dev/hexa-lang/self/codegen_c2.hexa (5 `_strlit_def_parts` hits — all comments + preserved init)
- /Users/ghost/Dev/hexa-lang/tool/rebuild_stage0.hexa (build-from, not rewrite)
- /Users/ghost/Dev/hexa-lang/tool/relock_daemon.hexa (no `hexa_cc` hits)
- /Users/ghost/Dev/hexa-lang/tool/watchd.hexa (no `hexa_cc` hits)
- /Users/ghost/Dev/hexa-lang/launchd/com.hexa-lang.relock.plist (raw#1 relock watcher, unrelated)
- /Users/ghost/Dev/hexa-lang/.git/hooks/ (samples only, no active hooks)

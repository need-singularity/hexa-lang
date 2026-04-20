# ROI #152 Audit — hexa_v2 circular rebuild regression

Date: 2026-04-20
Auditor: sub-agent (worktree agent-aa561506)
Verdict: ALREADY RESOLVED — no new code change; stale memory only.

## Memory claim (stale)

`project_hexa_v2_circular_rebuild_20260417` states:

> "on-disk hexa_v2 emits 5.6M-line corrupt C (__hexa_ic_N decls dropped,
> fn signatures truncated). T33-class _ic_def_parts aliasing in
> codegen_c2.hexa:1701/295. DO NOT run FORCE=1 rebuild_stage0.hexa until fixed."

## Evidence collected

1. `self/codegen_c2.hexa:1701` is inside `AssignStmt` → nested-`Index =` codegen
   (outer_container/outer_idx/inner_get). NOT an IC-slot emit site.
2. `self/codegen_c2.hexa:295` is a comment line in the `_resolved_modules`
   forward-decl block. Also not an IC-slot site.
3. Grep for `_ic_def_parts` in `self/codegen_c2.hexa`: four hits, all COMMENTS
   (lines 4128/4161 + history references in 651/858/883/4127/4181/4215/5381)
   explaining the historical bug and pointing at the fcf275f4 counter-driven fix.
4. The actual IC-slot def emission is already counter-driven
   (`self/codegen_c2.hexa:875-879`, mirrored at 5376 for codegen_c2_full):
   ```
   let mut _ic_i = 0
   while _ic_i < _ic_counter {
       parts = parts.push("static HexaIC __hexa_ic_" + to_string(_ic_i) + " = {0};\n")
       _ic_i = _ic_i + 1
   }
   ```
5. `self/native/hexa_cc.c` (the active macOS `self/native/hexa_v2` source per
   `self/main.hexa:122/126`) carries the same counter-driven pattern
   (`_ic_counter` reset at 8595, regen loop at 8805/13180). `_ic_def_parts`
   symbol appears only in a historical-explanation comment at 6924.
6. `self/native/codegen_c2_v2.c` (2062 lines) still holds the legacy
   `_ic_def_parts` push pattern (61-74, 157-158, 884-886) — but that file is
   NOT the active hexa_v2 source; it is a vestigial single-module transpile
   artifact. No active build path references it (confirmed via
   `tool/build_hexa_v2_linux.hexa:54` = `hexa_cc.c`; `self/main.hexa:122/126`
   = `hexa_cc.c`).

## Parse-check (V1 USER_PATH smoke)

```
HEXA_NO_LAUNCHD=1 ./build/hexa_stage0 self/codegen_c2.hexa
→ exit 0, silent (stdlib module — expected)
```

## Regression test (harness already in tree)

Ran `self/test_ic_slot_regen.hexa` (the permanent ROI#152 guard) via
`build/hexa_stage0`:

- Quick synthetic (12 field accesses): decls=12 refs=12 missing=0 → PASS.quick
- Extended scale (compiles `self/codegen_c2.hexa` via
  `self/native/hexa_v2`): 10,017 lines output, 954 decls, 974 refs,
  highest-id probe PASS → PASS.big

The "5.6M-line corrupt" signature is NOT reproduced. On-disk hexa_v2 emits
10K-line healthy C for the exact module the memory cites.

## Git archaeology

Fix landed in commits prior to 2026-04-19:
- `fcf275f4` fix(codegen-c2): _ic_def_parts push 제거 — rt#32 freelist aliasing 우회
- `b5c21ad8` fix(codegen): __hexa_sl_* + __hexa_ic_* emission — Blocker #5 해제
- `c461f60a` fix(codegen): T33 _lambda_fwd_parts aliasing — follow-up to fcf275f4
- `c91cfcf3` feat(bootstrap): seed-freeze 부분 해제 — post-fix hexa_v2 승격

`doc/roi.json:2715-2733` already records ROI #152 as `status=done` with
`done_at=2026-04-19` and the full resolution note.

## Diagnosis

No live aliasing path in the active codegen sources (codegen_c2.hexa +
hexa_cc.c). The vestigial `codegen_c2_v2.c` carries the old pattern but is
unreferenced. The memory file predates the fix.

## Decision

AUDIT-ONLY. No codegen_c2.hexa edit. The target bug is a ghost.

## Recommended next steps

1. Mark `~/.claude-claude3/.../project_hexa_v2_circular_rebuild_20260417.md`
   with a RESOLVED stamp (or supersede). The stale memory keeps triggering
   unnecessary re-investigations and blocks seed-freeze lift ceremonies that
   are actually gated on a DIFFERENT stage0 bug (feedback_seed_freeze.md
   references a separate issue).
2. Note: `feedback_seed_freeze.md` currently claims HX8 is blocked by "hexa_v2
   circular rebuild regression" — that rationale is no longer valid. Whatever
   genuinely blocks `FORCE=1 tool/rebuild_stage0.hexa` today is a separate
   issue and should be re-captured with fresh evidence.
3. Consider cleanup of `self/native/codegen_c2_v2.c` (delete or mark
   `.hexanoport`) to prevent future auditors from chasing the vestigial
   pattern.
4. bab99c98 hexa_full exec_with_status SSOT fix activation is not gated by
   this ROI — re-evaluate its real blocker.

## Files touched

- (new) doc/audit/roi152_hexa_v2_rebuild_regression_20260420.md
- codegen_c2.hexa: untouched.

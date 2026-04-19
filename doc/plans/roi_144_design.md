# ROI #144 — lax-baseline autotrim BLOCKED (2026-04-19)

Status: BLOCKED. Task cannot proceed as written.

## Summary

ROI #144 asked to shrink `.hexa-lax-baseline` toward ≤10 files by
(a) running `tool/lax_baseline_autotrim.hexa` to drop strict-clean paths and
(b) adding per-file `#!hexa lax` shebangs to the rest.

Two structural problems surfaced at investigation time that make the task a
no-op as-specified.

## Blocker 1 — baseline file was repurposed ~6h before this session

Commit **8503b3ad** (2026-04-19 01:47, "fix(lax-baseline): replace file-list
seed with real naked-fn count") replaced the 2124-line path list in
`.hexa-lax-baseline` with a single integer ("31587" at the time; working copy
currently shows "24763"). The commit message is explicit: the file is now a
**warn-on-increase counter for naked-fn occurrences** used by the Layer 3
`@attr` parser gate migration (`tool/migrate_fn_attr.hexa`), and the 2124-
line format was called out as an "exploration artifact".

Downstream consequences:

- `tool/ai_native_lint.hexa`'s `load_lax_baseline()` /
  `path_in_baseline()` still treat the file as a newline-separated path list
  (see lines 311–332). With contents `"24763\n"`, `path_in_baseline(p, ...)`
  is effectively always false → no path is ever grandfathered via baseline.
  The only lax opt-out now in use is the `#!hexa lax` shebang (file-level).
- `tool/lax_baseline_autotrim.hexa` walks `load_baseline_lines(path)` and
  skips integer lines as "paths" — `test -f <repo>/24763` fails, autotrim
  would mark the one entry as GONE and (with `--apply`) write an empty file,
  which would silently clobber the naked-fn counter the migration tool
  depends on.

The autotrim tool and the ROI #144 framing both pre-date 8503b3ad. They are
now operating on a file that no longer has the semantics they assume.

## Blocker 2 — hexa_stage0 shim lock held by long-running blowup

Dry-run invocation `hexa tool/lax_baseline_autotrim.hexa` exited after 300s
with `hexa_stage0 shim: lock timeout after 300s (holder: 82392)`. PID 82392 is
a `blowup.hexa math 1 --seeds 0.6180339887498949` invocation that had been
running >12min of CPU at inspection; nine other hexa_stage0 queued processes
were waiting on the same shim lock. Even if the semantic collision above were
resolved, autotrim would need the shim to be free to spawn ~2000 per-file
`ai_native_lint.hexa` subprocesses.

Compounding: **seed freeze** (see `feedback_seed_freeze`,
`project_hexa_v2_circular_rebuild_20260417`) prohibits any
`FORCE=1 rebuild_stage0` workaround.

## What needs to happen before ROI #144 can run

Pick one:

1. **Split the two concerns into separate files.** Move naked-fn counter to
   e.g. `.hexa-naked-fn-count` (tool/migrate_fn_attr.hexa + CI gate
   updates) and restore `.hexa-lax-baseline` to a real path list regenerated
   from `git show HEAD~2:.hexa-lax-baseline` (2124 entries) or by rescan of
   files that currently trip `ai_native_lint --default-strict`. Then autotrim
   semantics are valid again and ROI #144 proceeds.
2. **Retire the path-list baseline mechanism entirely.** Delete the baseline
   loader from `tool/ai_native_lint.hexa`, delete `lax_baseline_autotrim.hexa`,
   and rely exclusively on `#!hexa lax` per-file shebangs. Then ROI #144 becomes
   "audit remaining `#!hexa lax` files and remove where source can be fixed",
   which is a different task shape than what was scoped.

Either path is a semantics decision, not a tactical shrink — sending it back
for a decision is the right move.

## No changes landed this session

- `.hexa-lax-baseline` — untouched.
- No per-file `#!hexa lax` shebangs added.
- `tool/lax_baseline_autotrim.hexa` left with its prior (non-session)
  working-copy modification; not staged, not committed.
- No stage0 rebuild (seed freeze respected).

## Key paths

- `/Users/ghost/Dev/hexa-lang/.hexa-lax-baseline`
- `/Users/ghost/Dev/hexa-lang/tool/lax_baseline_autotrim.hexa`
- `/Users/ghost/Dev/hexa-lang/tool/ai_native_lint.hexa` (lines 311–361)
- `/Users/ghost/Dev/hexa-lang/tool/migrate_fn_attr.hexa` (naked-fn counter consumer)
- Commit `8503b3ad` — repurposed baseline file
- Commit `b6c3304f` — `migrate_fn_attr` find+grep fallback

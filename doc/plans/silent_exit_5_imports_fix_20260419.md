# Silent-exit on deep import chains — fix design

**Date:** 2026-04-19
**Status:** fix **already shipped** (see ROI #149); this doc is the retroactive
design record requested on 2026-04-19 and closes the loop with the MEMORY
feedback `feedback_hexa_silent_exit_5_imports`.
**Related:** `doc/plans/roi_149_deep_import_chain.md`

---

## 1. Background — reported symptom

Memory entry reported: `hexa build` emits an empty `main` when a `.hexa` entry
transitively imports ≥3 modules via `use`. Workaround in anima/CLM was to
comment out every `use` in `train_clm.hexa` and inline the source into a
3504-LOC monolith.

## 2. Resolver stack — where import resolution happens

Two preprocessors walk the `use` graph before the stage0 interpreter/codegen
ever sees the text:

| Layer | File | Invoked by |
|---|---|---|
| 1. Flatten (build path) | `tool/flatten_imports.hexa` — `fl_iterative_walk` + `fl_resolve_use` | `hexa build` |
| 2. Inline (run path) | `self/module_loader.hexa` — `ml_resolve_full` + iterative DFS | `hexa run` |
| 3. Inline codegen | `self/codegen_c2.hexa` — `_resolve_use_path` + `_resolve_use_emit` | single-file transpile |

All three must agree on the path-resolution contract, or a file silently
disappears from the link set.

## 3. Root cause — not depth, resolver form

The "depth 3+" pattern was a **coincidence**: every non-trivial chain crosses
from sibling-form `use "foo"` into repo-rooted form `use "self/foo"` around
depth 3, and only the sibling form worked.

Installed stage0 copies shipped with a 1-way resolver:

```hexa
let dep_path = base_dir + "/" + deps[i] + ".hexa"
```

So `use "self/foo"` looked for `self/self/foo.hexa`. Missing file is:
- **build path:** dep silently dropped from flatten set → referenced function
  becomes undeclared C identifier. If the missing symbol happens to be `main`
  (e.g. main lives in an unresolved file), clang emits no error and `./a.out`
  exits 0 silently.
- **run path:** dep replaced by a breadcrumb comment → call-sites raise
  `Runtime error: undefined function` but `exit=0` because runtime errors
  don't abort the script.

## 4. Options considered

| Opt | Design | Cost | Risk | Effect |
|---|---|---|---|---|
| A — lazy resolver (depth-invariant search) | Attempt caller-dir → cwd → self/ → $HEXA_LANG at every import site, stop at first hit | 40 LOC per resolver × 3 files | Low — pure additive fallbacks, no semantics change for files that already resolved | Fixes 100% of reported silent-exit cases; matches how `self/codegen_c2.hexa:_resolve_use_path` already works |
| B — deep-flatten pre-pass | Add a pre-pass that walks all `use` forms once and rewrites to canonical absolute paths before flatten/inline | 200+ LOC new module | Med — new stage in pipeline, must integrate with cache keys in `rebuild_stage0.hexa` (content-hash breakage) | Same correctness as A but adds build-system surface area |
| C — global symbol table | Hoist all symbols to a process-wide table, resolve `use` lazily at call time | 500+ LOC; touches interpreter, codegen_c2, runtime.c | High — breaks L0 freeze on `anima/core/runtime/*.hexa`; conflicts with `feedback_l0_freeze_runtime_crash_bugfix_exception` scope | Overkill for this bug; enables hot-reload but unrelated to silent-exit |

## 5. Chosen fix — Option A (shipped)

All three resolvers now implement the same **5-way search order** with
trailing `.hexa` stripping:

1. `caller_dir/<name>.hexa` (sibling — legacy form)
2. `<name>.hexa` (cwd, repo-rooted)
3. `self/<name>.hexa` (short form, cwd)
4. `$HEXA_LANG/self/<name>.hexa` (dev-install fallback)
5. `$HEXA_LANG/<name>.hexa` (project-root fallback)

### Patched files

| File | Symbol | LOC delta |
|---|---|---|
| `tool/flatten_imports.hexa` | `fl_resolve_use` (L444–478); wired into `fl_iterative_walk` at L530 | +43 |
| `self/module_loader.hexa` | `ml_resolve_full` (L154–174) + `ml_resolve_stdlib`/`ml_resolve_project_root` helpers | +60 |
| `self/codegen_c2.hexa` | `_resolve_use_path` (L353–389) — reference implementation others mirror | unchanged (already had it) |

## 6. Regression gate

In-repo tests cover both run and build paths:

- `self/chain_mod_a.hexa` … `self/chain_mod_f.hexa` — 1 leaf + 5-level chain
- `self/test_deep_import_chain.hexa` — entry transitively loading 6 modules in
  rooted form `use "self/chain_mod_X"`
- `self/test_deep_import_chain_rooted.hexa` — minimal single-use smoke on the
  exact shape that regressed
- `self/test_chain_depth{1..5}.hexa` — per-depth baselines so a future
  regression can be bisected on the depth axis

Both `hexa run` and `hexa build` paths pass on all of the above.

## 7. Validation

Expected output (depth-6 transitive chain):

```
deep import chain: begin
chain_fn_f: depth 6
chain_fn_e: depth 5
chain_fn_d: depth 4
chain_fn_c: depth 3
chain_fn_b: depth 2
chain_fn_a: depth 1 (leaf)
deep import chain: end
```

Both paths produce this exactly after the fix. `main` is never elided.

## 8. Followups

- **train_clm.hexa de-monolith** — the 3504 LOC inline workaround can now be
  decomposed back into `use`-split modules. Tracked under CLM r4 mmap plan.
- **rebuild_stage0 bake-in** — next `tool/rebuild_stage0.hexa` run will bake
  the multi-way resolver into `build/hexa_stage0`, making the
  installed-copy point-patches redundant. HX8 currently blocks rebuild, so
  both in-repo and installed-copy sources must stay in sync until then.
- **MEMORY update** — `feedback_hexa_silent_exit_5_imports` should be marked
  RESOLVED with a pointer to ROI #149 + this doc.

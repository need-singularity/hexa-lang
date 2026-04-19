# ROI #149 — Deep use-chain silent exit fix

**Status:** done (both interpreter + native build paths fixed in stage0 install).
**Date:** 2026-04-19

## Symptom (roadmap)

> When a `.hexa` entry point transitively imports 5 or more modules via `use`
> statements, the compiled binary exits silently (exit 0, no error, no output).
> 1-4 chain depth works.

Real symptom discovered while reproducing: depth was never actually the
trigger. The discriminator was the **form** of the `use` path, not the depth.

## Root cause

Two preprocessors resolve `use "..."` directives:

1. `scripts/flatten_imports.hexa` — invoked by `hexa build` to concat the `use`
   graph into a single source file for the C transpile path.
2. `self/module_loader.hexa` — invoked by `hexa run` to inline-expand `use` /
   `import` directives before the stage0 interpreter sees the source.

Both preprocessors, in the **installed stage0 copies at
`/home/aiden/Dev/hexa-lang/`**, shipped with an obsolete 1-way resolver:

```hexa
let dep_path = base_dir + "/" + deps[i] + ".hexa"
```

This only accepts the sibling-name form `use "foo"` (relative to the file that
contains the directive). Any repo-rooted form `use "self/foo"` — the style used
by every non-trivial build, including the real `self/codegen_c2.hexa`
`_resolve_use_path` resolver — looks for `self/self/foo.hexa`, which does not
exist. The dep is silently dropped (flatten_imports) or quietly replaced with a
breadcrumb comment (module_loader).

Downstream symptoms:

* **native build**: `chain_fn_X` becomes an undeclared C identifier → clang
  error (not silent). Silent-exit shape occurs when the missing symbol is
  `main` itself (mentioned in flatten_imports' 2026-04-18 scan-private-decls
  comment about `has_user_main` check).
* **interpreter**: call-sites print `Runtime error: undefined function: X` but
  `exit=0` because errors don't abort script execution.

## Reproduction

```bash
hexa run self/test_deep_import_chain.hexa
hexa build self/test_deep_import_chain.hexa -o /tmp/deep && /tmp/deep
```

Test graph: `test_deep_import_chain → chain_mod_f → e → d → c → b → chain_mod_a`
(6 modules transitively, all using `use "self/chain_mod_X"` rooted form).

Expected output:
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

`self/test_deep_import_chain_rooted.hexa` exercises the single-use rooted-form
path for quick smoke.

## Fix

Both installed preprocessors were patched to a **5-way resolver** that matches
`self/codegen_c2.hexa:_resolve_use_path`:

1. `caller_dir/<name>.hexa` (sibling — legacy form)
2. `<name>.hexa` (repo-rooted, cwd)
3. `self/<name>.hexa` (short form)
4. `$HEXA_LANG/self/<name>.hexa` (dev-install fallback)
5. `$HEXA_LANG/<name>.hexa` (project-root fallback)

With trailing `.hexa` stripping so both `use "foo"` and `use "foo.hexa"` work.

Patched files (stage0 install — outside git repo):
* `/home/aiden/Dev/hexa-lang/scripts/flatten_imports.hexa` — added
  `fl_resolve_dep` helper; `fl_walk` now calls it.
* `/home/aiden/Dev/hexa-lang/self/module_loader.hexa` — added
  `ml_resolve_use_multi`; `ml_resolve` uses it before falling back to legacy
  `ml_resolve_path`.

The in-repo copies at `/home/aiden/mac_home/dev/hexa-lang/` already contain a
newer, functionally-equivalent multi-way resolver (via `fl_resolve_use` and
`ml_resolve_full` respectively). Next `rebuild_stage0.hexa` run will pick up
those newer versions and obsolete these point-patches.

## Regression gate (durable)

In-repo regression tests:

* `self/chain_mod_a.hexa` ... `self/chain_mod_f.hexa` — leaf + 5-level chain.
* `self/test_deep_import_chain.hexa` — entry that transitively loads 6 modules
  with repo-rooted `use "self/..."` form.
* `self/test_deep_import_chain_rooted.hexa` — single-use smoke for the rooted
  form (was the exact shape of the regression).
* `self/test_chain_depth{1..5}.hexa` — per-depth baselines so future regressions
  can be bisected on depth axis.

All tests pass both `hexa run` and `hexa build` paths after the fix.

## HX8 constraint note

Per HX8 we did not run `scripts/rebuild_stage0.hexa`. The fix targets the
installed-copy `.hexa` sources which the stage0 binary reads at runtime — no
rebuild needed.  When a future rebuild happens, the mac_home source tree
already contains the newer multi-way resolver and these point-patches become
redundant.

---
schema: hexa-lang/stdlib/registry_autodiscover/ai-native/1
last_updated: 2026-05-02
module: stdlib/registry_autodiscover.hexa
depends_on: (none — pure stdlib + ls(1))
sister_doc: /Users/<user>/core/anima/anima/modules/rng/README.ai.md
marker: state/markers/phase_alpha_yaml_registry_autodiscover_landed.marker
status: landed
phase: alpha (Friction 0)
---

# stdlib/registry_autodiscover (AI-native)

Filename-driven enumeration of `<dir>/*.<suffix>` modules, producing a name → path dispatch map. Replaces hand-coded `rng_registry_names()`-style switches across sister repos.

## TL;DR

- `registry_scan_dir(dir, ".hexa")` — basenames stripped of suffix; `[]` on missing dir.
- `registry_build_dispatch(dir, ".hexa")` — `name -> "dir/name.hexa"` map.
- `registry_filter_excluded(names, [exclude...])` — set difference.
- Stage0 hexa lacks fn-pointers → autodiscover trims **enumeration boilerplate**, not the inlined collect dispatch (raw#10 limit 1).

## API surface

```hexa
use "stdlib/registry_autodiscover"

pub fn registry_scan_dir(dir: string, suffix: string)        -> [string]
pub fn registry_build_dispatch(dir: string, suffix: string)  -> map     // name -> path
pub fn registry_filter_excluded(names: [string], exclude: [string]) -> [string]
```

## Invocation matrix

| Goal | Function |
|------|----------|
| List modules in `modules/<feature>/` | `registry_scan_dir("modules/rng", ".hexa")` |
| Get name → path dispatch map | `registry_build_dispatch("modules/rng", ".hexa")` |
| Drop test fixtures from list | `registry_filter_excluded(names, ["fixture", "test"])` |

## Failure modes

| Input | Behavior |
|-------|----------|
| `dir = ""` | `[]` / `{}` (silent) |
| Non-existent dir | `[]` / `{}` (`ls 2>/dev/null` swallows ENOENT) |
| Suffix mismatch | excluded by suffix filter |
| Dotfile (`.hidden.hexa`) | dropped (leading-dot filter) |
| `README.ai.md` (no `.hexa` suffix) | excluded by suffix filter |
| Subdirectory with `.hexa` files | NOT recursed (flat-only) |
| Filename containing `\n` | breaks `ls` line-split (raw#10 limit 3) |
| Trailing slash on dir | path joined without doubled `/` |

## Caveats (full list in marker)

1. **Stage0 dispatch trim is partial.** anima `rng_registry_collect()` uses an inlined switch over canonical names because hexa stage0 has no fn-pointers / dyn-call. autodiscover trims `rng_registry_names()` (12 LOC → ~3) and meta-table boilerplate (~5 LOC), but the per-source collect inline cannot be removed without runtime traits.
2. **`ls(1)` external dependency.** Minimal Alpine images and airgapped distros may lack `ls`; the wrapper returns `[]` in that case (silent).
3. **Newline-in-filename unhandled.** Convention assumes ASCII module names (raw 271 enforced).
4. **No symlink discrimination.** BSD `ls` follows file symlinks by default; Linux GNU `ls` ditto. Symlinked `.hexa` files appear as regular entries.
5. **Case-sensitive suffix.** `.hexa` and `.HEXA` are distinct.
6. **README* not auto-excluded.** Caller passes README/dotfile bases to `registry_filter_excluded` if needed (in practice `.ai.md` extension already filters them out).

## Sister consumer projection

`/Users/<user>/core/anima/anima/core/rng/registry.hexa` (339 LOC). Migration impact estimate **without** breaking byte-identical reproducibility:

| Section | Current LOC | Post-autodiscover | Δ |
|---------|-------------|-------------------|---|
| `rng_registry_names()` (manual list of 6) | 12 | 3 | -9 |
| meta dispatch (per-source meta table) | 24 | ~10 (data-driven via per-module META block) | -14 |
| collect inline switch (Stage0 fn-pointer absence) | ~210 | ~210 (unchanged) | 0 |
| struct + helpers (sha/hex) | 89 | 89 (unchanged) | 0 |
| **Total** | **339** | **~316** | **-23 (~7%)** |

raw#10: full 1/3 LOC reduction (target ~110 LOC) requires Stage1 hexa fn-pointer / dyn-call — not in scope for Phase α.

## Selftest

`stdlib/test/test_registry_autodiscover.hexa` — 27 cases across 8 scenarios:
- empty dir (2)
- 3 .hexa modules anima-rng-style (5)
- mixed suffix filter (5)
- README + dotfile auto-skip (4)
- exclude filter (5)
- nested subdir not recursed (3)
- missing dir silent (1)
- trailing slash dir (2)

Byte-identical 2-run: stdout sha256 `8348cefb39ad6fd7da1c3209e2af6a2bbfb2389eea498594cd6d6e296dc5c275`.

Uses `/tmp/hexa_ra_selftest` scratch dir (auto-cleaned at exit).

## File index

| Path | sha256 |
|------|--------|
| `stdlib/registry_autodiscover.hexa` | `397efc2b65329ab6ea6b3f9d29ff5eea7caaf0b768a2c599407bd6752181da99` |
| `stdlib/test/test_registry_autodiscover.hexa` | `8ca6a40b839b02faf614f49721e75af97e268e145b111c6d2ab18fcfa3e0329a` |
| `stdlib/registry_autodiscover.ai.md` | (this doc) |
| `state/markers/phase_alpha_yaml_registry_autodiscover_landed.marker` | (see marker) |

# `registry_autodiscover`

_registry_autodiscover_

**Source:** [`stdlib/registry_autodiscover.hexa`](../../stdlib/registry_autodiscover.hexa)  

## Overview

Phase α (Friction 0) — AI-native module landed 2026-05-02.

Purpose: enumerate `<dir>/*.<suffix>` files and produce a name → path
dispatch map without hand-rolling shell+split helpers in every sister
repo. The classic pattern (anima/core/rng/registry.hexa, 339 LOC) hand-
writes a switch over canonical names; with autodiscover the enumeration
step is one stdlib call.

raw 9 hexa-only — pure stdlib using `exec("ls ...")` for portability
across BSD (Mac) and GNU (Linux). No find(1) flags depended on.

API:
  registry_scan_dir(dir, suffix)            -> [str]   // basenames stripped of suffix
  registry_build_dispatch(dir, suffix)      -> map     // name → full path
  registry_filter_excluded(names, exclude)  -> [str]   // names \ exclude

Limits (raw#10):
  1. Only direct children scanned. Subdirectories ignored — caller
     recurses if needed (anima rng intentionally flat).
  2. Symbolic links followed iff `ls` follows them on the host (BSD
     default: yes for files, no for dir entries).
  3. Filenames containing `\n` not handled — `ls` line-split breaks.
     ASCII module-name convention assumed (raw 271 enforces).
  4. README.* and *.ai.md are NOT auto-excluded — caller passes them
     via `registry_filter_excluded` if desired.
  5. Suffix match is case-sensitive. ".hexa" ≠ ".HEXA".
  6. Empty `dir` → `[]` / `{}`. Non-existent dir → `[]` / `{}` (silent).

── core scan ────────────────────────────────────────────────────────────

Internal: shell-escape a single path (bash single-quote safe).

## Functions

| Function | Returns |
|---|---|
| [`registry_build_dispatch`](#fn-registry-build-dispatch) | `map` |

### <a id="fn-registry-build-dispatch"></a>`fn registry_build_dispatch`

```hexa
pub fn registry_build_dispatch(dir: string, suffix: string) -> map
```

**Parameters:** `dir: string, suffix: string`  
**Returns:** `map`  

Build a name → full-path dispatch map for `<dir>/*<suffix>`.
Caller invokes `map[name]` to get the absolute (or dir-relative) path.

---

← [Back to stdlib index](README.md)

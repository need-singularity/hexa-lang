# Interpreter binary stale — common root cause for hxa-005 + hxa-009

**Date:** 2026-04-23
**Affects:** hxa-20260423-005 (dict_keys let-bind / map literal under HEXA_CACHE=0), hxa-20260423-009 (`use` loses real_args / starts_with / ends_with)
**Status:** root cause identified, fix blocked on rebuild path

## What was investigated

While trying the roadmap-65 (M3) call-site migration on `self/linter.hexa`
(`args()[2..]` → `real_args()`), the migrated linter started emitting
`Runtime error: undefined function: real_args`. A clean `git status`
revert confirmed the linter is *already* broken before any edit:

```
$ ./hexa run self/linter.hexa README.md
Runtime error: undefined function: starts_with
Runtime error: undefined function: ends_with
…
```

## Where the stale layer lives

| binary               | size  | mtime           | has `real_args` |
|----------------------|-------|-----------------|-----------------|
| `./hexa` (Rust drv)  | 366KB | 2026-04-23 13:44 | (driver only)   |
| `self/native/hexa_v2`| 1.4MB | 2026-04-23 13:44 | **yes** (strings) |
| `build/stage1/main.c`| 165KB | 2026-04-23 13:44 | **no** (`grep -c` = 0) |
| `build/interpreter.c`| 662KB | 2026-04-15 13:51 | **no** (8-day old) |

The Rust `./hexa run` dispatches into the interpreter layer — and the
interpreter layer is one of `build/interpreter.c` or `build/stage1/main.c`.
Both lack `real_args`, even though `self/hexa_full.hexa:10266` has had
the dispatch since commit `9bdf2336` (2026-04-23 12:47).

## Why it explains both gaps

- **hxa-009** — `use` triggers the multi-file flatten path that goes
  through stage1 (rather than the AOT-cache shortcut), so `real_args`
  / `starts_with` / `ends_with` resolve to "undefined function" via the
  env_fns fallback at `self/hexa_full.hexa:9156`.

- **hxa-005** — `dict_keys` let-bind returning `len=0` under the
  default cache path, plus the `let d = {}` parse error under
  `HEXA_CACHE=0`, are both consistent with the interpreter binary
  pre-dating the current `hexa_full.hexa` map / builtin-dispatch
  surface. Fresh inline `dict_keys(d)` works because the AOT-cache
  hits codegen_c2 directly without the stale interpreter path.

## Why it isn't fixable in this slice

`tool/rebuild_stage0.hexa` is the canonical path to refresh the
stage1/interpreter artifacts. Two blockers observed today:

1. Lock directory `mkdir /tmp/hexa-cl/2026-04-23/...` returns
   `Operation not permitted` under the current sandbox profile.
2. "missing root inputs — falling back to legacy" with no further
   diagnostic — even with `FORCE=1`.

Both indicate the rebuild path needs daemon-level privileges or a
pre-staged input set this session does not provide.

## Hand-off

- hxa-005 and hxa-009 stay `in_progress` until the daemon's next
  rebuild round (the same daemon that landed `968900b8` regen) lifts
  the interpreter artifact to current `self/hexa_full.hexa`.
- Verification once rebuild lands:
  ```
  ./hexa run test/regress_dict_keys_let_bind.hexa     # hxa-005 closer
  ./hexa run /tmp/probe9.hexa                          # hxa-009 closer
  ./hexa run self/linter.hexa README.md                # 65 migration unblock
  ```
- After verification, the real_args migration of `self/linter.hexa`
  (and the other 40+ call sites) becomes a straight mechanical pass.

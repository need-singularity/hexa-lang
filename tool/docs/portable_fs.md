# `portable_fs`

_BSD/Linux 공통 file/time helpers_

**Source:** [`stdlib/portable_fs.hexa`](../../stdlib/portable_fs.hexa)  

## Overview

Triggered by ω-hexa-1 finding (state/omega_hexa_1_portability.json):
`stat -f %z` works only on Mac (BSD); on Linux it dumps filesystem
metadata. `date +%s%N` works only on Linux (GNU); on Mac it returns  // @date-n-guard: doc-prose
"1234567890N" literal. 864 + 743 sites use these patterns.

This module exposes drop-in replacements that work on both platforms.
Migration strategy: callers replace `stat -f %z` shellouts with
`pfs_file_size(path)` and `date +%s%N` with `pfs_now_ns()`.  // @date-n-guard: doc-prose

ω-stdlib-1 (2026-04-26): All exports prefixed `pfs_` to avoid silent
shadowing with hexa builtins of the same name (file_size, file_exists
are dispatched in hexa_full.hexa). The builtin `file_size` returns -1
on missing path and (in interp dispatch) reads whole file into memory
— broken for multi-GB shards. portable_fs versions return 0 on missing
and use `wc -c` (constant memory). Different semantics → explicit prefix
makes intent unambiguous at every call site regardless of import order.

Tradeoffs:
  pfs_file_size: uses POSIX `wc -c < FILE` — reads whole file metadata
    via stdin, O(1) on most filesystems but technically does a stat
    under the hood. ~50% slower than native stat but portable.
  pfs_now_ns: uses `date +%s%N` with fallback to `(date +%s) * 10^9`.  // @date-n-guard: doc-prose
    On Mac the second path wins, losing ns precision but keeping
    monotonic increase across calls.

Returns file size in bytes, or 0 if path missing/unreadable.
Portable across BSD (Mac) and GNU (Linux) without uname check.

_No public functions detected._

---

← [Back to stdlib index](README.md)

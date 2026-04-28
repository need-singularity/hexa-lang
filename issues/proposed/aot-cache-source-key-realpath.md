# `self/main.hexa` `source_key`: canonicalize via `realpath` / `readlink -f` before sha256

## Summary

The AOT cache key in `self/main.hexa` (`source_key` fn, lines ~1071-1088) must
fully canonicalize the source path *before* hashing. The current implementation
canonicalizes the directory via `pwd -P` and re-attaches `basename`, which
correctly resolves directory symlinks but **not file symlinks**. Two paths that
point to the same physical file via a file symlink therefore hash to different
cache slots, and the AOT layer rebuilds both — neither slot ever sees the
other's fingerprint.

**Anchor**: hive raw 145 (`aot-cache-canonical-path`, NEW 2026-04-28,
severity warn).
**Workaround pattern label**: `realpath` (raw 159 NARROW lint matrix row 3).
**Signal kind**: runtime-bug.
**Severity to request**: high — correctness; cache-invariant violation.

## Motivation

The AOT cache contract is "same source ⇒ same slot". A file symlink is the
same source: `realpath(link)` and `realpath(target)` are equal. The current
key derivation breaks that:

```hexa
// current (sketch): pwd -P + basename
let T = "$(cd "$(dirname "$F")" 2>/dev/null && pwd -P)/$(basename "$F")"
sha256("$T")[0..16]
```

`cd dirname && pwd -P` canonicalizes the directory part. `basename`, by
construction, *does not* follow symlinks on the file itself. So:

- `hexa-lang/self/stdlib/x.hexa` (real file) → key `K1`.
- `hive/tool/x.hexa` (file symlink → the path above) → key `K2 ≠ K1`.

Both keys map to the same compiled object, but the cache stores them in two
slots, doubles disk usage, and — worse — neither slot sees the other's
fingerprint update, so warm-hit latency degrades and we get spurious rebuilds.

## Repro

Concrete failing site:

```
/Users/ghost/core/hexa-lang/self/main.hexa:1071-1082   // source_key fn (fix here)
```

Reference idiom already present in the same file (proven `realpath` usage):

```
/Users/ghost/core/hexa-lang/self/main.hexa:782-784     // install_dir_from_argv0
```

Cache schema bump site (must move to invalidate all v5 slots):

```
/Users/ghost/core/hexa-lang/self/main.hexa:1120        // cache_schema_version
```

Reproducible scenario:

1. Place a hexa source file at `A`.
2. Symlink `B -> A`.
3. `hexa run A` populates slot `K1`.
4. `hexa run B` populates slot `K2` even though source content is bit-identical.
5. Touch `A`, re-run `hexa run B` — it rebuilds because `K2`'s fingerprint does
   not see the change made via `K1`'s view.

Sub-agent verdict + measurement:
`/private/tmp/claude-501/-Users-ghost-core-anima/e8d30a3e-9b9f-4aec-b681-348a3ba457ae/tasks/a38dcbed465b44a7f.output`.

## Proposed fix (signature)

4-LoC change at `source_key`:

```hexa
// Replace the pwd -P + basename pipeline with a real path canonicalization.
fn source_key(file: string) -> string {
    // Prefer realpath; fall back to readlink -f for portability; fall
    // back to the legacy pwd -P composite as a last resort so existing
    // call paths don't fail-hard on platforms missing both binaries.
    let cmd = "F='" + file + "'; "
            + "R=\"$(realpath \"$F\" 2>/dev/null "
            + "      || readlink -f \"$F\" 2>/dev/null)\"; "
            + "if [ -n \"$R\" ]; then T=\"$R\"; "
            + "else T=\"$(cd \"$(dirname \"$F\")\" 2>/dev/null && pwd -P)/$(basename \"$F\")\"; fi; "
            + "printf '%s' \"$T\" | { sha256sum 2>/dev/null || shasum -a 256; } | cut -c1-16 | tr -d '\\n'"
    return exec(cmd)
}
```

And bump:

```hexa
let cache_schema_version = "v6"  // was v5; v6 forces wipe of mis-keyed slots
```

The version bump is non-negotiable — the migration path needs every legacy v5
slot evicted because they were keyed under the wrong canonicalization.

## Honest C3 (what this fix does NOT cover)

`realpath` covers ~99% of file-symlink cases. The residual ~1% — hard links,
APFS firmlinks, bind mounts, NFS mount-point variations — point to the same
inode under different `realpath`-canonical paths and so still hash to
different slots. Fixing those requires Option B: an inode-based key
(`stat(2).st_ino` + `st_dev`) or content-hash key, both of which have their
own complications (cross-device collisions for inode; hash cost for content).
Option B should land as a separate issue once the realpath fix has soak time.

## Why this is upstream-worthy

Every consumer of the AOT cache hits this — it is not project-specific. The
fix lives entirely inside `self/main.hexa` and one schema-version constant. No
new dependencies, no new build steps. The only behavior change visible to
users is "your cache directory grows half as fast and warm hits actually warm".

## Evidence anchors

- hive `.raw` L4785-4843 — raw 145 registration.
- `/Users/ghost/core/hexa-lang/self/main.hexa:1071-1088` — current
  `source_key` (note: nexus ω-cycle 2026-04-28 already merged the realpath
  patch locally; this issue formalizes it upstream so other consumers
  inherit the correctness fix and the cache-version bump).
- `/Users/ghost/core/hexa-lang/self/main.hexa:782-784` — proven realpath
  idiom used by `install_dir_from_argv0`.
- `/Users/ghost/core/hexa-lang/self/main.hexa:1120` — cache schema version
  constant.
- Sub-agent `a38dcbed` measurement
  (`/private/tmp/claude-501/...a38dcbed465b44a7f.output`).

# stdlib: `find_after(line, needle, start) -> int` for prefix-skip semantics in structured-format parsers

## Summary

Add `find_after(line: string, needle: string, start: int) -> int` to the
stdlib so structured-format parsers can express "find the next occurrence
of `needle` *after* the prefix" without having to hand-roll an indexed
loop. Today the only built-in is `find(needle)` which always returns the
first match index — which is the wrong semantics whenever the prefix
already contains the needle character.

**Anchor**: hive raw 147 (`prefix-aware-find-mandate`, NEW 2026-04-28,
severity warn).
**Workaround pattern label**: `find_after` (raw 159 NARROW lint matrix row 2).
**Signal kind**: stdlib-gap.
**Severity to request**: medium — small stdlib addition, broad
bug-class retirement; aligns with the `starts_with` proposal.

## Motivation

`string.find(needle)` returns the *first* match. For unstructured boolean
checks ("does the line contain `needle` at all?") that is exactly right.
For structured-format parsing it is exactly wrong, because the prefix
already contains the same character class as the field separator.

Concrete example (HXC Phase 8 P8 Bug 4 — `_process_tree_decl`):

```hexa
let line       = "# tree:0.A 2a62ba65:[child:foo]"
let space_idx  = line.find(' ')   // returns 1 (between '#' and 'tree:')
let after      = line.substring(space_idx + 1, len(line))   // wrong field
```

The intent was "skip the `# tree:0.A ` prefix, then split on the next
space"; the realisation was "split on the first space", which sits between
`#` and `tree:` because the comment-marker is followed by a space. The
parser silently malformed every entry it touched.

The fix at the call site is to either (a) precompute the prefix length and
search from there, or (b) wrap an indexed loop. Both are easy to write
*and* easy to forget. With every parser independently inventing it,
correctness drift is inevitable.

## Repro

Failing site:

```
hxc_migrate.hexa  ::  A15 _process_tree_decl   (Phase 8 P8 closure doc)
```

Documented in `/Users/ghost/core/anima/docs/hxc_phase8_closure_20260428.md`.

Audit surface (sub-agent `ae7c7e125` partial enumeration):

- 813 hexa files use `find()`.
- 1,462 total `find` call sites across the ecosystem.

Most are fine — boolean-presence or first-match-by-design. But every
structured-format parser is a systematic-risk site, and they cannot be
distinguished from safe sites by a syntactic scan. A stdlib `find_after`
makes the *intent* legible.

## Proposed signature

```hexa
// Returns the index of the first occurrence of `needle` in `line`
// at or after position `start`. Returns -1 if no such occurrence.
// Pure-hexa, ~10 LoC; wraps `index_of` / `substring` semantics.
fn find_after(line: string, needle: string, start: int) -> int {
    let n = len(needle)
    let limit = len(line) - n
    if start < 0 || start > limit { return -1 }
    let mut i = start
    while i <= limit {
        if line.substring(i, i + n) == needle {
            return i
        }
        i = i + 1
    }
    return -1
}
```

Call sites collapse cleanly:

```hexa
let prefix_end = len("# tree:0.A ")            // explicit, documented
let space_idx  = find_after(line, " ", prefix_end)   // intent visible
let field      = line.substring(space_idx + 1, len(line))
```

Or, paired with `starts_with` from the sibling proposal:

```hexa
if starts_with(line, "# tree:") {
    let after_prefix = len("# tree:")
    let sep = find_after(line, ":", after_prefix)
    // ...
}
```

## Why upstream

Same argument as `starts_with`: every repo invents this independently the
moment they touch a colon-separated, space-separated, or comment-prefixed
format. A 10-LoC stdlib helper retires the bug class globally and lets
the raw 159 lint's P3 (`find_after-emul`) detector fire on regressions
instead of expected code.

## Companion lint

After this lands, the AST lint described in raw 147 — "flag `find(...)`
immediately followed by `substring(...)` where the substring start
implies prefix-skip intent" — becomes mechanically actionable: every flag
has a one-line rewrite to `find_after`, instead of a per-site
"insert indexed loop" judgement call.

## Migration

- Drop-in replacement for the most common bug shape (`find(x)` followed by
  `substring(idx + len(x), ...)` with prefix > 0).
- The raw 159 lint's P3 row will start firing on remaining hand-rolls,
  driving them to zero.
- Raw 147's `prefix-aware-find-mandate` is satisfied at the language
  level — no need for project-specific lint rules.

## Evidence anchors

- hive `.raw` L4899-4944 — raw 147 registration.
- `/Users/ghost/core/anima/docs/hxc_phase8_closure_20260428.md` — Phase 8
  P8 root cause + concrete bug (Bug 4).
- `/Users/ghost/core/hive/tool/raw159_upstream_lint_minimal.hexa` — P3
  `_detect_find_after_emul` detector that this helper retires.
- Sub-agent `ae7c7e125` `find()` call-site enumeration (partial — 813
  files / 1,462 sites surveyed before timeout).

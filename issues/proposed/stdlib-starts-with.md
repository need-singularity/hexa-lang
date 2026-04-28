# stdlib: `starts_with(line, prefix) -> bool` to retire `substring(0, N) == literal` off-by-one bug class

## Summary

Add a tiny stdlib helper, `starts_with(line: string, prefix: string) -> bool`,
so the call sites that currently express prefix testing as
`substring(0, N) == "literal"` can collapse to one obviously-correct line.
The current idiom is the source of a recurring off-by-one bug class
(`N != len(literal)` → equality is silently always false).

**Anchor**: hive raw 146 (`prefix-length-mismatch-lint`, NEW 2026-04-28,
severity warn).
**Workaround pattern labels**: `starts_with` (raw 159 NARROW lint matrix
row 1) **and** `substring-mismatch` (matrix row 4) — one helper retires both.
**Signal kind**: stdlib-gap.
**Severity to request**: medium — ~10 LoC of pure-hexa stdlib for broad
bug-class retirement.

## Motivation

Without `starts_with`, every prefix test is hand-written:

```hexa
if len(line) >= 8 && line.substring(0, 8) == "# tree:" {
    // never reached: '# tree:' is 7 chars, substring(0,8) extracts 8,
    // equality with a 7-char literal is always false
}
```

The bug above is real — it surfaced as Bug 3 of the HXC Phase 8 P8 audit
(`hxc_migrate.hexa` `_process_tree_decl`). Every legitimate `# tree:` line
was silently skipped because the bound (`8`) drifted from the literal length
(`7`). The compiler can't catch it; the type system can't catch it; the only
way to catch it today is by spotting the literal-length / bound mismatch by
eye.

Three things make this a bug magnet:

1. **Two numbers must agree.** `8` in `len(line) >= 8` and `8` in
   `substring(0, 8)` and the implicit `7` from `len("# tree:")`. Anyone who
   updates the literal must remember to update both bounds.
2. **No runtime signal.** Wrong bound never throws; the equality just
   evaluates `false` forever, so the parser silently drops every line that
   should have matched.
3. **Static lint requires AST work.** A naive lint can scan for the pattern
   but only an AST-aware checker can recompute `len(literal_RHS)` and
   compare against the slice bound. We can write that lint (raw 159 P1
   detector — string-shape — already does), but the *real* fix is "don't
   write the pattern in the first place".

## Repro

Call site that demonstrated the failure mode:

```
hxc_migrate.hexa  ::  A15 _process_tree_decl   (Phase 8 P8 closure doc)
```

Pattern (paraphrased):

```hexa
if !(len(line) >= 8 && line.substring(0, 8) == "# tree:") {
    continue   // <-- wrongly taken for EVERY '# tree:' line
}
```

Documented in `/Users/ghost/core/anima/docs/hxc_phase8_closure_20260428.md`
(Phase 8 P8 root-cause section).

## Proposed signature

```hexa
// Pure-hexa, ~10 LoC. Lives in self/stdlib/string.hexa or wherever
// existing string helpers reside.
fn starts_with(line: string, prefix: string) -> bool {
    let n = len(prefix)
    if len(line) < n { return false }
    let mut i = 0
    while i < n {
        if line.substring(i, i + 1) != prefix.substring(i, i + 1) {
            return false
        }
        i = i + 1
    }
    return true
}
```

(Implementation can be optimised later — a single `index_of` scan or a
runtime intrinsic — but the pure-hexa version is small enough that a
simple loop is fine. The point is the **signature**, not the body.)

Call sites collapse to:

```hexa
if starts_with(line, "# tree:") {
    // ...
}
```

One literal, one length, one read.

## Why upstream

The same helper has been hand-rolled in at least three repos in the
ecosystem (hive, nexus, anima — independent reinventions). A stdlib helper
ends the reinvention loop and lets the raw 159 P1 / P4 lint detectors fire
on any *new* hand-roll as a flagged regression rather than as expected code.

## Migration

Once landed:

- Repo-wide `substring(0, N) == "literal"` sweeps become safe automatic
  rewrites — there's a real target to rewrite to.
- The raw 159 lint's P1 + P4 rows will start firing on remaining sites,
  driving cleanup to zero over a few cycles.
- Raw 146's `prefix-length-mismatch-lint` mandate is satisfied at the
  language level (no need for repo-by-repo lint rules).

## Evidence anchors

- hive `.raw` L4845-4897 — raw 146 registration.
- `/Users/ghost/core/anima/docs/hxc_phase8_closure_20260428.md` — Phase 8
  P8 root cause + concrete bug.
- `/Users/ghost/core/hive/tool/raw159_upstream_lint_minimal.hexa` — P1
  `_detect_starts_with_emul` detector + P2 `_detect_substring_mismatch`
  detector that this helper retires.
- Sub-agent `ad82a91829` task output (partial completion — Phase 8 doc
  carried the verdict forward per raw 91 honesty triad).

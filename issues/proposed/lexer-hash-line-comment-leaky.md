# lexer: `#` line comments are leaky — `=` / `:` / `/` in comment text trigger parse errors

## Summary

`#`-prefixed comments leak through the lexer when the comment contains
punctuation: `=`, `:`, `/`, etc. The result is a string of parse errors
on stderr, sometimes accompanied by a runtime `modulo by zero` when the
parser appears to retry interpreting comment text as an expression.

**Severity**: medium — execution typically continues (parser recovers)
but stderr is polluted and some idioms (Egyptian-fraction notes,
SSOT-pointer prose) cause genuine runtime errors.
**Signal kind**: lexer-bug.
**Detected on**: hexa.real 2026-05-07 across pre-existing
`verify_chip-*.hexa` legacy stubs in `/Users/ghost/core/hexa-chip/`.

## Repro

```hexa
fn main() {
    # This is a comment with = symbols 1/2+1/3=5/6
    println("hi")
    # SSOT 이관 예정: nexus/shared/n6/scripts/verify_xxx_n6.hexa
    println("done")
}
```

Observed:
```
Parse error at 2:30: unexpected token Eq ('=')
Parse error at 2:32: expected LBrace, got Ident ('symbols')
Parse error at 4:43: unexpected token Eq ('=')
Parse error at 7:1: expected RBrace, got Eof ('')
hi
done
```

Expected:
```
hi
done
```

(no parse errors; both `#` lines are line comments and should be
discarded by the lexer).

## Severity escalation: runtime errors

In `conscious_chip/verify_consciousness-chip.hexa` (pre-migration) the
comment `# Egyptian 의식 분배 = 1/2+1/3+1/6=1` triggered:

```
Parse error at 49:27: unexpected token Eq ('=')
Runtime error: unknown method .md() on void
modulo by zero
```

— exit code 1, the file's actual logic never runs. So `#` comments
aren't merely cosmetic noise; they can break execution.

## Hypothesis

`#` is the legacy comment marker (raw-style, shell-style) and `//` is
the official one. The lexer probably special-cases `#!hexa strict`
shebangs but otherwise tries to lex the rest of a `#`-prefixed line
character-by-character, sometimes recovering and sometimes not.

## Workaround in the wild

Migrate `#` comments to `//` per file:

```diff
-# verify_consciousness-chip -- HEXA-NOUS 의식 칩 n=6 검증
-# 핵심: IIT Phi=sigma-phi=10, 감각=sopfr=5, GNW=J2=24, 코어=sigma^2=144
+// verify_consciousness-chip -- HEXA-NOUS 의식 칩 n=6 검증
+// 핵심: IIT Phi=sigma-phi=10, 감각=sopfr=5, GNW=J2=24, 코어=sigma^2=144
```

Done across `conscious_chip/verify_consciousness-chip.hexa`. Other
`#`-comment files (`chip_3d/verify_chip-3d.hexa`, `hexa1/verify_chip-hexa1.hexa`,
`photonic/verify_chip-photonic.hexa`, `pim/verify_chip-pim.hexa`,
`sc/verify_chip-sc.hexa`, `wafer/verify_chip-wafer.hexa`) emit parse
warnings but do exit 0.

## Proposed fix

In the lexer, when scanning a line and encountering `#` (other than at
start-of-file as part of `#!hexa` shebang), discard everything to the
next `\n` and continue. That mirrors `//` handling.

If `#` is intentionally reserved for a future feature (attributes /
preprocessor directives), then either:

- Mark `#` line comments **explicitly deprecated** with a one-time
  migration warning ("`#` comment will become an attribute marker; use
  `//`"), or
- Document the restriction and update the existing legacy `#`-comment
  stubs in n6-arch / hexa-chip via a one-shot migration.

## Why upstream

The leakiness is per-file unpredictable — it depends on whether the
comment text happens to include `=` / `:` / `/`. That makes the bug
class invisible until a stub starts running and fails. A clean lexer
fix retires every legacy file's parse-error spew at once.

## Migration

Either:

- (a) Lexer fix → no source changes anywhere.
- (b) Mass-rewrite `#` → `//` across the n6-arch / hexa-chip ecosystem
  (~50 stub files). Mechanical sed-able.

Option (a) is preferred — option (b) is the fallback.

## Evidence anchors

- `/Users/ghost/core/hexa-chip/UPSTREAM_NOTES.md` §U-2 — in-repo notes.
- `/Users/ghost/core/hexa-chip/conscious_chip/verify_consciousness-chip.hexa`
  — migrated example (commit history shows the `#` → `//` migration).
- `/Users/ghost/core/hexa-chip/{chip_3d,hexa1,photonic,pim,sc,wafer}/verify_chip-*.hexa`
  — pre-existing `#`-comment legacy stubs that emit parse warnings.

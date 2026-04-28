# RFC 004 — `\n` in string literals: actual-newline lowering

- **Status**: proposed
- **Date**: 2026-04-28
- **Severity**: correctness (parser strips `\n` to literal `n`, forces `chr(10)` workaround)
- **Priority**: P1
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (extension to gap 3 — same lexer surface)

## Problem

hexa string literal `"line1\nline2"` does not lower to a single byte 0x0A between `1` and `l`. Current behavior under at least one mode (AOT confirmed; interpreter inconsistent): the backslash is stripped and `n` survives as literal character, OR the escape is dropped entirely depending on context.

Forces tools to construct newlines via `chr(10)` concatenation:
```hexa
// Current workaround
let NL = chr(10)
let multi = "line1" + NL + "line2"
```

Reproducer (anima-eeg multi-line render):
```hexa
let banner = "header\n----\nbody"
println(banner)   // observed: literal `\n` characters; expected: 3 lines
```

Note: this is closely related to RFC 003 (escape sequences) but reported separately because `\n` is the most-used escape and a higher-impact correctness issue. RFC 003 covers hex/unicode; RFC 004 covers the basic `\n` `\r` `\t` set.

## Proposed Change

Lexer (`self/lexer.hexa`) MUST lower the standard C escapes inside double-quoted string literals consistently across interpreter and AOT:

| Escape | Byte | Required |
|--------|------|----------|
| `\n` | 0x0A | YES |
| `\r` | 0x0D | YES |
| `\t` | 0x09 | YES |
| `\\` | 0x5C | YES |
| `\"` | 0x22 | YES |
| `\0` | 0x00 | YES |

This RFC may be merged with RFC 003 if a single lexer-escape sweep is performed; tracked separately because the falsifier surface is narrower (no hex/unicode dependency).

## Compatibility

Strictly a bug fix where `\n` is currently broken. If `\n` already works in some modes, this RFC ensures parity. Migration audit: any file using `chr(10)` concatenation can be simplified post-fix; existing code remains valid (additive).

## Implementation Scope

- **Audit + fix** `self/lexer.hexa` string-escape switch (~10-30 LoC)
- **Regression test**: `test/regression/lexer_basic_escape.hexa` (~20 LoC)
  - `"a\nb" == "a" + chr(10) + "b"` in both interpreter and AOT
  - `"\t" == chr(9)`, `"\r" == chr(13)`, `"\\" == chr(92)`, `"\"" == chr(34)`
  - `len("\n") == 1`

## Falsifier (raw#71)

INVALIDATED iff `"\n\r\t\\\"\0"` produces the exact 6-byte sequence `[0x0A, 0x0D, 0x09, 0x5C, 0x22, 0x00]` under BOTH interpreter and AOT.

**Anti-falsifier**: any divergence between interpreter and AOT for the basic escape set fails the proposal.

## Effort Estimate

- LoC: ~30 (lexer) + ~20 (test) = **~50 LoC**
- Hours: **2-3h** (likely a one-line fix + parity audit)

## Retire Conditions

- Falsifier passes → status `done`
- Merged into RFC 003 if escape-sweep cycle handles both → status `superseded by rfc_003`

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g3b-lexer-newline-escape`

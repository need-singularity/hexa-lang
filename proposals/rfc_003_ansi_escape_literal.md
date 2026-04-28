# RFC 003 — \xHH / \uHHHH / \U00HHHHHH escape sequences in string literals

- **Status**: proposed
- **Date**: 2026-04-28
- **Severity**: correctness (lexer silently produces wrong bytes)
- **Priority**: P1
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (gap 3)

## Problem

hexa string literal `"\x1b[2J\x1b[H"` produces literal characters `\`, `x`, `1`, `b`, ... NOT the ESC byte (0x1B) followed by `[2J[H`. The hexa lexer does not recognize `\x` hex escape. The leading backslash is stripped but the `x1b` survives as literal characters.

Same gap for `\033` (octal) and `\uXXXX` (unicode). User-visible failure mode: terminal shows literal `\x1b` characters; "screen doesn't clear".

Forces tools to construct ESC via `chr(27)` concatenation:
```hexa
// Current workaround
let ESC = chr(27)
let clear = ESC + "[H" + ESC + "[2J"
```
Verbose, easy to forget, and breaks lookup-table style ANSI palettes.

Reproducer (anima-eeg `electrode_adjustment_helper.hexa`):
- iter1 used `"\x1b[2J\x1b[H"` → terminal showed literal `\x1b` characters
- patched to `chr(27)` concatenation form

## Proposed Change

Lexer (`self/lexer.hexa`) recognizes the following standard escape forms inside double-quoted string literals:

| Escape | Meaning |
|--------|---------|
| `\xHH` | hex byte (HH = 2 hex digits, case-insensitive) |
| `\uHHHH` | unicode codepoint (4 hex digits, UTF-8 encoded) |
| `\U00HHHHHH` | unicode codepoint (8 hex digits, UTF-8 encoded) |
| `\0NN` / `\0NNN` | octal byte (legacy compatibility, optional) |

Existing escapes preserved: `\n` `\r` `\t` `\\` `\"` `\0`.

Malformed escapes (`\xZZ`, `\xG`) MUST raise a clear lex error rather than silently producing garbage.

## Compatibility

Potentially breaking for any existing source that contains a literal `\x` or `\u` sequence inside a string and relies on the current lexer leaving it as literal characters. Mitigation: a 1-day grep audit across hexa-lang/hive/anima/nexus repos to enumerate occurrences (expected: ~0; the current behavior is unusable).

## Implementation Scope

- **Extend** `self/lexer.hexa` string-literal escape switch (~20-50 LoC)
  - Add `\x`, `\u`, `\U` cases
  - Add malformed-escape error path
- **Regression test**: `test/regression/lexer_string_escape_x_u.hexa` (~30 LoC)
  - `len("\x1b") == 1`
  - `ord("\x1b"[0]) == 27`
  - `"\x1b[2J" == chr(27) + "[2J"`
  - UTF-8 round-trip for `é` ↔ `é`
  - Malformed `\xZZ` raises lex error

## Falsifier (raw#71)

INVALIDATED iff:
1. `len("\x1b") == 1` AND `ord("\x1b"[0]) == 27` in both interpreter and AOT
2. `"\x1b[2J"` byte-equals `chr(27) + "[2J"`
3. Lexer rejects malformed escapes (`\xZZ`, `\xG`) with a clear error

**Anti-falsifier**: any case where the lexer accepts `\xZZ` or produces a different byte sequence between interpreter and AOT fails the proposal.

## Effort Estimate

- LoC: ~50 (lexer) + ~30 (test) = **~80 LoC**
- Hours: **3-5h**

## Retire Conditions

- Falsifier passes → status `done`
- Source-level audit reveals the existing literal-`\x` pattern is in use somewhere production-critical → status `parked` pending migration plan

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g3-lexer-x-u-escape`

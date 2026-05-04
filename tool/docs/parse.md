# `parse`

_parse_

**Source:** [`stdlib/parse.hexa`](../../stdlib/parse.hexa)  

## Overview

Decimal-tolerant integer parse with deterministic fallback.
Truncates fractional part. Returns 0 on empty / non-digit / whitespace-only
inputs. No panic on malformed input.

Distinct from `to_int` (strict — panic on malformed): use `to_int_safe` at
data-boundary parse sites (ps output / jq fallback / shell glob / user
input) where input may be malformed but pipeline must continue.

Source: RFC `to_int_safe` (airgenome 2026-04-30 wave 4 cross-repo evidence —
5+ scattered local helpers in airgenome alone, ~17 across 5 sister repos).
Option A pure-hexa stdlib fn (raw 9 hexa-only compliant).

Behavior table:
  "0.0"    → 0
  "5.7"    → 5    (truncate fractional)
  "-2.9"   → -2   (truncate toward zero)
  " 5 "    → 5    (trim)
  "abc"    → 0
  "5abc"   → 0    (strict trailing — not partial parse)
  "0x10"   → 0    (no hex)
  "1e3"    → 0    (no scientific notation)

Internal helper: returns true if `s` (after trim/strip) parses as a strict
non-negative decimal integer (no fractional, no sign). Useful for callers
that want to distinguish "parsed 0" from "parse failed → 0".

## Functions

| Function | Returns |
|---|---|
| [`is_numeric_str`](#fn-is-numeric-str) | `_inferred_` |
| [`to_int_safe`](#fn-to-int-safe) | `_inferred_` |

### <a id="fn-is-numeric-str"></a>`fn is_numeric_str`

```hexa
pub fn is_numeric_str(s)
```

**Parameters:** `s`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-to-int-safe"></a>`fn to_int_safe`

```hexa
pub fn to_int_safe(s)
```

**Parameters:** `s`  
**Returns:** _inferred_  

Public API: decimal-tolerant integer parse with deterministic fallback.
See module header for behavior table.

---

← [Back to stdlib index](README.md)

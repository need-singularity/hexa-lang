# `bytes`

_byte/uint conversions (P1 — qmirror per-shot perf)_

**Source:** [`stdlib/bytes.hexa`](../../stdlib/bytes.hexa)  
**Module slug:** `stdlib-bytes`  
**Description:** byte-array <-> integer + hex helpers  

**Usage:**

```hexa
import stdlib/bytes; let u = bytes_to_uint64(qrng_bytes, 0)
```

## Overview

Source: anima/docs/hexa_lang_attr_review_for_qmirror_2026_05_03.md §5.5

PROBLEM (qmirror sampler.hexa per-shot path)
  The hot loop pulls 8 bytes from the entropy source and folds them
  into a uint64 to drive the [0, 1) float used for measurement-basis
  selection. Today the conversion is `exec("printf '%d' 0x" + pair)`
  per byte pair — fork-per-shot. At 50K shots × 8B/shot that is 400K
  process forks, totally dominating the per-shot budget.

FIX (additive, no runtime.c change)
  1. bytes_to_uint64(b, offset) -> int
       Big-endian fold of 8 bytes starting at b[offset]. Returns 0 if
       the slice would run off the end (no panic). Pure arithmetic, no
       exec — eliminates the per-shot fork cost.
  2. bytes_to_uint64_le(b, offset) -> int
       Little-endian variant for callers wrapping LE C structs.
  3. bytes_to_uint32(b, offset) -> int
       4-byte BE fold. Same family.
  4. int_from_hex(s) -> int
       "ff" / "0xFF" / "FF" → 255. Returns 0 on empty / bad input.
  5. hex_encode_bytes(b) -> string
       [0..255] array → "0a1bff…" lowercase. ASCII-safe.
  6. uint64_to_unit_float(u) -> float
       Convert a 64-bit uint into a [0, 1) double via division by 2^53
       (the largest power-of-two that fits losslessly into IEEE-754
       double). The top 11 bits of the uint are discarded — see caveat.

Breaking risk: 0. New module, all new symbols. The runtime hex_encode
builtin (which encodes a string to hex) is untouched; this module's
hex_encode_bytes operates on an int-array of 0..255 byte values.

Caveats (raw#91):
  · `bytes` in hexa convention is `[int]` with each element in 0..255.
    This module does NOT range-check input bytes — bytes outside
    0..255 produce silent overflow (e.g. b[i]=256 → result shifted
    wrong). qrng/anu byte arrays are guaranteed in-range; sister
    callers should pre-validate or use an upstream sanitizer.
  · uint64_to_unit_float discards 11 entropy bits to fit IEEE-754
    mantissa. For Bell-state / measurement-basis selection (4 outcomes)
    this is overkill precision; for cryptographic uses prefer raw
    bytes_to_uint64 + manual masking.
  · int_from_hex relies on the runtime hex_decode builtin per pair —
    which is itself implemented in C and runs in O(N). Acceptable for
    short cache-version strings; do NOT call in inner loops on KB-scale
    inputs (use hex_encode_bytes + cache).
  · No SIMD / runtime fast-path — these are pure arithmetic loops.
    Fine for 8-byte folds (per-shot path); KB-scale conversions
    should use a future C builtin if profiled hot.

raw#9 hexa-only-strict: pure hexa, zero exec(), zero new C builtin.

── bytes_to_uint64 — big-endian 8-byte fold ─────────────────────

Reads b[offset..offset+8] as big-endian (network order) and returns
the 64-bit unsigned interpretation as a hexa int. Out-of-range
(offset < 0 OR offset+8 > len(b)) returns 0 — caller must pre-check
length if 0 is a valid result for them.

Implementation: shift-by-multiply-256 to stay portable across hexa
integer types (the runtime int is signed 64; explicit shift would
require bit-ops on signed which behave platform-dependently).

## Functions

| Function | Returns |
|---|---|
| [`bytes_to_uint64`](#fn-bytes-to-uint64) | `int` |
| [`bytes_to_uint64_le`](#fn-bytes-to-uint64-le) | `int` |
| [`bytes_to_uint32`](#fn-bytes-to-uint32) | `int` |
| [`int_from_hex`](#fn-int-from-hex) | `int` |
| [`hex_encode_bytes`](#fn-hex-encode-bytes) | `string` |
| [`uint64_to_unit_float`](#fn-uint64-to-unit-float) | `float` |

### <a id="fn-bytes-to-uint64"></a>`fn bytes_to_uint64`

```hexa
pub fn bytes_to_uint64(b, offset: int) -> int
```

**Parameters:** `b, offset: int`  
**Returns:** `int`  

_No docstring._

### <a id="fn-bytes-to-uint64-le"></a>`fn bytes_to_uint64_le`

```hexa
pub fn bytes_to_uint64_le(b, offset: int) -> int
```

**Parameters:** `b, offset: int`  
**Returns:** `int`  

bytes_to_uint64_le — little-endian variant.

Same range checks; reads b[offset+7] as the most-significant byte.

### <a id="fn-bytes-to-uint32"></a>`fn bytes_to_uint32`

```hexa
pub fn bytes_to_uint32(b, offset: int) -> int
```

**Parameters:** `b, offset: int`  
**Returns:** `int`  

bytes_to_uint32 — big-endian 4-byte fold.

### <a id="fn-int-from-hex"></a>`fn int_from_hex`

```hexa
pub fn int_from_hex(s: string) -> int
```

**Parameters:** `s: string`  
**Returns:** `int`  

── int_from_hex — accept "ff", "0xff", "FF" ─────────────────────

Decimal-only input is NOT supported (would overlap the to_int builtin
semantics ambiguously). Empty / non-hex input returns 0 silently —
caller treats 0 as the "no parse" sentinel, validating in a caller
scope that knows what 0 means in their problem domain.

### <a id="fn-hex-encode-bytes"></a>`fn hex_encode_bytes`

```hexa
pub fn hex_encode_bytes(b) -> string
```

**Parameters:** `b`  
**Returns:** `string`  

── hex_encode_bytes — [0..255] array to lowercase hex string ────

NOTE: the runtime `hex_encode` builtin operates on a STRING (encodes
each char's byte value). This module's `hex_encode_bytes` operates on
an INT-ARRAY (each element treated as a byte). Distinct functions for
distinct inputs — sister callers using qrng [int]-byte arrays land
here; callers with text strings stay on the runtime builtin.

### <a id="fn-uint64-to-unit-float"></a>`fn uint64_to_unit_float`

```hexa
pub fn uint64_to_unit_float(u: int) -> float
```

**Parameters:** `u: int`  
**Returns:** `float`  

── uint64_to_unit_float — uint to [0, 1) ────────────────────────

Scales a 64-bit unsigned (passed as a hexa int) into the half-open
[0, 1) interval by dividing by 2^53. The mantissa-aligned divisor
guarantees the result has full IEEE-754 double precision and never
reaches exactly 1.0.

Only the low 53 bits of the input are used (top 11 bits discarded
via integer division). Adequate for ≤2^53 distinct outcomes — far
beyond the qmirror Bell-test 4-outcome alphabet.

---

← [Back to stdlib index](README.md)

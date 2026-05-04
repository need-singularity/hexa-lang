# `hash/xxhash`

_hash/xxhash_

**Source:** [`stdlib/hash/xxhash.hexa`](../../stdlib/hash/xxhash.hexa)  

## Overview

xxHash32 + xxHash64 non-cryptographic hash (Yann Collet).

Public API:
  pub fn xxh32(data: array, seed: int) -> int
    - data: array of byte values (0..255)
    - returns 32-bit hash as int in [0, 0xFFFFFFFF]
  pub fn xxh64(data: array, seed: int) -> array
    - returns [hi, lo] u32 halves of the 64-bit hash,
      since interp int literals clamp at i63 (cannot hold u64).

Stage0 notes:
  P30 no unsigned int — all 32-bit ops mask with & 0xFFFFFFFF.
  No 64-bit literals > 0x7FFFFFFF... — u64 values are
  represented as [hi, lo] pairs of 32-bit ints.

── xxHash32 primes ──────────────────────────────────────────────

## Functions

| Function | Returns |
|---|---|
| [`xxh32`](#fn-xxh32) | `int` |
| [`xxh64`](#fn-xxh64) | `array` |

### <a id="fn-xxh32"></a>`fn xxh32`

```hexa
pub fn xxh32(data: array, seed: int) -> int
```

**Parameters:** `data: array, seed: int`  
**Returns:** `int`  

_No docstring._

### <a id="fn-xxh64"></a>`fn xxh64`

```hexa
pub fn xxh64(data: array, seed: int) -> array
```

**Parameters:** `data: array, seed: int`  
**Returns:** `array`  

_No docstring._

---

← [Back to stdlib index](README.md)

# `math/rng_ctx`

_math/rng_ctx_

**Source:** [`stdlib/math/rng_ctx.hexa`](../../stdlib/math/rng_ctx.hexa)  

## Overview

Seed-propagating RNG context for deterministic multi-seed ensembles.

Builds on stdlib/math/rng (LCG + Box-Muller) and stdlib/hash/xxhash
(stream-split mixer). A RngCtx carries its own state; `rng_fork`
produces a child whose sequence is statistically independent of the
parent for each distinct `sub_id`, reproducible from the parent seed.

Public API:
  pub fn rng_make(seed) -> RngCtx
  pub fn rng_fork(ctx, sub_id) -> RngCtx
  pub fn rng_ensemble(seed, n) -> array<RngCtx>
  pub fn rng_ctx_next_u32(ctx) -> [new_ctx, u32]
  pub fn rng_ctx_next_float01(ctx) -> [new_ctx, float]
  pub fn rng_ctx_next_gauss(ctx) -> [new_ctx, z0, z1]

Determinism guarantees:
  * Same (seed, sub_id) path → byte-identical RngCtx and output stream.
  * Different sub_ids mix via xxh64 — sequences decorrelate without
    relying on the parent's advanced state.
  * No hidden globals.

## Functions

| Function | Returns |
|---|---|
| [`rng_make`](#fn-rng-make) | `RngCtx` |
| [`rng_fork`](#fn-rng-fork) | `RngCtx` |
| [`rng_ctx_next_u32`](#fn-rng-ctx-next-u32) | `array` |
| [`rng_ctx_next_float01`](#fn-rng-ctx-next-float01) | `array` |
| [`rng_ctx_next_gauss`](#fn-rng-ctx-next-gauss) | `array` |
| [`rng_ensemble`](#fn-rng-ensemble) | `array` |

### <a id="fn-rng-make"></a>`fn rng_make`

```hexa
pub fn rng_make(seed: int) -> RngCtx
```

**Parameters:** `seed: int`  
**Returns:** `RngCtx`  

_No docstring._

### <a id="fn-rng-fork"></a>`fn rng_fork`

```hexa
pub fn rng_fork(ctx: RngCtx, sub_id: int) -> RngCtx
```

**Parameters:** `ctx: RngCtx, sub_id: int`  
**Returns:** `RngCtx`  

_No docstring._

### <a id="fn-rng-ctx-next-u32"></a>`fn rng_ctx_next_u32`

```hexa
pub fn rng_ctx_next_u32(ctx: RngCtx) -> array
```

**Parameters:** `ctx: RngCtx`  
**Returns:** `array`  

_No docstring._

### <a id="fn-rng-ctx-next-float01"></a>`fn rng_ctx_next_float01`

```hexa
pub fn rng_ctx_next_float01(ctx: RngCtx) -> array
```

**Parameters:** `ctx: RngCtx`  
**Returns:** `array`  

_No docstring._

### <a id="fn-rng-ctx-next-gauss"></a>`fn rng_ctx_next_gauss`

```hexa
pub fn rng_ctx_next_gauss(ctx: RngCtx) -> array
```

**Parameters:** `ctx: RngCtx`  
**Returns:** `array`  

_No docstring._

### <a id="fn-rng-ensemble"></a>`fn rng_ensemble`

```hexa
pub fn rng_ensemble(seed: int, n: int) -> array
```

**Parameters:** `seed: int, n: int`  
**Returns:** `array`  

_No docstring._

---

← [Back to stdlib index](README.md)

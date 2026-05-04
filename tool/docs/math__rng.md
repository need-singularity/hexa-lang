# `math/rng`

_math/rng_

**Source:** [`stdlib/math/rng.hexa`](../../stdlib/math/rng.hexa)  

## Overview

Linear congruential generator + Box-Muller Gaussian sampling.

Constants: Numerical Recipes
  a = 1664525
  c = 1013904223
  m = 2^32  (masked via & 0xFFFFFFFF since interp has no 2^32 literal
             for unsigned types).

Public API:
  pub fn lcg_next(state: int) -> int
      single-step LCG; next state in [0, 2^32).
  pub fn lcg_u32(state: int) -> array
      [new_state, u32_value].
  pub fn lcg_float01(state: int) -> array
      [new_state, float in [0, 1)].
  pub fn randn_box_muller(state: int) -> array
      [new_state, z0, z1]  (two i.i.d. standard normals).
  pub fn randn_matrix(rows: int, cols: int, seed: int) -> array
      deterministic row-major flat Gaussian matrix.

## Functions

| Function | Returns |
|---|---|
| [`lcg_next`](#fn-lcg-next) | `int` |
| [`lcg_u32`](#fn-lcg-u32) | `array` |
| [`lcg_float01`](#fn-lcg-float01) | `array` |
| [`randn_box_muller`](#fn-randn-box-muller) | `array` |
| [`randn_matrix`](#fn-randn-matrix) | `array` |

### <a id="fn-lcg-next"></a>`fn lcg_next`

```hexa
pub fn lcg_next(state: int) -> int
```

**Parameters:** `state: int`  
**Returns:** `int`  

_No docstring._

### <a id="fn-lcg-u32"></a>`fn lcg_u32`

```hexa
pub fn lcg_u32(state: int) -> array
```

**Parameters:** `state: int`  
**Returns:** `array`  

_No docstring._

### <a id="fn-lcg-float01"></a>`fn lcg_float01`

```hexa
pub fn lcg_float01(state: int) -> array
```

**Parameters:** `state: int`  
**Returns:** `array`  

_No docstring._

### <a id="fn-randn-box-muller"></a>`fn randn_box_muller`

```hexa
pub fn randn_box_muller(state: int) -> array
```

**Parameters:** `state: int`  
**Returns:** `array`  

_No docstring._

### <a id="fn-randn-matrix"></a>`fn randn_matrix`

```hexa
pub fn randn_matrix(rows: int, cols: int, seed: int) -> array
```

**Parameters:** `rows: int, cols: int, seed: int`  
**Returns:** `array`  

_No docstring._

---

← [Back to stdlib index](README.md)

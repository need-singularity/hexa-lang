# `math/permille`

_math/permille_

**Source:** [`stdlib/math/permille.hexa`](../../stdlib/math/permille.hexa)  

## Overview

Fixed-point arithmetic at scale=1000 (per-mille).

Motivation: cell/edu scoring and cert reward math in anima currently
scale integer inputs by ×1000 ad-hoc to avoid IEEE-754 drift (#10
cross-substrate Φ). This module bundles that convention into an
explicit `PerMille` type so the ×1000 discipline is visible at the
API surface and mixing raw ints / floats / ratios is a compile-level
mistake rather than a silent rounding bug.

Semantics
  - All values are stored as an int `pm` where pm = value × 1000,
    half-away-from-zero rounding at construction time.
  - Arithmetic that preserves the scale (add/sub, mul_int, div_int)
    returns PerMille with the same scale.
  - `pm_mul_pm(a, b)` reduces the implicit ×10^6 product back to
    ×10^3 by dividing by 1000 with banker's rounding.

Public API:
  pub fn pm(n: int)                        -> PerMille  // 1.000  from int 1
  pub fn pm_of_permille(raw: int)          -> PerMille  // raw taken as-is
  pub fn pm_of_percent(pc: int)            -> PerMille  // 123 → 1.230
  pub fn pm_of_ratio(num: int, den: int)   -> PerMille
  pub fn pm_of_float(f: float)             -> PerMille
  pub fn pm_add(a, b)                      -> PerMille
  pub fn pm_sub(a, b)                      -> PerMille
  pub fn pm_mul_int(a, k: int)             -> PerMille
  pub fn pm_div_int(a, k: int)             -> PerMille
  pub fn pm_mul_pm(a, b)                   -> PerMille
  pub fn pm_neg(a)                         -> PerMille
  pub fn pm_clamp(a, lo, hi)               -> PerMille
  pub fn pm_to_float(a)                    -> float
  pub fn pm_to_int_floor(a)                -> int
  pub fn pm_to_string(a)                   -> string  // "1.230"
  pub fn pm_eq(a, b)                       -> bool
  pub fn pm_cmp(a, b)                      -> int   // -1 / 0 / 1

## Functions

| Function | Returns |
|---|---|
| [`pm`](#fn-pm) | `PerMille` |
| [`pm_of_permille`](#fn-pm-of-permille) | `PerMille` |
| [`pm_of_percent`](#fn-pm-of-percent) | `PerMille` |
| [`pm_of_ratio`](#fn-pm-of-ratio) | `PerMille` |
| [`pm_of_float`](#fn-pm-of-float) | `PerMille` |
| [`pm_add`](#fn-pm-add) | `PerMille` |
| [`pm_sub`](#fn-pm-sub) | `PerMille` |
| [`pm_mul_int`](#fn-pm-mul-int) | `PerMille` |
| [`pm_div_int`](#fn-pm-div-int) | `PerMille` |
| [`pm_mul_pm`](#fn-pm-mul-pm) | `PerMille` |
| [`pm_neg`](#fn-pm-neg) | `PerMille` |
| [`pm_clamp`](#fn-pm-clamp) | `PerMille` |
| [`pm_to_float`](#fn-pm-to-float) | `float` |
| [`pm_to_int_floor`](#fn-pm-to-int-floor) | `int` |
| [`pm_to_string`](#fn-pm-to-string) | `string` |
| [`pm_eq`](#fn-pm-eq) | `bool` |
| [`pm_cmp`](#fn-pm-cmp) | `int` |

### <a id="fn-pm"></a>`fn pm`

```hexa
pub fn pm(n: int) -> PerMille
```

**Parameters:** `n: int`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-of-permille"></a>`fn pm_of_permille`

```hexa
pub fn pm_of_permille(raw: int) -> PerMille
```

**Parameters:** `raw: int`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-of-percent"></a>`fn pm_of_percent`

```hexa
pub fn pm_of_percent(pc: int) -> PerMille
```

**Parameters:** `pc: int`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-of-ratio"></a>`fn pm_of_ratio`

```hexa
pub fn pm_of_ratio(num: int, den: int) -> PerMille
```

**Parameters:** `num: int, den: int`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-of-float"></a>`fn pm_of_float`

```hexa
pub fn pm_of_float(f: float) -> PerMille
```

**Parameters:** `f: float`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-add"></a>`fn pm_add`

```hexa
pub fn pm_add(a: PerMille, b: PerMille) -> PerMille
```

**Parameters:** `a: PerMille, b: PerMille`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-sub"></a>`fn pm_sub`

```hexa
pub fn pm_sub(a: PerMille, b: PerMille) -> PerMille
```

**Parameters:** `a: PerMille, b: PerMille`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-mul-int"></a>`fn pm_mul_int`

```hexa
pub fn pm_mul_int(a: PerMille, k: int) -> PerMille
```

**Parameters:** `a: PerMille, k: int`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-div-int"></a>`fn pm_div_int`

```hexa
pub fn pm_div_int(a: PerMille, k: int) -> PerMille
```

**Parameters:** `a: PerMille, k: int`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-mul-pm"></a>`fn pm_mul_pm`

```hexa
pub fn pm_mul_pm(a: PerMille, b: PerMille) -> PerMille
```

**Parameters:** `a: PerMille, b: PerMille`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-neg"></a>`fn pm_neg`

```hexa
pub fn pm_neg(a: PerMille) -> PerMille
```

**Parameters:** `a: PerMille`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-clamp"></a>`fn pm_clamp`

```hexa
pub fn pm_clamp(a: PerMille, lo: PerMille, hi: PerMille) -> PerMille
```

**Parameters:** `a: PerMille, lo: PerMille, hi: PerMille`  
**Returns:** `PerMille`  

_No docstring._

### <a id="fn-pm-to-float"></a>`fn pm_to_float`

```hexa
pub fn pm_to_float(a: PerMille) -> float
```

**Parameters:** `a: PerMille`  
**Returns:** `float`  

_No docstring._

### <a id="fn-pm-to-int-floor"></a>`fn pm_to_int_floor`

```hexa
pub fn pm_to_int_floor(a: PerMille) -> int
```

**Parameters:** `a: PerMille`  
**Returns:** `int`  

_No docstring._

### <a id="fn-pm-to-string"></a>`fn pm_to_string`

```hexa
pub fn pm_to_string(a: PerMille) -> string
```

**Parameters:** `a: PerMille`  
**Returns:** `string`  

_No docstring._

### <a id="fn-pm-eq"></a>`fn pm_eq`

```hexa
pub fn pm_eq(a: PerMille, b: PerMille) -> bool
```

**Parameters:** `a: PerMille, b: PerMille`  
**Returns:** `bool`  

_No docstring._

### <a id="fn-pm-cmp"></a>`fn pm_cmp`

```hexa
pub fn pm_cmp(a: PerMille, b: PerMille) -> int
```

**Parameters:** `a: PerMille, b: PerMille`  
**Returns:** `int`  

_No docstring._

---

← [Back to stdlib index](README.md)

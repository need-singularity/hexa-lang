# `matrix/construct`

_matrix/construct (Wave A)_

**Source:** [`stdlib/matrix/construct.hexa`](../../stdlib/matrix/construct.hexa)  

## Overview

Matrix constructors. Returns flat row-major `array<float>` length m*n.

Public API:
  pub fn matrix_eye(n)          -> array<float>   length n*n  (n × n I)
  pub fn matrix_diag(v)         -> array<float>   length n*n  (n = len(v))
  pub fn matrix_zeros(m, n)     -> array<float>   length m*n
  pub fn matrix_ones(m, n)      -> array<float>   length m*n

All matrices are row-major: out[i, j] = out[i * n + j].

── matrix_zeros ─────────────────────────────────────────────────

## Functions

| Function | Returns |
|---|---|
| [`matrix_zeros`](#fn-matrix-zeros) | `array` |
| [`matrix_ones`](#fn-matrix-ones) | `array` |
| [`matrix_eye`](#fn-matrix-eye) | `array` |
| [`matrix_diag`](#fn-matrix-diag) | `array` |

### <a id="fn-matrix-zeros"></a>`fn matrix_zeros`

```hexa
pub fn matrix_zeros(m: int, n: int) -> array
```

**Parameters:** `m: int, n: int`  
**Returns:** `array`  

_No docstring._

### <a id="fn-matrix-ones"></a>`fn matrix_ones`

```hexa
pub fn matrix_ones(m: int, n: int) -> array
```

**Parameters:** `m: int, n: int`  
**Returns:** `array`  

── matrix_ones ──────────────────────────────────────────────────

### <a id="fn-matrix-eye"></a>`fn matrix_eye`

```hexa
pub fn matrix_eye(n: int) -> array
```

**Parameters:** `n: int`  
**Returns:** `array`  

── matrix_eye ───────────────────────────────────────────────────
n × n identity matrix.

### <a id="fn-matrix-diag"></a>`fn matrix_diag`

```hexa
pub fn matrix_diag(v: array) -> array
```

**Parameters:** `v: array`  
**Returns:** `array`  

── matrix_diag ──────────────────────────────────────────────────
Build n × n square matrix with `v` on the main diagonal, where n =
len(v). Off-diagonals are 0.

---

← [Back to stdlib index](README.md)

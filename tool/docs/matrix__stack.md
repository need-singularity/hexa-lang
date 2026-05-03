# `matrix/stack`

_matrix/stack (Wave A)_

**Source:** [`stdlib/matrix/stack.hexa`](../../stdlib/matrix/stack.hexa)  

## Overview

Stacking and transpose helpers for flat row-major matrices.

Public API:
  pub fn matrix_vstack(arrs, ncols)    -> array<float>
      Concatenate flat row-major matrices along axis 0 (vertical).
      `arrs` is an array-of-arrays (each entry is a flat row-major
      matrix with the same column count `ncols`). Returned length is
      Σ_i len(arrs[i]). Per-input row count is inferred:
        len(arrs[i]) must be divisible by ncols.

  pub fn matrix_hstack(arrs, nrows)    -> array<float>
      Concatenate flat row-major matrices along axis 1 (horizontal).
      Each entry has the same row count `nrows`; per-input column
      count is inferred: len(arrs[i]) / nrows.

  pub fn matrix_transpose(M, m, n)     -> array<float>   length n*m
      Returns Mᵀ as a flat row-major matrix of shape (n, m).

── matrix_vstack ────────────────────────────────────────────────
Output shape: (Σ rows_i, ncols). All inputs must have len divisible
by ncols. Order is preserved: arrs[0] rows first, then arrs[1], etc.

## Functions

| Function | Returns |
|---|---|
| [`matrix_vstack`](#fn-matrix-vstack) | `array` |
| [`matrix_hstack`](#fn-matrix-hstack) | `array` |
| [`matrix_transpose`](#fn-matrix-transpose) | `array` |

### <a id="fn-matrix-vstack"></a>`fn matrix_vstack`

```hexa
pub fn matrix_vstack(arrs: array, ncols: int) -> array
```

**Parameters:** `arrs: array, ncols: int`  
**Returns:** `array`  

_No docstring._

### <a id="fn-matrix-hstack"></a>`fn matrix_hstack`

```hexa
pub fn matrix_hstack(arrs: array, nrows: int) -> array
```

**Parameters:** `arrs: array, nrows: int`  
**Returns:** `array`  

── matrix_hstack ────────────────────────────────────────────────
Output shape: (nrows, Σ cols_i). All inputs share `nrows` rows; each
input's column count is inferred from len(mat) / nrows. Output is
laid out row-major so we have to interleave per-row across inputs.

### <a id="fn-matrix-transpose"></a>`fn matrix_transpose`

```hexa
pub fn matrix_transpose(M: array, m: int, n: int) -> array
```

**Parameters:** `M: array, m: int, n: int`  
**Returns:** `array`  

── matrix_transpose ─────────────────────────────────────────────
Mᵀ[j, i] = M[i, j]. Output is flat row-major shape (n, m).

---

← [Back to stdlib index](README.md)

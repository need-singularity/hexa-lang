# `linalg/reference`

_linalg/reference_

**Source:** [`stdlib/linalg/reference.hexa`](../../stdlib/linalg/reference.hexa)  

## Overview

Pure-hexa BLAS-lite fallback (roadmap 57 / B16).

All matrices are row-major, contiguous `array<float>`. No FFI, no raw
buffers — usable under interp interpretation and deterministic across
every backend. The FFI backend (linalg/ffi.hexa) calls the equivalent
runtime-builtin C kernel when it produces a speedup; otherwise it
defers here.

Indexing convention (row-major):
  A (M × K)   A[i, k] = A[i * K + k]     i ∈ [0,M), k ∈ [0,K)
  B (K × N)   B[k, j] = B[k * N + j]
  C (M × N)   C[i, j] = C[i * N + j]

Public kernels:
  sgemm_ref(m, n, k, A, B, C, alpha, beta) -> array<float>  (length m*n)
      D = alpha * A·B + beta * C. If `C` is empty (len 0) beta is
      treated as 0.0 (fresh output).
  sgemv_ref(m, n, A, x, y, alpha, beta)    -> array<float>  (length m)
      z = alpha * A·x + beta * y. If `y` is empty beta is treated as 0.
  sdot_ref(n, x, y)                        -> float
  saxpy_ref(n, alpha, x, y)                -> array<float>  (length n)
      out[i] = alpha * x[i] + y[i]
  snrm2_ref(n, x)                          -> float           sqrt(sum(x²))

Returned arrays are freshly-allocated; inputs are never mutated.

## Functions

| Function | Returns |
|---|---|
| [`sdot_ref`](#fn-sdot-ref) | `float` |
| [`saxpy_ref`](#fn-saxpy-ref) | `array` |
| [`snrm2_ref`](#fn-snrm2-ref) | `float` |

### <a id="fn-sdot-ref"></a>`fn sdot_ref`

```hexa
pub fn sdot_ref(n: int, x: array, y: array) -> float
```

**Parameters:** `n: int, x: array, y: array`  
**Returns:** `float`  

_No docstring._

### <a id="fn-saxpy-ref"></a>`fn saxpy_ref`

```hexa
pub fn saxpy_ref(n: int, alpha: float, x: array, y: array) -> array
```

**Parameters:** `n: int, alpha: float, x: array, y: array`  
**Returns:** `array`  

_No docstring._

### <a id="fn-snrm2-ref"></a>`fn snrm2_ref`

```hexa
pub fn snrm2_ref(n: int, x: array) -> float
```

**Parameters:** `n: int, x: array`  
**Returns:** `float`  

_No docstring._

---

← [Back to stdlib index](README.md)

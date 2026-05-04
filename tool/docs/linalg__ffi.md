# `linalg/ffi`

_linalg/ffi_

**Source:** [`stdlib/linalg/ffi.hexa`](../../stdlib/linalg/ffi.hexa)  

## Overview

Thin wrappers that route BLAS-lite ops through the native-C runtime
builtins (`matmul`, `matvec`) when the operation maps cleanly. Falls
back to reference kernels for ops that have no builtin (sdot / saxpy
/ snrm2) or for shapes/coefficients the builtin cannot express.

Rationale — why not @link("hxblas") directly?
  * hxblas_* takes raw `float*` pointers; stdlib arrays are boxed
    `array<float>` values. Marshalling through alloc_raw / write_f32
    would cost O(m*k) writes *before* the call and O(m*n) reads
    *after* — the builtin `matmul` is already a tight C loop with
    zero marshalling overhead and is present on every platform.
  * hxblas symbols are conditionally compiled (#ifdef __linux__) and
    not present in every hxblas dylib build — probing for symbols at
    stdlib call-time is fragile. The runtime builtins are always
    available.

If the user explicitly opts in (`HEXA_LINALG_BACKEND=ffi`) and the
builtin path applies, we take it; otherwise we fall through to the
reference implementation, so the ffi backend is never worse than ref.

Note: callers must import `reference.hexa` alongside this file — the
interp loader does not chain transitive imports through intermediate
modules, so ffi.hexa deliberately does not `import "reference.hexa"`
itself (sdot_ref / saxpy_ref / snrm2_ref are resolved in the caller's
import scope).

sgemm via builtin `matmul`:
  * builtin computes C' = A · B without alpha/beta support.
  * Builtin arg order is `matmul(A, B, M, N, K)` (M, N = output dims;
    K = contraction dim) — confirmed against the self-host
    interpreter at self/hexa_full.hexa:11069-11094. The CblasSgemm-
    flavoured (m, n, k) ordering is translated here.
  * For the common case (alpha=1, beta=0, C empty) we take the
    builtin path; otherwise we fold alpha/beta in a post-pass to
    avoid a second full-matmul for trivial scalings.

## Functions

| Function | Returns |
|---|---|
| [`sdot_ffi`](#fn-sdot-ffi) | `float` |
| [`saxpy_ffi`](#fn-saxpy-ffi) | `array` |
| [`snrm2_ffi`](#fn-snrm2-ffi) | `float` |

### <a id="fn-sdot-ffi"></a>`fn sdot_ffi`

```hexa
pub fn sdot_ffi(n: int, x: array, y: array) -> float
```

**Parameters:** `n: int, x: array, y: array`  
**Returns:** `float`  

No builtin equivalents for sdot / saxpy / snrm2 as of roadmap 57.
Defer to the reference kernels — they are already single-pass tight
loops that the hexa interpreter runs through its int/float fast path.

### <a id="fn-saxpy-ffi"></a>`fn saxpy_ffi`

```hexa
pub fn saxpy_ffi(n: int, alpha: float, x: array, y: array) -> array
```

**Parameters:** `n: int, alpha: float, x: array, y: array`  
**Returns:** `array`  

_No docstring._

### <a id="fn-snrm2-ffi"></a>`fn snrm2_ffi`

```hexa
pub fn snrm2_ffi(n: int, x: array) -> float
```

**Parameters:** `n: int, x: array`  
**Returns:** `float`  

_No docstring._

---

← [Back to stdlib index](README.md)

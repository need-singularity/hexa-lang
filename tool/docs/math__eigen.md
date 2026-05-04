# `math/eigen`

_math/eigen_

**Source:** [`stdlib/math/eigen.hexa`](../../stdlib/math/eigen.hexa)  

## Overview

Symmetric eigendecomposition via cyclic Jacobi rotations (roadmap 58).

Public API:
  pub fn eigh(A: array, n: int) -> array
    Symmetric n x n matrix given row-major flat (length n*n).
    Returns [values, vectors] where values is a length-n array of
    eigenvalues sorted descending, vectors is an array-of-rows so
    vectors[k] is the k-th eigenvector (length n). Eigenvectors are
    sign-canonicalized: first entry with |v| > 1e-10 is positive.

  pub fn eigvalsh(A: array, n: int) -> array
    Values only (sorted descending).

  pub fn eigh_jacobi(A: array, n: int, max_sweeps: int, tol: float) -> array
    Returns [values, vectors, actual_sweeps].

Algorithm: classical cyclic Jacobi. Each sweep visits every super-
diagonal pair (i, j). A pair is rotated by angle theta so that the
off-diagonal M[i,j] is zeroed. Iteration converges quadratically for
symmetric matrices. Convergence test: off-diagonal sum below
tol * frobenius_norm(M).

All functions avoid BLAS dependencies — the (i,j) plane rotation is
applied inline with six floating-point multiplies per affected entry.

## Functions

| Function | Returns |
|---|---|
| [`eigh_jacobi`](#fn-eigh-jacobi) | `array` |
| [`eigh`](#fn-eigh) | `array` |
| [`eigvalsh`](#fn-eigvalsh) | `array` |

### <a id="fn-eigh-jacobi"></a>`fn eigh_jacobi`

```hexa
pub fn eigh_jacobi(A: array, n: int, max_sweeps: int, tol: float) -> array
```

**Parameters:** `A: array, n: int, max_sweeps: int, tol: float`  
**Returns:** `array`  

── Core Jacobi driver ──────────────────────────────────────────

### <a id="fn-eigh"></a>`fn eigh`

```hexa
pub fn eigh(A: array, n: int) -> array
```

**Parameters:** `A: array, n: int`  
**Returns:** `array`  

_No docstring._

### <a id="fn-eigvalsh"></a>`fn eigvalsh`

```hexa
pub fn eigvalsh(A: array, n: int) -> array
```

**Parameters:** `A: array, n: int`  
**Returns:** `array`  

_No docstring._

---

← [Back to stdlib index](README.md)

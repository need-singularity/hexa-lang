# `optim/projector`

_optim/projector_

**Source:** [`stdlib/optim/projector.hexa`](../../stdlib/optim/projector.hexa)  

## Overview

Orthogonal projector primitives for Constraint-Projected
Gradient Descent (CPGD).

Conventions (match stdlib/optim.hexa + stdlib/nn.hexa):
  - Matrices are flat row-major float arrays of length rows*cols.
  - Eigenvector inputs are a list-of-vectors (each vec a flat array)
    because that is how callers already load them (e.g. from JSON).
  - All functions are pure (no hidden state); functional return form.

Math (P_S = V^T · V, V has eigenvec_i as row i, orthonormal rows):
  - P_S is symmetric and idempotent (P_S · P_S = P_S).
  - For any vector g, (g · P_S) is the orthogonal projection of g
    onto span{v_1..v_k}.  Since P_S is symmetric, row-projection
    of a matrix G via G · P_S == column-projection via P_S · G^T.

Public API:
  pub fn build_projector(eigenvecs: array) -> array
      V^T · V; returns flat N×N row-major, N = len(eigenvecs[0]).
  pub fn verify_projector_idempotent(P: array, eps: float) -> bool
      Max |P·P - P|_ij < eps.
  pub fn project_row(g: array, P: array) -> array
      Single-row projection: g · P  (N-vector, P is flat N×N).
  pub fn matmul_right(G: array, P: array) -> array
      Matrix G · P row-wise; G is flat M×N, P is flat N×N.

── build_projector ──────────────────────────────────────────
eigenvecs: array of arrays (each length N, orthonormal rows of V)
returns flat N×N row-major with P[a][b] = sum_k v_k[a] * v_k[b]

## Functions

| Function | Returns |
|---|---|
| [`build_projector`](#fn-build-projector) | `array` |
| [`verify_projector_idempotent`](#fn-verify-projector-idempotent) | `bool` |
| [`project_row`](#fn-project-row) | `array` |
| [`matmul_right`](#fn-matmul-right) | `array` |

### <a id="fn-build-projector"></a>`fn build_projector`

```hexa
pub fn build_projector(eigenvecs: array) -> array
```

**Parameters:** `eigenvecs: array`  
**Returns:** `array`  

_No docstring._

### <a id="fn-verify-projector-idempotent"></a>`fn verify_projector_idempotent`

```hexa
pub fn verify_projector_idempotent(P: array, eps: float) -> bool
```

**Parameters:** `P: array, eps: float`  
**Returns:** `bool`  

── verify_projector_idempotent ──────────────────────────────
Checks ‖P·P − P‖_∞ (max abs element) < eps.
P is flat N×N; N inferred as isqrt(len(P)).

### <a id="fn-project-row"></a>`fn project_row`

```hexa
pub fn project_row(g: array, P: array) -> array
```

**Parameters:** `g: array, P: array`  
**Returns:** `array`  

── project_row ──────────────────────────────────────────────
Single-row projection: out[b] = sum_a g[a] * P[a,b]

### <a id="fn-matmul-right"></a>`fn matmul_right`

```hexa
pub fn matmul_right(G: array, P: array) -> array
```

**Parameters:** `G: array, P: array`  
**Returns:** `array`  

── matmul_right ─────────────────────────────────────────────
G · P row-wise; G is flat M×N, P is flat N×N, result is flat M×N.
N inferred from isqrt(len(P)); M = len(G) / N.

---

← [Back to stdlib index](README.md)

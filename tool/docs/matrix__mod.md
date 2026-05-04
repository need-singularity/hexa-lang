# `matrix/mod`

_matrix (Wave A of numeric stdlib)_

**Source:** [`stdlib/matrix/mod.hexa`](../../stdlib/matrix/mod.hexa)  

## Overview

Axis-aware reductions over flat row-major matrices.

Source: Track 2 of Ω-philosophy parallel cycle (anima 2026-04-26),
design proposal `anima/design/hexa_lang_numeric_stdlib_proposal_20260426.md`
— Tier 1, Wave A.

Conventions (lifted from existing `stdlib/linalg/reference.hexa` and
`stdlib/math/eigen.hexa`):
  * Matrices are flat `array<float>` row-major with explicit (m, n)
    parameters: M[i, j] = M[i * n + j], i ∈ [0, m), j ∈ [0, n).
  * Pure-hexa naive implementations (Tier 1). Future Tier 3 native
    promotion can be wired through a sibling `dispatch.hexa` exactly
    like `stdlib/linalg/dispatch.hexa` — Wave A intentionally keeps
    the surface single-backend so callers can rely on it today.
  * Returned arrays are freshly allocated; inputs are never mutated.
  * Convention for `axis`:
      axis = 0 → reduce ALONG rows (collapse the row dim, keep cols)
                 Output has length n (one value per column).
      axis = 1 → reduce ALONG cols (collapse the col dim, keep rows)
                 Output has length m (one value per row).
    This matches numpy / anima research convention.

Public API (this file):
  pub fn matrix_mean_axis(M, m, n, axis)   -> array<float>
  pub fn matrix_sum_axis(M, m, n, axis)    -> array<float>
  pub fn matrix_std_axis(M, m, n, axis)    -> array<float>     ddof=0
  pub fn matrix_argmax_axis(M, m, n, axis) -> array<int>       indices

See sibling files in this directory:
  construct.hexa  — eye / diag / zeros / ones
  stack.hexa      — vstack / hstack / transpose
  test/matrix_test.hexa — selftest

Import idiom — interp does not chain transitive imports through
modules. Callers should import the leaf files directly:

  import "<prefix>/stdlib/matrix/mod.hexa"
  import "<prefix>/stdlib/matrix/construct.hexa"
  import "<prefix>/stdlib/matrix/stack.hexa"

Compatibility goal (per design proposal §4): 6-digit relative parity
with reference numpy run on documented operations. Codified per-test.

── matrix_mean_axis ─────────────────────────────────────────────
Column means (axis=0): out[j] = (1/m) Σ_i M[i,j], length n.
Row    means (axis=1): out[i] = (1/n) Σ_j M[i,j], length m.

## Functions

| Function | Returns |
|---|---|
| [`matrix_mean_axis`](#fn-matrix-mean-axis) | `array` |
| [`matrix_sum_axis`](#fn-matrix-sum-axis) | `array` |
| [`matrix_std_axis`](#fn-matrix-std-axis) | `array` |
| [`matrix_argmax_axis`](#fn-matrix-argmax-axis) | `array` |

### <a id="fn-matrix-mean-axis"></a>`fn matrix_mean_axis`

```hexa
pub fn matrix_mean_axis(M: array, m: int, n: int, axis: int) -> array
```

**Parameters:** `M: array, m: int, n: int, axis: int`  
**Returns:** `array`  

_No docstring._

### <a id="fn-matrix-sum-axis"></a>`fn matrix_sum_axis`

```hexa
pub fn matrix_sum_axis(M: array, m: int, n: int, axis: int) -> array
```

**Parameters:** `M: array, m: int, n: int, axis: int`  
**Returns:** `array`  

── matrix_sum_axis ──────────────────────────────────────────────
Column sums (axis=0): out[j] = Σ_i M[i,j], length n.
Row    sums (axis=1): out[i] = Σ_j M[i,j], length m.

### <a id="fn-matrix-std-axis"></a>`fn matrix_std_axis`

```hexa
pub fn matrix_std_axis(M: array, m: int, n: int, axis: int) -> array
```

**Parameters:** `M: array, m: int, n: int, axis: int`  
**Returns:** `array`  

── matrix_std_axis (population std, ddof=0) ─────────────────────
σ_axis0[j] = sqrt((1/m) Σ_i (M[i,j] - μ_j)^2)        length n
σ_axis1[i] = sqrt((1/n) Σ_j (M[i,j] - μ_i)^2)        length m

Two-pass algorithm: first pass computes mean, second pass accumulates
squared deviation. This avoids the catastrophic cancellation of the
`E[X^2] - E[X]^2` shortcut on FP arrays whose values are far from 0.

### <a id="fn-matrix-argmax-axis"></a>`fn matrix_argmax_axis`

```hexa
pub fn matrix_argmax_axis(M: array, m: int, n: int, axis: int) -> array
```

**Parameters:** `M: array, m: int, n: int, axis: int`  
**Returns:** `array`  

── matrix_argmax_axis ───────────────────────────────────────────
Column argmax (axis=0): out[j] = arg max_i M[i,j] (row index), length n.
Row    argmax (axis=1): out[i] = arg max_j M[i,j] (col index), length m.

On ties, returns the smallest index — matches numpy.argmax behaviour.
Output element type is int (indices), not float.

---

← [Back to stdlib index](README.md)

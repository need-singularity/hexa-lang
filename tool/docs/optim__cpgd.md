# `optim/cpgd`

_optim/cpgd_

**Source:** [`stdlib/optim/cpgd.hexa`](../../stdlib/optim/cpgd.hexa)  

## Overview

Constraint-Projected Gradient Descent (CPGD).

Promotes anima/edu/lora/cpgd_{wrapper,minimal_proof} math into
a reusable stdlib primitive.

Math (same-shape SGD that stays on a cert-admissible manifold
S = span{v_1..v_k}, V = [v_1..v_k]^T, v_i orthonormal):
    P_S   = V^T · V           (orthogonal projector onto S)
    W_0   = P_S · W_rand      (closed-form init → on-manifold)
    W_{k+1} = W_k − lr · (grad_k · P_S)   (projected step)

Conventions:
  - Matrices flat row-major float arrays (match stdlib/optim.hexa).
  - Eigenvec inputs are list-of-vectors (caller-friendly).
  - In-place mutation idiom: cpgd_step mutates W in-place AND
    returns it (mirrors adam_step).  Callers may alias the input.

RNG: uses stdlib/math/rng randn_matrix (Numerical Recipes LCG).
  Consumers that need a different LCG family (e.g. anima uses
  BSD: a=1103515245, c=12345, m=2^31) should invoke cpgd_step
  with their own pre-generated W_rand rather than cpgd_init.

Public API:
  pub fn cpgd_init(P_S, rows, cols, seed) -> array
      Flat on-manifold W_0 = matmul_right(randn_matrix, P_S).
  pub fn cpgd_step(W, grad, P_S, lr) -> array
      W' = W − lr * (grad · P_S).  In-place; returns W.
  pub fn cpgd_step_fn(W, grad, project, lr) -> array
      Callback form; `project` is any fn(array)->array that takes
      a flat M×N gradient matrix and returns a flat M×N projected
      gradient.  In-place on W; returns W.
  pub fn row_cosines(W, eigenvecs) -> array
      For each row i of W, cos(W[i,:], eigenvecs[i]).  Monitor only.
  pub fn cpgd_state(W, P_S, lr, step) -> array
      Wraps state as [W, P_S, lr, step]; mirrors adam's
      [params, m, v] carry idiom.

## Functions

| Function | Returns |
|---|---|
| [`cpgd_init`](#fn-cpgd-init) | `array` |
| [`cpgd_step`](#fn-cpgd-step) | `array` |
| [`cpgd_step_fn`](#fn-cpgd-step-fn) | `array` |
| [`row_cosines`](#fn-row-cosines) | `array` |
| [`cpgd_state`](#fn-cpgd-state) | `array` |

### <a id="fn-cpgd-init"></a>`fn cpgd_init`

```hexa
pub fn cpgd_init(P_S: array, rows: int, cols: int, seed: int) -> array
```

**Parameters:** `P_S: array, rows: int, cols: int, seed: int`  
**Returns:** `array`  

── cpgd_init ────────────────────────────────────────────────
Draw W_rand ~ N(0,1) via stdlib rng, project row-wise onto S.
Result lies in S at every row (W_0 ∈ S^rows).

### <a id="fn-cpgd-step"></a>`fn cpgd_step`

```hexa
pub fn cpgd_step(W: array, grad: array, P_S: array, lr: float) -> array
```

**Parameters:** `W: array, grad: array, P_S: array, lr: float`  
**Returns:** `array`  

── cpgd_step (concrete projector form) ──────────────────────
W' = W − lr * (grad · P_S).  In-place on W; returns W.

### <a id="fn-cpgd-step-fn"></a>`fn cpgd_step_fn`

```hexa
pub fn cpgd_step_fn(W: array, grad: array, project, lr: float) -> array
```

**Parameters:** `W: array, grad: array, project, lr: float`  
**Returns:** `array`  

── cpgd_step_fn (callback / schema-agnostic form) ───────────
`project` is any function flat_M×N -> flat_M×N.  Useful for
non-orthogonal subspace projections (future cert schemas) or
for masking / sparsity projections.  In-place on W; returns W.

### <a id="fn-row-cosines"></a>`fn row_cosines`

```hexa
pub fn row_cosines(W: array, eigenvecs: array) -> array
```

**Parameters:** `W: array, eigenvecs: array`  
**Returns:** `array`  

── row_cosines (monitor) ────────────────────────────────────
For each row i of W (flat rows×cols), compute
cos(W[i,:], eigenvecs[i]).  Returns float array of length rows.
Used for certification monitoring; not part of the step itself.

### <a id="fn-cpgd-state"></a>`fn cpgd_state`

```hexa
pub fn cpgd_state(W: array, P_S: array, lr: float, step: int) -> array
```

**Parameters:** `W: array, P_S: array, lr: float, step: int`  
**Returns:** `array`  

── cpgd_state ───────────────────────────────────────────────
State-carry idiom (mirrors adam returning [params, m, v]).
Returns a 4-element array: [W, P_S, lr, step].

---

← [Back to stdlib index](README.md)

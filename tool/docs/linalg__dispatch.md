# `linalg/dispatch`

_linalg/dispatch_

**Source:** [`stdlib/linalg/dispatch.hexa`](../../stdlib/linalg/dispatch.hexa)  

## Overview

Backend selector. Honors env `HEXA_LINALG_BACKEND`:
  "ref"  → always pure-hexa reference kernels
  "ffi"  → always runtime-builtin kernels (matmul / matvec)
  "auto" → same as "ffi" when a builtin path applies; else "ref"
  unset / empty / any other value → "auto"

`auto` today is behaviourally identical to `ffi` because `ffi.hexa`
already falls back to reference internally for ops without a builtin
— the dispatch layer just routes the entry point.

Callers must import `reference.hexa` and `ffi.hexa` alongside this
file — interp does not chain transitive imports, so this module
deliberately does not re-import them. See mod.hexa for the canonical
triple-import idiom.

## Functions

| Function | Returns |
|---|---|
| [`sdot`](#fn-sdot) | `float` |
| [`saxpy`](#fn-saxpy) | `array` |
| [`snrm2`](#fn-snrm2) | `float` |
| [`linalg_backend_name`](#fn-linalg-backend-name) | `string` |

### <a id="fn-sdot"></a>`fn sdot`

```hexa
pub fn sdot(n: int, x: array, y: array) -> float
```

**Parameters:** `n: int, x: array, y: array`  
**Returns:** `float`  

_No docstring._

### <a id="fn-saxpy"></a>`fn saxpy`

```hexa
pub fn saxpy(n: int, alpha: float, x: array, y: array) -> array
```

**Parameters:** `n: int, alpha: float, x: array, y: array`  
**Returns:** `array`  

_No docstring._

### <a id="fn-snrm2"></a>`fn snrm2`

```hexa
pub fn snrm2(n: int, x: array) -> float
```

**Parameters:** `n: int, x: array`  
**Returns:** `float`  

_No docstring._

### <a id="fn-linalg-backend-name"></a>`fn linalg_backend_name`

```hexa
pub fn linalg_backend_name() -> string
```

**Parameters:** _none_  
**Returns:** `string`  

Exposed for introspection / tests.

---

← [Back to stdlib index](README.md)

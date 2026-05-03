# `cert/selftest`

_triplet verification + determinism probe_

**Source:** [`stdlib/cert/selftest.hexa`](../../stdlib/cert/selftest.hexa)  

## Overview

 stdlib/cert/selftest.hexa — triplet verification + determinism probe

 * verify_triplet   : all slugs load with sat ≥ 0.999 (anima triplet gate)
 * check_determinism: run a zero-arg thunk twice, compare SHA256 of its
                      string-coerced result over both runs

 Implementation note: the hex digest is computed by writing the string to
 a short-lived temp file and shelling out to `shasum -a 256 <tmp>`
 (macOS) with a fallback to `sha256sum`. This avoids shell-piping the
 payload through stdin, which the interp interpreter can double-evaluate
 under certain exec-in-closure patterns.

## Functions

| Function | Returns |
|---|---|
| [`verify_triplet`](#fn-verify-triplet) | `_inferred_` |
| [`check_determinism`](#fn-check-determinism) | `_inferred_` |

### <a id="fn-verify-triplet"></a>`fn verify_triplet`

```hexa
pub fn verify_triplet(cert_dir, slugs)
```

**Parameters:** `cert_dir, slugs`  
**Returns:** _inferred_  

All slugs in `slugs` must load (present=true) with sat >= 0.999.

### <a id="fn-check-determinism"></a>`fn check_determinism`

```hexa
pub fn check_determinism(thunk)
```

**Parameters:** `thunk`  
**Returns:** _inferred_  

Run `thunk` twice, compare SHA256 of the string-coerced results.
Accepts a zero-arg closure — caller closes over any inputs.

---

← [Back to stdlib index](README.md)

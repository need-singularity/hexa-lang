# `cert/factor`

_aggregate a list<CertScore> → FactorResult_

**Source:** [`stdlib/cert/factor.hexa`](../../stdlib/cert/factor.hexa)  

## Overview

 stdlib/cert/factor.hexa — aggregate a list<CertScore> → FactorResult

 Generalized form of anima cert_gate's compute_an11 / compute_hexad /
 compute_mk8 / compute_aux helpers — single aggregate() function with
 an explicit skip-list (for baseline-reference slugs that must not
 distort the arithmetic mean).

## Functions

| Function | Returns |
|---|---|
| [`aggregate`](#fn-aggregate) | `_inferred_` |

### <a id="fn-aggregate"></a>`fn aggregate`

```hexa
pub fn aggregate(name, scores, skip)
```

**Parameters:** `name, scores, skip`  
**Returns:** _inferred_  

Arithmetic mean of CertScore.sat across `scores`, excluding:
  * entries whose slug appears in `skip` (e.g. r0 baseline controls)
  * entries with present == false (missing files contribute nothing)

---

← [Back to stdlib index](README.md)

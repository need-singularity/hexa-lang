# `cert/verdict`

_verdict vocabulary → satisfaction score_

**Source:** [`stdlib/cert/verdict.hexa`](../../stdlib/cert/verdict.hexa)  

## Overview

 stdlib/cert/verdict.hexa — verdict vocabulary → satisfaction score

 Phase 1 of the cert stdlib extraction from anima/tool/cert_gate.hexa.
 verdict_base_score values are taken VERBATIM from cert_gate.hexa so that
 any downstream anima tool consuming this stdlib preserves bit-identical
 reward_mult outputs (raw#10 proof-carrying determinism).

 The three additional vocabulary terms listed in the Phase-1 spec
 (synthetic_proof / natural_observed / conjecture) are appended below
 the anima-derived table — they are new β additions, not overrides.

Map a verdict string → base satisfaction score ∈ [0.0, 1.0].

## Functions

| Function | Returns |
|---|---|
| [`verdict_score`](#fn-verdict-score) | `_inferred_` |
| [`is_satisfied`](#fn-is-satisfied) | `_inferred_` |

### <a id="fn-verdict-score"></a>`fn verdict_score`

```hexa
pub fn verdict_score(v)
```

**Parameters:** `v`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-is-satisfied"></a>`fn is_satisfied`

```hexa
pub fn is_satisfied(sat)
```

**Parameters:** `sat`  
**Returns:** _inferred_  

True iff satisfaction is at the "fully satisfied" threshold used by
anima cert_gate (sat >= 0.999 — tolerant of f64 round-off).

---

← [Back to stdlib index](README.md)

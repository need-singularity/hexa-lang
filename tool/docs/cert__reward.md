# `cert/reward`

_soft reward-shaping multiplier_

**Source:** [`stdlib/cert/reward.hexa`](../../stdlib/cert/reward.hexa)  

## Overview

 stdlib/cert/reward.hexa — soft reward-shaping multiplier

 Generalized form of anima cert_gate.hexa's compute_reward(). The anima
 β defaults (floor=0.5, linear_lo=0.75, linear_hi=1.25, aux_weight=0.25,
 ceiling=1.5) are preserved in default_config() so Phase-2 drop-in
 refactors of cert_gate.hexa stay bit-identical.

 Formula (core_sat = mean(factor_sats)):
    if core_sat < floor        → reward_mult = 0, passed = false
    else linear_sat = linear_lo + (linear_hi − linear_lo) ·
                       (core_sat − floor) / (1.0 − floor)
         aux_contrib = aux.sat · aux_weight
         reward_mult = clamp(linear_sat + aux_contrib, 0, ceiling)

 For the anima β defaults this reduces to:
    linear_sat = 1.0 + 0.25 · (2·core_sat − 1)   (identical to anima)
 — because (linear_hi − linear_lo)/(1 − floor) = 0.5/0.5 = 1, and
   linear_lo = 0.75 so at core_sat=0.5 → 0.75, at core_sat=1.0 → 1.25.

## Functions

| Function | Returns |
|---|---|
| [`default_config`](#fn-default-config) | `_inferred_` |
| [`compute_reward`](#fn-compute-reward) | `_inferred_` |

### <a id="fn-default-config"></a>`fn default_config`

```hexa
pub fn default_config()
```

**Parameters:** _none_  
**Returns:** _inferred_  

Anima β defaults — do NOT change without updating cert_gate parity.

### <a id="fn-compute-reward"></a>`fn compute_reward`

```hexa
pub fn compute_reward(factors, aux, cfg)
```

**Parameters:** `factors, aux, cfg`  
**Returns:** _inferred_  

Generalized soft reward shaping. `factors` is a list<FactorResult>
whose `.sat` fields contribute to core_sat. `aux` is a single
FactorResult providing the bounded bonus signal.

---

← [Back to stdlib index](README.md)

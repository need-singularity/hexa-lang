# `policy/dual_track`

_policy/dual_track_

**Source:** [`stdlib/policy/dual_track.hexa`](../../stdlib/policy/dual_track.hexa)  

## Overview

Deterministic blind A/B routing for benchmarks and rollouts.

Motivation: bench/zeta_likert and similar blind-comparison harnesses
need a reproducible assignment of request-keys to tracks without
leaking which track served which request. Hashing a (salt || key)
tuple with xxh32 gives a byte-stable bucket across substrates and
survives the strict_fp determinism budget (no float ops in the hot
path).

Public API:
  pub fn make_router(tracks: [TrackWeight], salt: int) -> Router
  pub fn route(r: Router, key: string)               -> string
  pub fn route_bucket(r: Router, key: string, n: int) -> int     // 0 .. n-1
  pub fn router_tracks(r: Router)                     -> [string]
  pub fn bump_count(r: Router, name: string)          -> Router  // returns updated router
  pub fn count_for(r: Router, name: string)           -> int

Construction:
  make_router([TrackWeight{"A", 50}, TrackWeight{"B", 50}], 0)
  # 50/50 split, salt=0
  make_router([TrackWeight{"beta", 70}, TrackWeight{"alpha", 30}], 42)
  # weighted split, salt=42 so two deployments with different salts
  # get uncorrelated assignments even on identical keys.

Determinism:
  route(r, key) is a pure function of (tracks, salt, key). Same
  inputs ⇒ same output across runs / processes / substrates.

## Functions

| Function | Returns |
|---|---|
| [`make_router`](#fn-make-router) | `Router` |
| [`route_bucket`](#fn-route-bucket) | `int` |
| [`route`](#fn-route) | `string` |
| [`router_tracks`](#fn-router-tracks) | `array` |
| [`bump_count`](#fn-bump-count) | `Router` |
| [`count_for`](#fn-count-for) | `int` |

### <a id="fn-make-router"></a>`fn make_router`

```hexa
pub fn make_router(tracks: [TrackWeight], salt: int) -> Router
```

**Parameters:** `tracks: [TrackWeight], salt: int`  
**Returns:** `Router`  

_No docstring._

### <a id="fn-route-bucket"></a>`fn route_bucket`

```hexa
pub fn route_bucket(r: Router, key: string, n: int) -> int
```

**Parameters:** `r: Router, key: string, n: int`  
**Returns:** `int`  

Bucket the hash into [0, n) using unsigned modular reduction.
xxh32 returns a 32-bit value already masked; n > 0 by caller contract.

### <a id="fn-route"></a>`fn route`

```hexa
pub fn route(r: Router, key: string) -> string
```

**Parameters:** `r: Router, key: string`  
**Returns:** `string`  

_No docstring._

### <a id="fn-router-tracks"></a>`fn router_tracks`

```hexa
pub fn router_tracks(r: Router) -> array
```

**Parameters:** `r: Router`  
**Returns:** `array`  

_No docstring._

### <a id="fn-bump-count"></a>`fn bump_count`

```hexa
pub fn bump_count(r: Router, name: string) -> Router
```

**Parameters:** `r: Router, name: string`  
**Returns:** `Router`  

_No docstring._

### <a id="fn-count-for"></a>`fn count_for`

```hexa
pub fn count_for(r: Router, name: string) -> int
```

**Parameters:** `r: Router, name: string`  
**Returns:** `int`  

_No docstring._

---

← [Back to stdlib index](README.md)

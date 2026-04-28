# RFC 002 — map.has() / map.get(key, default) / map.keys() stdlib methods

- **Status**: done (P1 m.has(key) landed 2026-04-28; m.get/m.keys/m.values already present from prior work)
- **Date**: 2026-04-28
- **Severity**: ergonomic (workaround exists but adds string-parsing overhead and brittleness)
- **Priority**: P1
- **Source convergence**: convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence (gap 2)
- **Landing**: self/hexa_full.hexa:16699 (interp dispatch) + self/codegen_c2.hexa:2534, 4082 (AOT codegen — 2 sites: gen2_method_builtin + gen2_expr fallback) + test/regression/map_has_method.hexa (5/5 PASS — empty / present / absent / empty-string-key / parity-with-contains_key — raw#71 falsifier verified). Total: ~14 LoC added.

## Problem

hexa map type does not expose key-existence check. `m.has(key)` fails with `unknown method .has() on map`. There is no `m.get(key, default)` for safe lookup, and no `m.keys()` to iterate.

Tools fall back to string-based key-value blob parsing (`_kv_lookup(body, key)`) — 16 string scans per tick where 16 `map.has()` calls would have sufficed.

Reproducer (anima-eeg earlier draft):
```hexa
let state = {"Fp1": "ok", "Fp2": "drift", ...}
if state.has("Fp1") { ... }   // ERROR: unknown method .has() on map
```

Current workaround (string-blob parsing):
```hexa
fun _kv_lookup(body: string, key: string) -> string {
    // scan body for "key=value\n" pattern, return value or ""
    // O(n) per lookup, 16 positions × per-tick = 16 scans
}
```

## Proposed API

```hexa
let m = {"a": 1, "b": 2}

if m.has("a") { ... }              // bool
let v = m.get("a", default_val)    // value or default
let keys = m.keys()                // iterable
let vals = m.values()              // iterable

for k in m.keys() {
    use(m.get(k, ""))
}
```

## Compatibility

Strictly additive. Method-resolution table grows by 4 entries (`has`, `get`, `keys`, `values`). No existing semantic changes.

## Implementation Scope

- **NEW or extend**: `self/stdlib/map.hexa` (~50 LoC)
  - Declare `.has(key) -> bool`, `.get(key, default) -> V`, `.keys() -> Iter<K>`, `.values() -> Iter<V>` as standard map methods
- **Method-resolution wiring** in `self/main.hexa` (~10 LoC)
  - Ensure map dispatches `.has/.get/.keys/.values` to stdlib intrinsics under both interpreter and AOT
- **Regression test** (interpreter + AOT dual-run): `test/regression/map_methods.hexa` (~20 LoC)

## Falsifier (raw#71)

INVALIDATED iff:
1. `m.has(k)` returns boolean equal to `k in keys(m)` for all (k, m) pairs in a 1k-pair fuzzer
2. `m.get(k, def)` returns `m[k]` when present else `def`
3. Both behaviors identical in interpreter AND AOT modes

**Anti-falsifier**: any divergence between interpreter and AOT semantics, or any case where `.has()` throws instead of returning bool, fails the proposal.

## Effort Estimate

- LoC: ~50 (stdlib) + ~10 (dispatch) + ~20 (test) = **~80 LoC**
- Hours: **3-4h**

## Retire Conditions

- Falsifier passes → status `done`
- Map type retired in favor of structural records → status `superseded`

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-g2-map-has-get-keys`

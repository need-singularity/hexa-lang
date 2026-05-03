# `json`

_JSON write-side helpers (P1 — qmirror calibration cache)_

**Source:** [`stdlib/json.hexa`](../../stdlib/json.hexa)  
**Module slug:** `stdlib-json`  
**Description:** write-side JSON helpers built on the runtime json_stringify builtin  

**Usage:**

```hexa
import stdlib/json; let s = json_dump_pretty(cache_map, 2)
```

## Overview

Source: anima/docs/hexa_lang_attr_review_for_qmirror_2026_05_03.md §5.3

PROBLEM (qmirror calibration cache writeback)
  nexus/modules/{calibration_v2,engine_aer}.hexa need to serialize a
  nested map (counts, ρ matrix slices, per-shot timings) to disk with
  stable, diff-friendly formatting. The runtime `json_stringify` builtin
  produces compact one-line output — fine for transport, awkward for
  git-diffable cache files. Sisters were hand-concatenating
  `"{\"key\": " + ... + "}"` strings (escape-fragile).

FIX (additive, no runtime.c change)
  1. json_stringify_value(v) -> string
       Stable alias over the runtime `json_stringify` builtin so callers
       that `use "stdlib/json"` get a guaranteed symbol path. Pure
       delegation — no behavioural change vs builtin.
  2. json_dump_pretty(v, indent) -> string
       Recursive pretty-printer. Indent = number of spaces per level
       (0 = compact, 2 = canonical cache format).
  3. json_object_set(obj, key, value) -> map
       In-place set (via index assignment) returning the same map for
       chaining. Mirrors hexa map convention without forcing callers to
       learn the `obj[key] = v` syntax inside expression contexts.
  4. json_array_push(arr, value) -> array
       Same idea for arrays — return-self push.

Breaking risk: 0. All new symbol names; the runtime `json_stringify`
builtin is untouched and remains the canonical compact encoder.
stdlib/json_object.hexa (read side) is unchanged.

Caveats (raw#91):
  · json_dump_pretty escaping is the runtime builtin's escape policy
    applied per-leaf — minimal `\\`, `\"`, `\n` only. Calibration cache
    payload is ASCII-numeric, so this is adequate. Arbitrary user input
    direct-serialize is NOT safe (UTF-16 surrogate pair, control chars
    not handled). See attr review §6 caveat 7.
  · The pretty-printer parses-then-re-emits via json_stringify on each
    leaf — O(N · depth) string concat, not the optimal single-pass
    emitter. For typical calibration files (<100KB) this is negligible.
  · json_object_set/json_array_push mutate the caller's map/array in
    place. The return value is the same reference — useful for
    chaining, NOT a copy. Callers mutating shared state should clone
    first via json_parse(json_stringify(orig)).

raw#9 hexa-only-strict: pure hexa wrapper, zero exec(), zero new C
  builtin. The underlying serializer is the existing runtime
  `json_stringify` already exercised by 37+ callsites (see runtime.c
  line 6669 inventory).

── json_stringify_value — runtime builtin alias ─────────────────

Re-exports the runtime json_stringify under a path-stable name. Callers
that `use "stdlib/json"` can rely on this symbol surviving any future
runtime rename. Pure passthrough — no allocation, no escape changes.

## Functions

| Function | Returns |
|---|---|
| [`json_stringify_value`](#fn-json-stringify-value) | `string` |
| [`json_dump_pretty`](#fn-json-dump-pretty) | `string` |
| [`json_object_set`](#fn-json-object-set) | `_inferred_` |
| [`json_array_push`](#fn-json-array-push) | `_inferred_` |

### <a id="fn-json-stringify-value"></a>`fn json_stringify_value`

```hexa
pub fn json_stringify_value(v) -> string
```

**Parameters:** `v`  
**Returns:** `string`  

_No docstring._

### <a id="fn-json-dump-pretty"></a>`fn json_dump_pretty`

```hexa
pub fn json_dump_pretty(v, indent: int) -> string
```

**Parameters:** `v, indent: int`  
**Returns:** `string`  

── json_dump_pretty — recursive pretty-printer ──────────────────

Emits `v` as JSON with `indent` spaces per nesting level. indent=0
degenerates to compact mode (delegates to runtime builtin one-shot).

Type dispatch uses runtime type_of() — identical to the builtin's own
dispatch table. Unknown types fall through to the builtin which emits
"null" or a quoted string representation depending on runtime policy.

### <a id="fn-json-object-set"></a>`fn json_object_set`

```hexa
pub fn json_object_set(obj, key: string, value)
```

**Parameters:** `obj, key: string, value`  
**Returns:** _inferred_  

── json_object_set / json_array_push — in-place builders ────────

Conventional hexa style is `m[k] = v` and `arr.push(v)`. These
helpers exist for two ergonomic reasons:
  1. Chainable: `json_object_set(json_object_set(m, "a", 1), "b", 2)`
     builds a map in expression context (no statement chain needed).
  2. Symbol stability: callers using `use "stdlib/json"` get a
     guaranteed-named entry point that survives map literal syntax
     churn (the language has had three map literal forms — `{}`,
     `#{}`, `json_parse("{}")` — over its lifetime).

Return value is the SAME map/array reference (mutated in place).

### <a id="fn-json-array-push"></a>`fn json_array_push`

```hexa
pub fn json_array_push(arr, value)
```

**Parameters:** `arr, value`  
**Returns:** _inferred_  

_No docstring._

---

← [Back to stdlib index](README.md)

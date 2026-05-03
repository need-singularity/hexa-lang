# `json_object`

_json_object_

**Source:** [`stdlib/json_object.hexa`](../../stdlib/json_object.hexa)  

## Overview

Sister-repo unblock (hxa-20260424-004, cross_repo_blocker prio=95):
  anima / nexus / airgenome 등이 JSON object traverse 에
  수동 line-scan (parse_entry_blocks, str_idx, substring 조합)
  workaround 를 500+ LOC 로 중복 유지 중.
  nexus/tool/proposal_archive.hexa 의 `parse_entry_blocks` 구현 (L114-180) 이
  대표 예시 — `"entries":` 를 문자열로 찾고 indent-기반 brace depth 를
  수동 추적.

Root cause — runtime.c 의 hexa_json_parse 는 이미 object 를
  TAG_MAP 으로 반환하지만 stdlib wrapper 가 없어서 caller 마다
  "field 가 있나? 없나? nested path 는?" 를 재발명.

This file — no new C primitive. 기존 runtime:
  - json_parse(text) -> map | array | void
  - map_keys(m) / has_key(m, k)
  - type_of(v) -> "map" | "array" | "string" | "int" | "float" | "bool" | "void"
  - m[key] direct index
  만 조합하여 path-style nested access + 안전한 entries iteration 제공.

No runtime.c change → no tier2 ceremony required.
No new C builtin → no codegen_c2 dispatch update needed.

Usage:
  let obj = json_parse_object(text)          // map-only, non-object → empty map
  let entries = json_object_entries(obj)     // [[k,v], ...]
  let v = json_object_get(obj, "k")          // v | void
  let v = json_object_get_path(obj, "a.b.c") // v | void
  let s = json_object_get_str(obj, "k", "default")
  let n = json_object_get_int(obj, "k", 0)
  let xs = json_object_get_array(obj, "k")   // array | []

── basic guards ────────────────────────────────────────────────────────────

Parse text, fall back to an empty map if result is not a JSON object.
Sisters pass arbitrary text (file contents possibly mixed with BOMs or
leading whitespace); json_parse already handles whitespace, we only
enforce object-shape so downstream map_keys is safe.

## Functions

| Function | Returns |
|---|---|
| [`json_parse_object`](#fn-json-parse-object) | `_inferred_` |
| [`json_parse_array`](#fn-json-parse-array) | `_inferred_` |
| [`is_json_object`](#fn-is-json-object) | `bool` |
| [`is_json_array`](#fn-is-json-array) | `bool` |
| [`json_object_entries`](#fn-json-object-entries) | `_inferred_` |
| [`json_object_keys`](#fn-json-object-keys) | `_inferred_` |
| [`json_object_get`](#fn-json-object-get) | `_inferred_` |
| [`json_object_get_path`](#fn-json-object-get-path) | `_inferred_` |
| [`json_object_has_path`](#fn-json-object-has-path) | `bool` |
| [`json_object_get_str`](#fn-json-object-get-str) | `string` |
| [`json_object_get_int`](#fn-json-object-get-int) | `i64` |
| [`json_object_get_float`](#fn-json-object-get-float) | `f64` |
| [`json_object_get_bool`](#fn-json-object-get-bool) | `bool` |
| [`json_object_get_array`](#fn-json-object-get-array) | `_inferred_` |
| [`json_object_get_object`](#fn-json-object-get-object) | `_inferred_` |
| [`json_object_array_pluck`](#fn-json-object-array-pluck) | `_inferred_` |
| [`json_object_array_find`](#fn-json-object-array-find) | `_inferred_` |
| [`json_object_array_filter`](#fn-json-object-array-filter) | `_inferred_` |

### <a id="fn-json-parse-object"></a>`fn json_parse_object`

```hexa
pub fn json_parse_object(text)
```

**Parameters:** `text`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-json-parse-array"></a>`fn json_parse_array`

```hexa
pub fn json_parse_array(text)
```

**Parameters:** `text`  
**Returns:** _inferred_  

Parse text, fall back to empty array if result is not a JSON array.

### <a id="fn-is-json-object"></a>`fn is_json_object`

```hexa
pub fn is_json_object(v) -> bool
```

**Parameters:** `v`  
**Returns:** `bool`  

Return true iff v is a JSON object (hexa map).

### <a id="fn-is-json-array"></a>`fn is_json_array`

```hexa
pub fn is_json_array(v) -> bool
```

**Parameters:** `v`  
**Returns:** `bool`  

Return true iff v is a JSON array.

### <a id="fn-json-object-entries"></a>`fn json_object_entries`

```hexa
pub fn json_object_entries(obj)
```

**Parameters:** `obj`  
**Returns:** _inferred_  

── entries / keys / values (safe variants) ─────────────────────────────────

Iterate entries as an array of [key, value] pairs. Non-map → [].
Mirrors map.entries() but defensive against arrays / scalars passed in.

### <a id="fn-json-object-keys"></a>`fn json_object_keys`

```hexa
pub fn json_object_keys(obj)
```

**Parameters:** `obj`  
**Returns:** _inferred_  

Keys as str array. Non-map → [].

### <a id="fn-json-object-get"></a>`fn json_object_get`

```hexa
pub fn json_object_get(obj, key: string)
```

**Parameters:** `obj, key: string`  
**Returns:** _inferred_  

── lookup ──────────────────────────────────────────────────────────────────

Look up a single key. Returns void on missing / non-map.
Sisters check `has_key + index` separately; centralize.

### <a id="fn-json-object-get-path"></a>`fn json_object_get_path`

```hexa
pub fn json_object_get_path(obj, path: string)
```

**Parameters:** `obj, path: string`  
**Returns:** _inferred_  

Look up with dotted path. "a.b.c" walks obj["a"]["b"]["c"].
Missing segment anywhere → void. Non-map at any intermediate → void.
Does NOT split on "." inside key values — if your key literally contains
a dot, use json_object_get directly.

### <a id="fn-json-object-has-path"></a>`fn json_object_has_path`

```hexa
pub fn json_object_has_path(obj, path: string) -> bool
```

**Parameters:** `obj, path: string`  
**Returns:** `bool`  

Path exists (non-void terminal).

### <a id="fn-json-object-get-str"></a>`fn json_object_get_str`

```hexa
pub fn json_object_get_str(obj, key: string, default_str: string) -> string
```

**Parameters:** `obj, key: string, default_str: string`  
**Returns:** `string`  

── typed getters with defaults ─────────────────────────────────────────────
Sisters repeatedly wrap `has_key + to_string + fallback` inline. Provide
typed defaults so caller never produces (void | wrong-type) downstream.

### <a id="fn-json-object-get-int"></a>`fn json_object_get_int`

```hexa
pub fn json_object_get_int(obj, key: string, default_int: i64) -> i64
```

**Parameters:** `obj, key: string, default_int: i64`  
**Returns:** `i64`  

_No docstring._

### <a id="fn-json-object-get-float"></a>`fn json_object_get_float`

```hexa
pub fn json_object_get_float(obj, key: string, default_float: f64) -> f64
```

**Parameters:** `obj, key: string, default_float: f64`  
**Returns:** `f64`  

_No docstring._

### <a id="fn-json-object-get-bool"></a>`fn json_object_get_bool`

```hexa
pub fn json_object_get_bool(obj, key: string, default_bool: bool) -> bool
```

**Parameters:** `obj, key: string, default_bool: bool`  
**Returns:** `bool`  

_No docstring._

### <a id="fn-json-object-get-array"></a>`fn json_object_get_array`

```hexa
pub fn json_object_get_array(obj, key: string)
```

**Parameters:** `obj, key: string`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-json-object-get-object"></a>`fn json_object_get_object`

```hexa
pub fn json_object_get_object(obj, key: string)
```

**Parameters:** `obj, key: string`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-json-object-array-pluck"></a>`fn json_object_array_pluck`

```hexa
pub fn json_object_array_pluck(arr, key: string)
```

**Parameters:** `arr, key: string`  
**Returns:** _inferred_  

── convenience: iterate array-of-objects ───────────────────────────────────
inventory.json 류 — top-level object, `entries` 가 array of objects.
sisters 이 가장 자주 하는 일: `entries[]` 를 뽑고 각 entry 의 `id` / `status`
를 읽기. 그 패턴을 primitive 화.

Pluck a field from each object in an array-of-objects.
arr[i] non-map or missing key → skipped (not emitted as void).

### <a id="fn-json-object-array-find"></a>`fn json_object_array_find`

```hexa
pub fn json_object_array_find(arr, key: string, value)
```

**Parameters:** `arr, key: string, value`  
**Returns:** _inferred_  

Find first entry in array-of-objects where entry[key] equals value.
Comparison via to_string to side-step int/str key-type mismatches.
void on no match.

### <a id="fn-json-object-array-filter"></a>`fn json_object_array_filter`

```hexa
pub fn json_object_array_filter(arr, key: string, value)
```

**Parameters:** `arr, key: string, value`  
**Returns:** _inferred_  

Filter array-of-objects where entry[key] equals value (string compare).

---

← [Back to stdlib index](README.md)

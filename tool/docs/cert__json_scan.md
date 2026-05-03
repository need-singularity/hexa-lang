# `cert/json_scan`

_minimal JSON field scanners (interp-safe)_

**Source:** [`stdlib/cert/json_scan.hexa`](../../stdlib/cert/json_scan.hexa)  

## Overview

 stdlib/cert/json_scan.hexa — minimal JSON field scanners (interp-safe)

 These are byte-level string scanners — NOT a full JSON parser. They
 mirror the exact loop structure used in anima/tool/cert_gate.hexa so
 that lifting the logic into stdlib does not change semantics.

 Convention:
   * json_str_field_last — cert bodies embed `{"type":"verdict",
     "value":"…"}` records earlier in the document; the outermost top-
     level  "verdict": "<X>"  always trails. LAST-wins preserves that
     ordering contract.
   * json_bool_field_first — status/pass-style booleans appear at most
     once at stable positions; FIRST-wins is canonical.

Find the LAST  "<key>": "<value>"  pair in `body` and return <value>.
Returns "" if not found.

## Functions

| Function | Returns |
|---|---|
| [`json_str_field_last`](#fn-json-str-field-last) | `_inferred_` |
| [`json_bool_field_first`](#fn-json-bool-field-first) | `_inferred_` |

### <a id="fn-json-str-field-last"></a>`fn json_str_field_last`

```hexa
pub fn json_str_field_last(body, key)
```

**Parameters:** `body, key`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-json-bool-field-first"></a>`fn json_bool_field_first`

```hexa
pub fn json_bool_field_first(body, key)
```

**Parameters:** `body, key`  
**Returns:** _inferred_  

Find the FIRST  "<key>": <bool>  pair. Returns 1 for true, 0 for false,
-1 if the key is missing or its value is not a bool literal.

---

← [Back to stdlib index](README.md)

# `cert/writer`

_canonical raw#10 proof-carrying JSON writer_

**Source:** [`stdlib/cert/writer.hexa`](../../stdlib/cert/writer.hexa)  

## Overview

 stdlib/cert/writer.hexa — canonical raw#10 proof-carrying JSON writer

 Output schema (stdlib.cert.v1):
     "schema":            "<caller-supplied tag>",
     "tool":              "stdlib/cert",
     "created":           "<UTC ISO-8601>",
     "verdict":           "PASSED" | "REJECTED",
     "reward_mult":       <float>,
     "core_sat":          <float>,
     "aux_sat":           <float>,
     "selftest_pass":     <bool>,
     "deterministic_sha": "<sha256 of the canonical payload prefix>",
     "signature":         "",                        (ed25519 TBD)
     "extra":             "<caller-supplied>"

## Functions

| Function | Returns |
|---|---|
| [`write_result`](#fn-write-result) | `_inferred_` |

### <a id="fn-write-result"></a>`fn write_result`

```hexa
pub fn write_result(path, schema, gate, selftest_pass, extra)
```

**Parameters:** `path, schema, gate, selftest_pass, extra`  
**Returns:** _inferred_  

Write a canonical raw#10 JSON result. `extra` is a caller-supplied
string (usually "" or a pre-rendered JSON fragment) appended into the
"extra" field verbatim.

---

← [Back to stdlib index](README.md)

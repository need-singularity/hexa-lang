# `cert/meta2_schema`

_Meta^2 breakthrough certificate schema_

**Source:** [`stdlib/cert/meta2_schema.hexa`](../../stdlib/cert/meta2_schema.hexa)  

## Overview

 stdlib/cert/meta2_schema.hexa — Meta^2 breakthrough certificate schema

 PURPOSE
   Schema constants + record constructors for Meta^2 certificates
   (anima/.meta2-cert/*.json). Stage0-compatible record strategy: dict
   literals (#{}) instead of named-field structs so the polymorphic
   evidence[] entries can round-trip losslessly.

 WHY DICT-RECORDS (vs named-field struct):
   Evidence entries carry heterogeneous shapes per `kind`:
     {"type":"phase_jump","K":4,"verdict":"PHASE_JUMP"}
     {"type":"kkt","target_value":0,"actual":0,"tolerance":1}
     {"type":"artifact_count","count":3}
     {"type":"jsd","target_value":1.0,"actual":1.0,"tolerance":0}
   A flat struct with fixed fields would either drop extras or force a
   sum-type (interp has none). Dicts preserve every key.

 RECORD CONSTRUCTORS (pure factory funcs; readers just use dict access)
   new_evidence()         → evidence record (kind, value, target_value,
                            actual, tolerance, ratio, count, total, extras,
                            _raw_numeric)
   new_selftest_triple()  → triplet record
   new_exit_criteria()    → exit criteria record
   new_trigger()          → trigger record
   new_cert()             → cert record (all v2 fields, populated defaults)
   new_chain()            → chain record (index.json mirror)
   new_chain_entry()      → chain entry record

 ENUM WHITELISTS
   verdict : VERIFIED | PASS | PARTIAL | HELD | CONDITIONAL | FAIL
   status  : synthetic_proof | natural_observed | conjecture | HELD

 NUMERIC-TOKEN PRESERVATION
   Every numeric JSON token is parsed BOTH as a native value (for compute)
   AND stored as a source-string token in the record's "_raw_numeric" map
   under the JSON key path. The emitter writes the raw token verbatim
   when present, preventing 1.60329e-16 → 1.60329e-16 drift or
   0.0000001 → 1e-07 drift.

 CONSTRAINTS
   raw#9  hexa-only (no external deps)
   raw#10 proof-carrying
   raw#11 snake_case
   raw#15 no-hardcode (schema version is the only constant)

── Schema version ──────────────────────────────────────────────────────────

## Functions

| Function | Returns |
|---|---|
| [`is_accepted_schema_version`](#fn-is-accepted-schema-version) | `bool` |
| [`verdict_whitelist`](#fn-verdict-whitelist) | `_inferred_` |
| [`status_whitelist`](#fn-status-whitelist) | `_inferred_` |
| [`is_valid_verdict`](#fn-is-valid-verdict) | `bool` |
| [`is_valid_status`](#fn-is-valid-status) | `bool` |
| [`new_evidence`](#fn-new-evidence) | `_inferred_` |
| [`new_selftest_triple`](#fn-new-selftest-triple) | `_inferred_` |
| [`new_exit_criteria`](#fn-new-exit-criteria) | `_inferred_` |
| [`new_trigger`](#fn-new-trigger) | `_inferred_` |
| [`new_cert`](#fn-new-cert) | `_inferred_` |
| [`new_chain`](#fn-new-chain) | `_inferred_` |
| [`new_chain_entry`](#fn-new-chain-entry) | `_inferred_` |
| [`new_error`](#fn-new-error) | `_inferred_` |

### <a id="fn-is-accepted-schema-version"></a>`fn is_accepted_schema_version`

```hexa
pub fn is_accepted_schema_version(v: string) -> bool
```

**Parameters:** `v: string`  
**Returns:** `bool`  

Accepted historical schema versions on input ("" defaults to v1).

### <a id="fn-verdict-whitelist"></a>`fn verdict_whitelist`

```hexa
pub fn verdict_whitelist()
```

**Parameters:** _none_  
**Returns:** _inferred_  

── Enum whitelists ─────────────────────────────────────────────────────────

### <a id="fn-status-whitelist"></a>`fn status_whitelist`

```hexa
pub fn status_whitelist()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-is-valid-verdict"></a>`fn is_valid_verdict`

```hexa
pub fn is_valid_verdict(v: string) -> bool
```

**Parameters:** `v: string`  
**Returns:** `bool`  

_No docstring._

### <a id="fn-is-valid-status"></a>`fn is_valid_status`

```hexa
pub fn is_valid_status(s: string) -> bool
```

**Parameters:** `s: string`  
**Returns:** `bool`  

_No docstring._

### <a id="fn-new-evidence"></a>`fn new_evidence`

```hexa
pub fn new_evidence()
```

**Parameters:** _none_  
**Returns:** _inferred_  

── Record constructors ─────────────────────────────────────────────────────
All records are plain dicts. A sentinel "_record" key tags the shape so
round-trip code can audit what it holds without type system support.

### <a id="fn-new-selftest-triple"></a>`fn new_selftest_triple`

```hexa
pub fn new_selftest_triple()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-new-exit-criteria"></a>`fn new_exit_criteria`

```hexa
pub fn new_exit_criteria()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-new-trigger"></a>`fn new_trigger`

```hexa
pub fn new_trigger()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-new-cert"></a>`fn new_cert`

```hexa
pub fn new_cert()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-new-chain"></a>`fn new_chain`

```hexa
pub fn new_chain()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-new-chain-entry"></a>`fn new_chain_entry`

```hexa
pub fn new_chain_entry()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-new-error"></a>`fn new_error`

```hexa
pub fn new_error(path: string, field: string, rule: string, message: string)
```

**Parameters:** `path: string, field: string, rule: string, message: string`  
**Returns:** _inferred_  

── Error record helper (shared by validator) ──────────────────────────────

---

← [Back to stdlib index](README.md)

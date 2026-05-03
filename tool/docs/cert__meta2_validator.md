# `cert/meta2_validator`

_Meta^2 cert parser + validator + emitter_

**Source:** [`stdlib/cert/meta2_validator.hexa`](../../stdlib/cert/meta2_validator.hexa)  

## Overview

 stdlib/cert/meta2_validator.hexa — Meta^2 cert parser + validator + emitter

 ENTRY POINTS
   parse_cert(path)                     → Cert record  (file path)
   parse_cert_from_string(body)         → Cert record  (in-memory JSON)
   parse_chain(path)                    → Chain record
   validate(cert)                       → list<Error>  ([] = OK)
   validate_chain(chain)                → list<Error>
   resolve_crosslinks(cert, dir)        → list<string> slugs missing
   walk_chain(chain, dir)               → list<Error>  (sha-chain audit)
   emit_cert(cert)                      → canonical JSON string
   emit_chain(chain)                    → canonical JSON string
   canonical_sha256(cert)               → sha256 hex of emit_cert(cert)

 CANONICAL JSON RULES (byte-identity critical)
   - UTF-8, LF only.
   - Top-level keys sorted lexicographically.
   - Arrays preserve source order.
   - Numbers preserved as source-string tokens via _raw_numeric shadow.
     When a field has a raw token in _raw_numeric, the emitter writes it
     verbatim; otherwise it serializes the native value.
   - No trailing whitespace; exactly one terminating LF.

 V1 BACKWARDS COMPAT
   - schema_version ∈ {"", "1", "2"} accepted on input; missing → "1".
   - Evidence "type" key aliased as "kind".
   - Emitter always writes schema_version="2".

 PARSER
   Hand-rolled JSON recursive descent (interp has no json stdlib). Tracks
   source positions to capture raw numeric tokens exactly as written.

## Functions

| Function | Returns |
|---|---|
| [`parse_cert_from_string`](#fn-parse-cert-from-string) | `_inferred_` |
| [`parse_cert`](#fn-parse-cert) | `_inferred_` |
| [`parse_chain`](#fn-parse-chain) | `_inferred_` |
| [`validate`](#fn-validate) | `_inferred_` |
| [`validate_chain`](#fn-validate-chain) | `_inferred_` |
| [`resolve_crosslinks`](#fn-resolve-crosslinks) | `_inferred_` |
| [`walk_chain`](#fn-walk-chain) | `_inferred_` |
| [`emit_cert`](#fn-emit-cert) | `string` |
| [`emit_chain`](#fn-emit-chain) | `string` |
| [`canonical_sha256`](#fn-canonical-sha256) | `string` |

### <a id="fn-parse-cert-from-string"></a>`fn parse_cert_from_string`

```hexa
pub fn parse_cert_from_string(body: string)
```

**Parameters:** `body: string`  
**Returns:** _inferred_  

── Top-level parse_cert ───────────────────────────────────────────────────

### <a id="fn-parse-cert"></a>`fn parse_cert`

```hexa
pub fn parse_cert(path: string)
```

**Parameters:** `path: string`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-parse-chain"></a>`fn parse_chain`

```hexa
pub fn parse_chain(path: string)
```

**Parameters:** `path: string`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-validate"></a>`fn validate`

```hexa
pub fn validate(cert)
```

**Parameters:** `cert`  
**Returns:** _inferred_  

═══════════════════════════════════════════════════════════════════════════
 Validation
═══════════════════════════════════════════════════════════════════════════

### <a id="fn-validate-chain"></a>`fn validate_chain`

```hexa
pub fn validate_chain(chain)
```

**Parameters:** `chain`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-resolve-crosslinks"></a>`fn resolve_crosslinks`

```hexa
pub fn resolve_crosslinks(cert, dir: string)
```

**Parameters:** `cert, dir: string`  
**Returns:** _inferred_  

Cross-link resolver: for every slug in cert["cross_refs"], check that
<dir>/<slug>.json exists. Slug whitelist is case-sensitive — some certs
use uppercase symbolic refs like "UNIVERSAL_CONSTANT_4" which map to
lowercase slug filenames via simple lowercase + dash substitution.

### <a id="fn-walk-chain"></a>`fn walk_chain`

```hexa
pub fn walk_chain(chain, dir: string)
```

**Parameters:** `chain, dir: string`  
**Returns:** _inferred_  

Walk the chain: verify each entry's file sha256, confirm prev_index_sha
points back to the previous entry or GENESIS.

### <a id="fn-emit-cert"></a>`fn emit_cert`

```hexa
pub fn emit_cert(cert) -> string
```

**Parameters:** `cert`  
**Returns:** `string`  

Canonical cert emitter.

### <a id="fn-emit-chain"></a>`fn emit_chain`

```hexa
pub fn emit_chain(chain) -> string
```

**Parameters:** `chain`  
**Returns:** `string`  

_No docstring._

### <a id="fn-canonical-sha256"></a>`fn canonical_sha256`

```hexa
pub fn canonical_sha256(cert) -> string
```

**Parameters:** `cert`  
**Returns:** `string`  

── canonical_sha256: shell out to shasum -a 256 ───────────────────────────

---

← [Back to stdlib index](README.md)

# `ckpt/verifier`

_load-time tamper-aware .pcc verification_

**Source:** [`stdlib/ckpt/verifier.hexa`](../../stdlib/ckpt/verifier.hexa)  

## Overview

 stdlib/ckpt/verifier.hexa — load-time tamper-aware .pcc verification

 PUBLIC API
   verify_pcc(path) -> dict
     path    : .pcc file
     returns : dict {
         "ok": bool, "code": str, "reason": str,
         "cert_json": str (when parseable; "" otherwise),
         "payload": array<int> (when ok; [] otherwise),
         "merkle_root": [hi, lo],
         "chunk_count": int, "payload_len": int

 FIVE TAMPER CODES — DISTINCT BY DESIGN
     ERR_MAGIC_MISMATCH     MAGIC differs at offset 0 or in trailer
     ERR_HEADER_CORRUPT     header fields run past file size, version
                            unknown, or chunk_size * chunk_count < payload_len
     ERR_CHUNK_HASH_MISMATCH  re-hashed chunk ≠ directory xxh64
     ERR_MERKLE_MISMATCH    chunk hashes match but reconstructed root
                            ≠ trailer's declared merkle_root
     ERR_CERT_SIG_INVALID   CERT block fails to parse OR validate OR the
                            cert's verdict is not a PASS-class value
                            (delegates to stdlib/cert/meta2_validator).

 ORDERING
   The checks MUST execute in the order above: a tampered byte in one
   region should not cascade into a distant error code. For example, a
   flipped byte in the payload only manifests as chunk-hash mismatch
   even though the Merkle root derived from those leaves is also
   "wrong" — we never get that far because chunk hashing fails first.

## Functions

| Function | Returns |
|---|---|
| [`verify_pcc`](#fn-verify-pcc) | `_inferred_` |

### <a id="fn-verify-pcc"></a>`fn verify_pcc`

```hexa
pub fn verify_pcc(path: string)
```

**Parameters:** `path: string`  
**Returns:** _inferred_  

_No docstring._

---

← [Back to stdlib index](README.md)

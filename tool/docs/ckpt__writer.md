# `ckpt/writer`

_produce a .pcc (proof-carrying checkpoint) file_

**Source:** [`stdlib/ckpt/writer.hexa`](../../stdlib/ckpt/writer.hexa)  

## Overview

 stdlib/ckpt/writer.hexa — produce a .pcc (proof-carrying checkpoint) file

 PUBLIC API
   write_pcc(path, payload_bytes, cert_json, opts) -> dict
     path          : output file path
     payload_bytes : byte array (ints 0..255) — the raw checkpoint payload
     cert_json     : UTF-8 JSON string; embedded VERBATIM into CERT block
     opts          : dict of optional overrides
                       "chunk_size" : int (bytes; default 4 MiB)
                       "flags"      : int u16 (default 0)
     returns       : dict { "ok": bool, "path": str, "chunk_count": int,
                            "payload_len": int, "total_len": int,
                            "merkle_root": [hi, lo] }

 The caller is responsible for generating the cert JSON (e.g. via
 [`stdlib/cert/meta2_validator::emit_cert`](cert__meta2_validator.md#fn-emit-cert) or stdlib/cert/writer). This
 module makes no assumption about the signature scheme — the cert is
 the trust root, the Merkle tree merely binds it to the payload bytes.

 STAGE0 NOTES
   * u64 fields are [hi, lo] u32 pairs throughout. Payload lengths beyond
     2^31 are rare in our use cases (tests synthesize ~10 MiB) but the
     format header encodes a real u64, so future > 2 GiB ckpts don't
     need a schema bump.
   * We buffer the whole file in memory (single `write_bytes` call) —
     fine for tests and for current anima ckpts (few hundred MiB max).
     A streaming writer can be added later without schema change.

## Functions

| Function | Returns |
|---|---|
| [`write_pcc`](#fn-write-pcc) | `_inferred_` |

### <a id="fn-write-pcc"></a>`fn write_pcc`

```hexa
pub fn write_pcc(path: string, payload_bytes, cert_json: string, opts)
```

**Parameters:** `path: string, payload_bytes, cert_json: string, opts`  
**Returns:** _inferred_  

Emit a .pcc file. Returns result dict described in the module header.

---

← [Back to stdlib index](README.md)

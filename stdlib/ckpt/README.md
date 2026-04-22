# stdlib/ckpt — proof-carrying checkpoint (.pcc)

Roadmap 61 / B5 — HXA-#08. Binds a meta2 cert to a chunked payload via a
u64 xxh64 Merkle tree, so a ckpt load can self-audit before weights enter
memory.

## Layout (little-endian)

```
HEXAPCC1 | hdr(28B) | CERT(JSON bytes) | dir[N*{off,len,xxh}] | chunks | merkle_root u64 | HEXAPCC1 | total_len u64
```

- `hdr` = version u16, flags u16, chunk_size u32, chunk_count u64, cert_block_len u32, payload_len u64.
- Default chunk = 4 MiB. Merkle leaves = xxh64 of each chunk; odd levels duplicate-last.
- CERT block is opaque UTF-8 JSON — signing trust flows through `stdlib/cert/meta2_validator`.

## Public API

```
write_pcc(path, payload_bytes, cert_json, opts) -> dict
verify_pcc(path) -> dict { ok, code, reason, cert_json, payload, merkle_root, chunk_count, payload_len }
```

## Error codes (distinct by design)

| code | trigger |
|---|---|
| `ERR_MAGIC_MISMATCH` | HEAD or TAIL magic differ |
| `ERR_HEADER_CORRUPT` | unknown version, chunk×count contradicts payload_len, declared total ≠ file size |
| `ERR_CHUNK_HASH_MISMATCH` | recomputed xxh64 ≠ directory entry |
| `ERR_MERKLE_MISMATCH` | chunk hashes OK but root differs (dir-swap attack) |
| `ERR_CERT_SIG_INVALID` | CERT fails parse / validate / pass-class verdict |

## Tests

- `test/pcc_roundtrip.hexa` — multi-chunk write → verify, payload byte-identical.
- `test/pcc_tamper.hexa` — one-byte mutation per code, all five distinct.

Out of scope: ed25519 signing, compression, anima integration, legacy shim.

# `ckpt/chunk_store`

_ckpt/chunk_store_

**Source:** [`stdlib/ckpt/chunk_store.hexa`](../../stdlib/ckpt/chunk_store.hexa)  

## Overview

Content-addressable chunk dedup for checkpoint packing (B6).

Motivation: ckpt workflows (G36) repeatedly write near-identical byte
ranges — weight shards, activation caches, lineage blobs — saving
duplicates costs 18+ GiB in practice. This module wraps the
xxh32-keyed content-addressable pattern so any ckpt writer can call
`put_chunk` and get dedup "for free", with dedup_ratio reported as
a PerMille for `@strict_fp` compatibility (no float division).

xxh32 is used (not SHA-256) deliberately — this store serves local
dedup during a ckpt pack, not cross-host trust. pcc-verify (B5)
still owns cryptographic chunk hashing for the manifest.

Public API:
  pub fn make_store()                                -> ChunkStore
  pub fn put_chunk(store, bytes)                     -> string   (hex hash)
  pub fn get_chunk(store, hex)                       -> array    ([] if absent)
  pub fn has_chunk(store, hex)                       -> bool
  pub fn chunk_count(store)                          -> int
  pub fn total_input_bytes(store)                    -> int
  pub fn total_stored_bytes(store)                   -> int
  pub fn dedup_ratio(store)                          -> PerMille
  pub fn put_stream(store, bytes, chunk_size)        -> array<string>
  pub fn reassemble(store, hex_hashes)               -> array

## Functions

| Function | Returns |
|---|---|
| [`make_store`](#fn-make-store) | `ChunkStore` |
| [`put_chunk`](#fn-put-chunk) | `string` |
| [`get_chunk`](#fn-get-chunk) | `array` |
| [`has_chunk`](#fn-has-chunk) | `bool` |
| [`chunk_count`](#fn-chunk-count) | `int` |
| [`total_input_bytes`](#fn-total-input-bytes) | `int` |
| [`total_stored_bytes`](#fn-total-stored-bytes) | `int` |
| [`dedup_ratio`](#fn-dedup-ratio) | `PerMille` |
| [`put_stream`](#fn-put-stream) | `array` |
| [`reassemble`](#fn-reassemble) | `array` |

### <a id="fn-make-store"></a>`fn make_store`

```hexa
pub fn make_store() -> ChunkStore
```

**Parameters:** _none_  
**Returns:** `ChunkStore`  

_No docstring._

### <a id="fn-put-chunk"></a>`fn put_chunk`

```hexa
pub fn put_chunk(store: ChunkStore, bytes: array) -> string
```

**Parameters:** `store: ChunkStore, bytes: array`  
**Returns:** `string`  

Insert a chunk; returns the xxh32-hex content address. If the same
content was already stored, the store is left unchanged (dedup).

### <a id="fn-get-chunk"></a>`fn get_chunk`

```hexa
pub fn get_chunk(store: ChunkStore, hex: string) -> array
```

**Parameters:** `store: ChunkStore, hex: string`  
**Returns:** `array`  

_No docstring._

### <a id="fn-has-chunk"></a>`fn has_chunk`

```hexa
pub fn has_chunk(store: ChunkStore, hex: string) -> bool
```

**Parameters:** `store: ChunkStore, hex: string`  
**Returns:** `bool`  

_No docstring._

### <a id="fn-chunk-count"></a>`fn chunk_count`

```hexa
pub fn chunk_count(store: ChunkStore) -> int
```

**Parameters:** `store: ChunkStore`  
**Returns:** `int`  

_No docstring._

### <a id="fn-total-input-bytes"></a>`fn total_input_bytes`

```hexa
pub fn total_input_bytes(store: ChunkStore) -> int
```

**Parameters:** `store: ChunkStore`  
**Returns:** `int`  

_No docstring._

### <a id="fn-total-stored-bytes"></a>`fn total_stored_bytes`

```hexa
pub fn total_stored_bytes(store: ChunkStore) -> int
```

**Parameters:** `store: ChunkStore`  
**Returns:** `int`  

_No docstring._

### <a id="fn-dedup-ratio"></a>`fn dedup_ratio`

```hexa
pub fn dedup_ratio(store: ChunkStore) -> PerMille
```

**Parameters:** `store: ChunkStore`  
**Returns:** `PerMille`  

Dedup ratio as a PerMille (scale=1000) — no float ops, strict_fp-safe.
  0   ‰  → no dedup savings (unique input)
  500 ‰  → half of input bytes were duplicates
 1000 ‰  → all bytes duplicated (degenerate)

### <a id="fn-put-stream"></a>`fn put_stream`

```hexa
pub fn put_stream(store: ChunkStore, bytes: array, chunk_size: int) -> array
```

**Parameters:** `store: ChunkStore, bytes: array, chunk_size: int`  
**Returns:** `array`  

Split `bytes` into fixed-size chunks (last chunk may be smaller),
store each via put_chunk, return the hash sequence in-order. The
sequence is enough to recover the original stream byte-for-byte.

### <a id="fn-reassemble"></a>`fn reassemble`

```hexa
pub fn reassemble(store: ChunkStore, hex_hashes: array) -> array
```

**Parameters:** `store: ChunkStore, hex_hashes: array`  
**Returns:** `array`  

_No docstring._

---

← [Back to stdlib index](README.md)

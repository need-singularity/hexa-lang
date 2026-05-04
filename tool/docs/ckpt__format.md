# `ckpt/format`

_.pcc proof-carrying checkpoint on-disk layout_

**Source:** [`stdlib/ckpt/format.hexa`](../../stdlib/ckpt/format.hexa)  

## Overview

 stdlib/ckpt/format.hexa — .pcc proof-carrying checkpoint on-disk layout

 All multi-byte fields are LITTLE-ENDIAN. This is chosen because all our
 target substrates (x86_64 linux, aarch64 darwin/linux) are native LE, so
 encoded buffers can be mmap'd without byte-swap in a future C reader.

 FILE LAYOUT
   [0]      MAGIC_HEAD  = "HEXAPCC1"               (8 bytes)
   [8]      version     : u16                      (2 bytes)
   [10]     flags       : u16                      (2 bytes)
   [12]     chunk_size  : u32                      (4 bytes)
   [16]     chunk_count : u64                      (8 bytes)
   [24]     cert_block_len : u32                   (4 bytes)
   [28]     payload_len : u64                      (8 bytes)
   [36]     cert_block  : <cert_block_len bytes>
   [36+cbl] chunk_dir   : chunk_count × { u64 offset, u32 len, u64 xxh64 }
                          (20 bytes per entry)
   [...]    payload chunks at declared offsets (interleaved is allowed)
   [...]    merkle_root : u64                      (8 bytes)
   [...]    MAGIC_TAIL  = "HEXAPCC1"               (8 bytes)
   [...]    total_len   : u64                      (8 bytes)

 SIZING
   HEADER_SIZE     = 36  (MAGIC + fixed header)
   DIR_ENTRY_SIZE  = 20
   TRAILER_SIZE    = 24  (merkle_root + MAGIC_TAIL + total_len)
     → we treat merkle_root as *part of the trailer region* even though
       the design doc describes it as a separate "merkle section". Single
       u64 root → no separate node array. If a v2 format ever wants full
       proof path audit, it should bump `version` and extend here.

 VERSION / FLAGS
   Current version = 1. Unknown version → ERR_HEADER_CORRUPT.
   Flags bit 0 reserved for future "payload compressed" signal (not set
   in v1 — compression is explicitly out-of-scope per roadmap 61).

 CHUNKING
   Default chunk_size = 4 * 1024 * 1024 (4 MiB). Last chunk may be short.
   chunks MUST be packed consecutively in write order, but the format
   does not enforce contiguity at read time — the directory is the
   ground truth for where each chunk lives.

 ENDIANNESS HELPERS
   Shared LE pack/unpack for u16, u32, u64. u64s stored as [hi, lo] pairs
   matching stdlib/hash/xxhash.hexa conventions (interp int clamps to
   i63, so we cannot hold u64 in a single int). Callers pass u64s as
   two-element arrays [hi, lo] where each half is a u32 int.

── Constants ──────────────────────────────────────────────────────────────

## Functions

| Function | Returns |
|---|---|
| [`pcc_magic_bytes`](#fn-pcc-magic-bytes) | `_inferred_` |
| [`enc_u16_le`](#fn-enc-u16-le) | `_inferred_` |
| [`enc_u32_le`](#fn-enc-u32-le) | `_inferred_` |
| [`enc_u64_le`](#fn-enc-u64-le) | `_inferred_` |
| [`dec_u16_le`](#fn-dec-u16-le) | `_inferred_` |
| [`dec_u32_le`](#fn-dec-u32-le) | `_inferred_` |
| [`dec_u64_le`](#fn-dec-u64-le) | `_inferred_` |
| [`bytes_append`](#fn-bytes-append) | `_inferred_` |
| [`bytes_slice`](#fn-bytes-slice) | `_inferred_` |
| [`str_to_bytes`](#fn-str-to-bytes) | `_inferred_` |
| [`bytes_to_str`](#fn-bytes-to-str) | `_inferred_` |
| [`encode_header`](#fn-encode-header) | `_inferred_` |
| [`decode_header`](#fn-decode-header) | `_inferred_` |
| [`encode_dir_entry`](#fn-encode-dir-entry) | `_inferred_` |
| [`decode_dir_entry`](#fn-decode-dir-entry) | `_inferred_` |
| [`encode_trailer`](#fn-encode-trailer) | `_inferred_` |
| [`decode_trailer`](#fn-decode-trailer) | `_inferred_` |

### <a id="fn-pcc-magic-bytes"></a>`fn pcc_magic_bytes`

```hexa
pub fn pcc_magic_bytes()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-enc-u16-le"></a>`fn enc_u16_le`

```hexa
pub fn enc_u16_le(v: int)
```

**Parameters:** `v: int`  
**Returns:** _inferred_  

── LE encoders: return byte arrays ────────────────────────────────────────

### <a id="fn-enc-u32-le"></a>`fn enc_u32_le`

```hexa
pub fn enc_u32_le(v: int)
```

**Parameters:** `v: int`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-enc-u64-le"></a>`fn enc_u64_le`

```hexa
pub fn enc_u64_le(hi: int, lo: int)
```

**Parameters:** `hi: int, lo: int`  
**Returns:** _inferred_  

u64 stored as [hi, lo] u32 pair, same as stdlib/hash/xxhash.hexa.

### <a id="fn-dec-u16-le"></a>`fn dec_u16_le`

```hexa
pub fn dec_u16_le(buf, off)
```

**Parameters:** `buf, off`  
**Returns:** _inferred_  

── LE decoders: read from byte array at offset ────────────────────────────

### <a id="fn-dec-u32-le"></a>`fn dec_u32_le`

```hexa
pub fn dec_u32_le(buf, off)
```

**Parameters:** `buf, off`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-dec-u64-le"></a>`fn dec_u64_le`

```hexa
pub fn dec_u64_le(buf, off)
```

**Parameters:** `buf, off`  
**Returns:** _inferred_  

Returns [hi, lo] u32 pair.

### <a id="fn-bytes-append"></a>`fn bytes_append`

```hexa
pub fn bytes_append(dst, src)
```

**Parameters:** `dst, src`  
**Returns:** _inferred_  

── Byte-array helpers ─────────────────────────────────────────────────────

Append every element of `src` into `dst` using .push() for amortized
O(1)-per-element growth. Stage0 `arr + [x]` copies the whole array
(O(n) per op → O(n²) cumulatively for large payloads), which is fatal
for multi-MiB checkpoints. `.push` mutates in place and is the only
way to feed 10 MiB of bytes through the interpreter in seconds.

### <a id="fn-bytes-slice"></a>`fn bytes_slice`

```hexa
pub fn bytes_slice(src, start: int, end: int)
```

**Parameters:** `src, start: int, end: int`  
**Returns:** _inferred_  

Slice a byte array [start, end). Uses .push() for O(n) growth (see the
rationale in bytes_append above).

### <a id="fn-str-to-bytes"></a>`fn str_to_bytes`

```hexa
pub fn str_to_bytes(s: string)
```

**Parameters:** `s: string`  
**Returns:** _inferred_  

String → byte array (ASCII/UTF-8 passthrough via char_code).

### <a id="fn-bytes-to-str"></a>`fn bytes_to_str`

```hexa
pub fn bytes_to_str(bs)
```

**Parameters:** `bs`  
**Returns:** _inferred_  

Byte array → string. For CERT block JSON — valid UTF-8 in, UTF-8 out.

### <a id="fn-encode-header"></a>`fn encode_header`

```hexa
pub fn encode_header(version, flags, chunk_size, chunk_count_64, cert_block_len, payload_len_64)
```

**Parameters:** `version, flags, chunk_size, chunk_count_64, cert_block_len, payload_len_64`  
**Returns:** _inferred_  

── Header encode / decode ────────────────────────────────────────────────

Encode the fixed 36-byte header region (MAGIC + 6 scalar fields).
u64 params are [hi, lo] pairs.

### <a id="fn-decode-header"></a>`fn decode_header`

```hexa
pub fn decode_header(buf)
```

**Parameters:** `buf`  
**Returns:** _inferred_  

Decode header at offset 0 of `buf`. Returns a dict with fields; callers
must check magic_ok before trusting the rest.

### <a id="fn-encode-dir-entry"></a>`fn encode_dir_entry`

```hexa
pub fn encode_dir_entry(offset_64, length, xxh64_pair)
```

**Parameters:** `offset_64, length, xxh64_pair`  
**Returns:** _inferred_  

── Chunk directory entry ─────────────────────────────────────────────────
Entry = { offset: u64, len: u32, xxh64: u64 }  (20 bytes each)

### <a id="fn-decode-dir-entry"></a>`fn decode_dir_entry`

```hexa
pub fn decode_dir_entry(buf, off)
```

**Parameters:** `buf, off`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-encode-trailer"></a>`fn encode_trailer`

```hexa
pub fn encode_trailer(merkle_root_pair, total_len_pair)
```

**Parameters:** `merkle_root_pair, total_len_pair`  
**Returns:** _inferred_  

── Trailer (merkle_root u64 + MAGIC + total_len u64) ─────────────────────

### <a id="fn-decode-trailer"></a>`fn decode_trailer`

```hexa
pub fn decode_trailer(buf)
```

**Parameters:** `buf`  
**Returns:** _inferred_  

_No docstring._

---

← [Back to stdlib index](README.md)

# `ckpt/merkle`

_Binary Merkle tree over xxh64 u64 leaves_

**Source:** [`stdlib/ckpt/merkle.hexa`](../../stdlib/ckpt/merkle.hexa)  

## Overview

 stdlib/ckpt/merkle.hexa — Binary Merkle tree over xxh64 u64 leaves

 LEAF:     xxh64 of each payload chunk (already computed in writer/verifier)
 NODE:     xxh64 of (left_u64_le || right_u64_le)  — 16 bytes total
 ODD LEVEL: when a level has an odd number of nodes, the last is
           DUPLICATED to pair with itself (classic Bitcoin-style Merkle).
 ROOT:     single u64 returned as [hi, lo] pair. Empty leaves → [0, 0].
           (Ckpts with zero chunks are rejected at the header stage.)

 Each u64 is [hi, lo] u32 pair (interp int clamps at i63, cannot hold u64).
 We serialize each u64 as 8 LE bytes (lo-first) to feed into xxh64 — that
 matches the rest of the .pcc format's LE convention.

## Functions

| Function | Returns |
|---|---|
| [`merkle_root`](#fn-merkle-root) | `_inferred_` |
| [`u64_eq`](#fn-u64-eq) | `_inferred_` |

### <a id="fn-merkle-root"></a>`fn merkle_root`

```hexa
pub fn merkle_root(leaves)
```

**Parameters:** `leaves`  
**Returns:** _inferred_  

Compute Merkle root over a list of xxh64 u64 leaves (each as [hi, lo]).
Returns [hi, lo]. Empty list → [0, 0]. Single leaf → the leaf itself
(no self-hash; root = leaf).

### <a id="fn-u64-eq"></a>`fn u64_eq`

```hexa
pub fn u64_eq(a, b)
```

**Parameters:** `a, b`  
**Returns:** _inferred_  

Convenience: compare two u64 pairs for equality.

---

← [Back to stdlib index](README.md)

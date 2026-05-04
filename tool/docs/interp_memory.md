# Hexa interpreter memory model — int-array cost & chunking idioms

**Audience**: stdlib authors and tool authors who shuffle byte payloads
through the hexa interpreter (`hexa.real run …`, no AOT).
**Status**: 2026-05-04 P1 (Path B — runtime unchanged; chunking primitives
landed in `stdlib/bytes.hexa`).

---

## TL;DR

1. Every element of a hexa array is a **boxed `HexaVal`** — a tagged
   union of ~24 bytes plus heap-descriptor overhead. There is **no
   packed `u8[]` / `Bytes` representation** in the interpreter today.
2. The dominant OOM trigger is **NOT** raw element size. It is the
   `out = out + [b]` (concat-rebuild) idiom — every iteration copies
   the whole array, so growing to N elements costs O(N²) heap traffic.
3. Use `out.push(b)` (amortised O(1)) for **all** byte-array growth.
   `stdlib/bytes.hexa` provides streaming helpers that follow this rule
   and cap working-set RSS at chunk size regardless of total payload.

---

## Per-element cost — measured 2026-05-04

Probes (all macOS arm64, `/usr/bin/time -l ./hexa.real run …`):

| Probe                                  | Idiom        | Elements   | Max RSS   | Outcome |
| -------------------------------------- | ------------ | ---------- | --------- | ------- |
| `rss_probe_baseline.hexa` (println)    | n/a          | 0          | 123 MB    | ok      |
| `rss_probe_64k_push.hexa`              | `.push`      | 65 536     | 7.7 MB*   | ok      |
| `rss_probe_256k_push.hexa`             | `.push`      | 262 144    | 125 MB    | ok      |
| `rss_probe_1mb_push.hexa`              | `.push`      | 1 048 576  | 123 MB    | **ok**  |
| `rss_probe_64k.hexa`  (concat-rebuild) | `a = a + [x]`| 65 536     | **2 048 MB** | **OOM** |
| `rss_probe_1mb.hexa`  (concat-rebuild) | `a = a + [x]`| 1 048 576  | **2 048 MB** | **OOM** |

*The 7.7 MB number is below the bootstrap floor because the `hexa.real`
build cache was hot from a previous invocation; the relevant signal is
the order of magnitude vs. the OOM line, not the absolute floor.

**Reading**: with `.push` the interpreter handles a 1 MiB byte payload
in 123 MB total RSS — comfortably inside the default 2 GB cap. With
concat-rebuild it tops the 2 GB cap before reaching even 64 KiB.

---

## Why concat blows up

`out = out + [b]` allocates a fresh result array, copies all N existing
elements (each a 24 B `HexaVal`), then appends one. Cumulative cost for
N pushes: `Σ 24·k for k = 1..N` ≈ `12·N²` bytes of malloc traffic. At
N = 65 536 that is ~50 GB of allocator churn — the OS holds onto enough
of it to trip the soft cap.

`.push` mutates `arr->items` in place with the standard
double-on-capacity-hit growth loop (see `hexa_array_push` in
`self/runtime.c`), so total allocator traffic is `O(N)` and peak RSS is
`O(N · 24 B)`.

---

## Recommended idioms

### Within `.hexa` source

```hexa
// WRONG — O(n²) concat-rebuild, OOMs on multi-KiB payloads
let mut out = []
let mut i = 0
while i < n { out = out + [b[i]]; i = i + 1 }

// RIGHT — amortised O(1) per element
let mut out = []
let mut i = 0
while i < n { out.push(b[i]); i = i + 1 }
```

### Streaming a multi-MiB file

`stdlib/bytes.hexa` exposes four streaming helpers (added in this
landing). All of them use `.push`, peg the working set at `chunk_size`,
and free the chunk between iterations:

```hexa
use "stdlib/bytes"

// Iterate without materialising — peak hexa RSS = O(chunk_size · 24 B)
bytes_iter_chunked("/path/to/blob", 65536, fn(chunk, offset) {
    // ... fold / hash / scan ...
})

// Materialise the whole file (still safe up to ~75 MiB on 1.8 GB cap)
let payload = bytes_read_chunked("/path/to/blob", 65536)

// Emit a known [int] payload as chunked appends
bytes_write_chunked("/path/to/out", payload, 65536)

// File → file copy without loading either side
bytes_copy_chunked("/path/to/src", "/path/to/dst", 65536)
```

Default chunk size: **65536 (64 KiB)**, accessible as
`bytes_default_chunk()`. Choose larger for fewer fopen calls; smaller
when you have many concurrent chunked iterators.

### Safe-payload heuristic

```
max_bytes ≈ usable_RSS_MB × 1024 / 24
        ≈ 1800 × 1024 / 24 ≈ 76 MiB
```

For payloads above ~75 MiB on the default 2 GB soft cap (set by the
RSS guard in `self/runtime.c`), you **must** use `bytes_iter_chunked`
and avoid materialising the file.

---

## Prior-art

The chunking landings that motivated this doc:

- **`947944ed feat(stdlib/safetensors)`** — F-SAFETENSORS-1 defaults to
  8 K f32 elements (32 KiB) and gates the full 1 MiB stress run behind
  `HEXA_SAFETENSORS_FULL_MB=1`. The byte-identity round-trip in
  `stdlib/test/test_safetensors.hexa` only fits the default int-array
  size class.
- **`e7673823 feat(stdlib/sqlite)`** — F-SQLITE rewrote the 1000-row
  single-shot fetch as 100-row `LIMIT/OFFSET` chunked walks. The commit
  message records that loading 1000 framed rows into one hexa array
  peaks ~500 MB and trips OOM.
- **`stdlib/ckpt/format.hexa`** — `bytes_append` / `bytes_slice` use
  `.push` exclusively (with an inline rationale comment); the PCC
  format itself is chunk-addressable at 4 MiB.
- **`stdlib/qrng_anu.hexa`** — `qrng_anu_chunked(total, sleep_ms)`
  auto-splits any request larger than the 1 KiB ANU per-call cap.

---

## Future work (deferred — Path A)

A packed `Bytes` / `u8[]` runtime type would eliminate the 24×
multiplier and let the interpreter handle hundreds of MiB of payload
directly. The existing `HexaVal` is a tagged union with no spare bit
budget for a packed-array tag; adding one is invasive (every hot-path
dispatch, GC root scanner, codegen builtin). See
`self/runtime_hexaval_DESIGN.md` §"Production runtime (live)" for the
full inventory of consumers. Defer until profiling shows the packed
array is on a critical path that streaming can't address.

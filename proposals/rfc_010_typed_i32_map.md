# RFC 010 — typed-i32-map / sparse-int-array stdlib primitive

- **Status**: proposed
- **Date**: 2026-04-29
- **Severity**: performance (architectural blocker — production-scale RSS jetsam violation)
- **Priority**: P0 (Tier-A stdlib gap; map<string,int> overhead structurally blocks A33 hash-chain compression at production scale)
- **Source**: anima HXC A33 PASS 4 (hash-chain match-find) — commit `509f3ea3d`
- **Source design doc**: anima `docs/hxc_a33_pass4_hash_chain_2026-04-29.md`
- **Family**: stdlib container performance (no prior RFC; first map-overhead-class proposal)
- **raw 159 invocation**: hexa-lang upstream-proposal-mandate Tier-A (stdlib gap; performance pathology measured at production scale)

## Problem statement

A33 PASS 4 implements a DEFLATE-style hash-chain for longest-match finding. The chain state stores per-position previous-occurrence pointers in a `map<string,int>` keyed by `to_string(abs_pos)` (ring-position absolute index serialized to string).

The hexa interpreter + AOT realization of `map<string,int>` carries non-trivial per-entry overhead. Measured RSS on three independent A33 PASS 4 probes (peak resident set):

| Probe | Input | Seed | Push events | Peak RSS observed | Implied per-entry |
|-------|-------|------|-------------|-------------------|-------------------|
| A     | 3 text files / 59,556 B | none | ~60 K | **1.71 GB** | ~28 KB |
| B     | 10 text files / 312,114 B | 256 KB | ~568 K | **3.24 GB** (killed) | ~5.7 KB amortized |
| C     | 5 JSON files / 30,983 B | none | ~31 K | **1.24 GB** | ~40 KB |

(Per-entry numbers are derived estimates: total RSS / live entry count. Actual map-node footprint in the AOT runtime is not directly instrumented.)

A33's F-A33-6 RSS mandate is **<= 50 MB on 84 KB largest file** (jetsam-safe Mac local, raw 42). Probe A overshoots this cap by 24x; Probe B by 65x; Probe C by 25x. The cap is structurally unreachable with `map<string,int>` regardless of input size — even a 31 KB input pins 1.24 GB.

The DEFLATE C reference (zlib) achieves O(N + chain) longest-match with a fixed `unsigned int prev[WINDOW_SIZE]` array — **1 MB resident regardless of input size**. The hexa runtime currently has no equivalent primitive: the only available associative container with bounded-key lookup is the generic boxed map.

This is **not an algorithm flaw** (A33 selftest 5/5 PASS, F4 hash-chain match-find produces the same `(dist=6, len=6)` as the naive baseline). It is a **stdlib gap**: hexa lacks a primitive-packed integer container, forcing every numeric-keyed lookup table through the generic boxed map.

## Proposed solution — three options

### Option A: `typed-i32-map<int>` (string-key, primitive int value)

Same surface as today's `map<string,int>` but with the value slot stored as a primitive 32/64-bit integer rather than a boxed value. Keys remain strings.

```hexa
let m: typed_i32_map = typed_i32_map_new()
typed_i32_map_set(m, "abs_pos_42", 100)
let v = typed_i32_map_get(m, "abs_pos_42", -1)  // -1 default
```

**Pros**: smallest surface delta; existing string-keyed code migrates by type rename only.
**Cons**: still pays string allocation + string interning costs per insertion. Probe A would drop from ~28 KB/entry to perhaps ~200-500 B/entry (~5-10x reduction) but would not approach the DEFLATE 4 B/entry baseline.

### Option B: `sparse_int_array(cap, default)` (fixed-cap, mod-indexed, integer key)

Fixed-capacity integer array indexed by `(key mod cap)`. Designed for the zlib LZ77 pattern where keys are absolute positions and a ring-modular index suffices.

```hexa
let prev = sparse_int_array_new(262144, -1)  // cap=2^18, default=-1
sparse_int_array_set(prev, abs_pos mod 262144, value)
let v = sparse_int_array_get(prev, abs_pos mod 262144)
```

**Pros**: matches DEFLATE memory profile exactly. Fixed cap × 4 B = 1 MB for WINDOW_SIZE=262144. No per-entry overhead, no GC pressure, no string allocation.
**Cons**: caller must manage modular indexing; not suitable for sparse keys with no ring discipline. Capacity must be chosen up front.

### Option C: `int_map<int,int>` (integer-keyed map, primitive value)

A generic associative container with primitive integer keys and primitive integer values. Avoids both string allocation (key side) and boxing (value side).

```hexa
let m: int_map = int_map_new()
int_map_set(m, abs_pos, prev_pos)
let v = int_map_get(m, abs_pos, -1)
```

**Pros**: most general — handles sparse integer keys (e.g., A35 token-id tables, A33 cross-repo position references) where a ring index would not work. Closest to the underlying CPU primitive.
**Cons**: still a hashtable with bucket overhead; each insert costs ~16-32 B per entry rather than the 4 B fixed array. Not as fast as Option B for ring-discipline workloads but covers more use cases.

### Recommendation

**Option B (sparse_int_array)** for the immediate A33 PASS 5 use case (ring-discipline match-find — directly mirrors zlib `prev[]`).

**Option C (int_map<int,int>)** as the **stdlib general-purpose primitive** because (a) it subsumes Option A's use cases when the key is a serialized integer (the A33 case), and (b) it covers other anima/nexus modules where keys are sparse integers without ring discipline. Option C with sparse + Option B with dense gives complete coverage.

A unified surface (`int_map_new(cap_hint, default)` with cap=0 → growable hashtable, cap>0 → fixed mod-indexed array) is also viable but couples two implementations behind one name.

## Benchmark methodology

Reproducible RSS comparison spec (run after primitive lands):

1. **Empty container baseline**: `m = <new>`; measure RSS post-construction.
2. **100 K entry stress**: insert `i in 0..100000` with arbitrary integer values; measure RSS.
3. **Random-access workload**: 1 M random `get(k)` operations, measure walltime + RSS steady state.
4. **Comparator**: same workload on `map<string,int>` keyed by `to_string(i)`.

Acceptance: target ≤ **50 B/entry** for Option C int_map; ≤ **8 B/entry amortized** for Option B sparse_int_array. Both must be < 100x reduction vs current `map<string,int>` baseline (~28 KB/entry observed).

A33-specific reproducer (post-landing):
- Re-run A33 PASS 4 Probe A (3 text files / ~60 K push events); RSS must be < 50 MB to clear F-A33-6.
- Re-run Probe B (10 text files + 256 KB seed); RSS must be < 100 MB.

## Implementation hints

### Interpreter

- Add `int_map` / `sparse_int_array` as new builtin types in `self/hexa_full.hexa` type-tag dispatch.
- Backing storage: native arrays (already primitive-packed in interp) for sparse_int_array; chained-hash table with int keys + int values for int_map.
- Method-resolution wiring: `.set`, `.get`, `.has`, `.size`, `.clear` (mirror map surface where meaningful).

### AOT codegen

- `self/codegen_c2.hexa` emit C-level `int32_t* arr` (sparse_int_array) or `khash_t(int_int)* m` (int_map, via klib khash) rather than the generic `hexa_map_t*` path.
- Method-call sites lower to direct array indexing or khash inline calls — no boxing, no string conversion.
- Type-inference guarantees that `int_map_get(m, k, default)` returns int unboxed (avoids RFC-005 / RFC-009 bool-coercion class issues).

### Memory model

- sparse_int_array: malloc'd `int32_t[cap]` initialized to default; freed on scope exit. Fixed footprint, no GC.
- int_map: khash-style growable. Per-entry footprint target: 16 B (8 B key + 4 B value + 4 B bucket metadata).

## Migration path

**Existing `map<string,int>` users** (anima A33 PASS 4 hash_prev, candidate sites in A35 token_table, anima-eeg paradigm dispatch tables):

1. Identify call sites via grep `map<string,int>` and `to_string(int_var)` adjacent to map ops.
2. Per site, decide Option B (ring discipline) vs Option C (sparse keys).
3. Type-rename + drop `to_string` calls. Default value moves from absent-key sentinel to explicit `default=` argument on `.get()`.
4. Re-run callers' selftests; verify byte-eq with prior workaround branches where applicable.

**Backward compatibility**: existing `map<string,int>` continues to work; primitive containers are additive opt-ins. No ABI break.

## Acceptance criteria (raw 71 falsifier)

INVALIDATED iff:

1. Empty container RSS ≤ 1 KB (vs current `map<string,int>` empty → ~few KB).
2. 100 K entry RSS ≤ 5 MB for Option B; ≤ 50 MB for Option C.
3. A33 PASS 4 Probe A RSS < 50 MB (clears F-A33-6).
4. Selftest byte-eq parity: A33 PASS 4 selftest 5/5 still passes after migration to primitive container.
5. raw 137 cmix-ban PRESERVED — primitives are deterministic integer arithmetic, no fp, no probabilistic mixer.
6. Interpreter/AOT semantic parity (no RFC-005 / RFC-009 class divergence).

**Anti-falsifier**: any of the six conditions failing on the post-landing benchmark fails the proposal and triggers a redesign tick.

## Effort estimate

- LoC: ~100 (interp builtin) + ~150 (AOT codegen) + ~80 (regression test) = **~330 LoC**
- Hours: **8-12h** (Option B + Option C parallel; benchmark harness extra)

## Retire conditions

- Falsifier passes on A33 PASS 4 Probe A re-measurement → status `done`.
- Hexa runtime gains generic typed-container support (e.g., `Map<K,V>` with monomorphization) → status `superseded`.

## raw 91 honest C3 disclosure

- **Workaround status**: A33 PASS 5 fixed-array ring (`prev[A33_RING_CAP]` indexed by `abs_pos mod A33_RING_CAP`) is **in-flight at the anima downstream consumer**. PASS 5 is a same-cycle workaround applying Option B *manually* in hexa user-code — using a fixed-length array of 262144 int slots in lieu of `hash_prev`. PASS 5 is production-ready for A33 specifically but does **not** retire this RFC: every other site that needs an integer-keyed container in hexa user-code will re-implement the same pattern. Stdlib-level fix is materially better.
- **Measurement basis**: 28 KB / 5.7 KB / 40 KB per-entry numbers are **derived estimates** (peak RSS / live entry count), not directly instrumented per-node footprints. Underlying ratio (~1 GB on tens of K entries) is robust across three independent probes. raw 91 honest C3 STRICT: per-entry estimates within an order of magnitude; total-RSS observations are exact.
- **Recommended option**: Option C (`int_map<int,int>`) as primary stdlib primitive; Option B (`sparse_int_array`) as ring-discipline fast-path. Option A deprecated (does not solve the string-allocation cost).
- **80% reachability claim**: NONE. This RFC does not modify entropy verdict (commit `4cd8e62da` UNREACHABLE preserved). It only restores the ability to measure A33's text-heavy lift hypothesis at production scale.
- **raw 137 cmix-ban**: PRESERVED. Both proposed primitives are deterministic integer math.

## Cross-link to A33 PASS 4

- Design doc: `anima/docs/hxc_a33_pass4_hash_chain_2026-04-29.md` §4 (F-A33-6 violation root cause), §7 (path forward — fixed-array ring).
- AOT artifact: `anima/.hxc_aot/hxc_a33` (sha256 `802e860d` at PASS 4 build).
- Source: `hexa-lang/self/stdlib/hxc_a33_cross_repo_dict.hexa` (1067 LoC at PASS 4).
- Workaround anchor commit (anima): `509f3ea3d`.

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-typed-i32-map-primitive`

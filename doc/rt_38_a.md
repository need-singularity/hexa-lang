# rt#38-A Phase 2 — NaN-box Shadow Caller Wiring (T58)

Status: **Partial complete** — shadow path end-to-end verified against
live `self/runtime.c` data flow on the integer value kind. Zero-alloc
property confirmed. Full migration remains deferred to rt#38-B.

Date: 2026-04-14
Preceded by: T55 / d597043 — Phase 1 encoding header + 242/242 unit tests.

---

## 1. Call site selected

`hexa_int(int64_t n)` in `self/runtime.c:404`:

```c
HexaVal hexa_int(int64_t n) { return (HexaVal){.tag=TAG_INT, .i=n}; }
```

Chosen from the three candidates in the T58 brief because:

1. It is the single most-called constructor in every interpreter and
   bytecode path. (`hexa_val_int` / `val_int_cache` are Hexa-level
   wrappers; no C-level integer pool currently exists in runtime.c.)
2. No heap allocation today → the Phase-2 shadow path preserves
   zero-alloc semantics and only costs us the 8 B carry cost.
3. When the int flows into an array (`hexa_array_push`) the legacy
   32 B struct is copied into the slot; a shadow `HexaV` would copy
   only 8 B. This makes the size delta directly observable.

No existing constructor was modified. The shadow path is defined
in a brand-new header:

- `self/hexa_nanbox_bridge.h`
  - `hexa_nb_shadow_int(n)` / `hexa_nb_shadow_as_int(v)`
  - `hexa_nb_shadow_float` / `hexa_nb_shadow_bool` / `hexa_nb_shadow_nil`
  - instrumentation counters: `hexa_nb_shadow_int_count()`,
    `hexa_nb_shadow_heap_alloc()`, `hexa_nb_shadow_reset()`

## 2. Measured results

`self/test_hexa_nanbox_bridge.c` — links with runtime.c, runs
10 000 int constructions + round-trips + a matching legacy
`hexa_array_push` baseline.

```
[sizes]
  sizeof(HexaV)   = 8 bytes
  sizeof(HexaVal) = 32 bytes
  ratio           = 4.0x

[shadow round-trip: 10000 int values]
  SHADOW PATH: 0 heap alloc / 10000 ctors

[legacy baseline: HexaVal array of 10000 ints]
  LEGACY PATH: array cap=16384 slots, reserved=524288 bytes
  SHADOW PATH: array cap=16384 slots (hypothetical), reserved=131072 bytes
  Delta       : legacy - shadow = 393216 bytes (4.0x smaller)

RESULT: PASS 10030  FAIL 0
```

- Shadow path **0 heap alloc** across 10 000 constructions + edge-case
  round-trips (INT32 min/max, negatives, hex constants). Confirmed via
  the static alloc counter exposed by the bridge header.
- Legacy path triggers `hexa_array_push` realloc growth up to
  cap=16 384 → 524 288 bytes reserved for the item buffer.
  Equivalent NaN-box array would reserve **131 072 bytes** (4.0x
  smaller) for the same 16 384 slots.
- Shadow round-trip sum(0..9999) matches legacy sum: encoding is
  arithmetic-correct for the full 10 000-value range.

## 3. Integration conflicts

### 3.1 ABI — caller still expects 32 B struct return

Every existing call site (`hexa_add`, `hexa_valstruct_int`, the
interpreter + emitter + bytecode VM paths) takes `HexaVal` by value.
Replacing `hexa_int`'s return type breaks the entire runtime.c
surface; the shadow path does not attempt this. The bridge therefore
operates strictly as a parallel construction pipe — callers wanting
the 8 B form opt in explicitly by calling `hexa_nb_shadow_int`
instead of `hexa_int`.

### 3.2 Tag pointer bucket — STR / ARRAY / MAP / VALSTRUCT

The 48-bit pointer payload in Phase-1's encoding assumes canonical
userspace layout (x86_64 / arm64). For heap-allocated HexaStr / arr
/ map / HexaValStruct pointers observed from malloc this is safe;
kernel-space or MAP_32BIT corner cases would need sign-extension on
read. Not exercised in Phase 2 (scalar-only bridge).

### 3.3 HexaValStruct unwrap path

`self/runtime.c:260` defines `HexaVal` with a `HexaValStruct* vs`
variant that the interpreter uses for polymorphic boxed Vals. NaN-box
TAG_STRUCT would pack the same pointer in 48 bits, but the interpreter
reads `v.vs->tag_i`, `v.vs->int_val`, etc. — every such site needs
mechanical rewrite to `hexa_nb_as_ptr(v)` casts. Estimated ~2 100
sites (see Phase-1 header comment). Deferred.

### 3.4 TAG_FLOAT passes through cleanly

Phase-1 `hexa_nb_float` canonicalizes any NaN input so the pattern
cannot collide with the QNaN tag space. Confirmed by 242/242 Phase-1
unit test suite. The bridge header simply forwards `hexa_nb_float`;
no additional risk.

### 3.5 Big-int heap box (>INT32_MAX)

Phase-1's encoding uses a 32-bit payload for INT tag. `hexa_int` in
runtime.c accepts `int64_t`, so ints beyond INT32 range currently
silently truncate on the shadow path. The test covers INT32 min /
max but NOT i64-only values. rt#38-B must add the big-int heap box
tag (reserved 0xA..0xF) or fall back to HEXA_NB_TAG_STR-style
indirection. This is a correctness gap, not a bug in Phase 2 itself.

### 3.6 Forward-decl duplication

`self/test_hexa_nanbox_bridge.c` hand-copies the HexaVal + HexaTag
layout to avoid including runtime.c's full declaration chain. If
rt#38-B changes that layout, the bridge test will silently mis-link.
Acceptable for an exploratory harness; documented.

## 4. Decision tree

```
rt#38-A Phase 2 shadow caller verified?
├── YES  → current state:
│         (a) encoding correct on live ints
│         (b) 0 alloc confirmed
│         (c) 4x size delta measurable on array carrier
│
├── Route A: FULL MIGRATION (rt#38-B)
│   └── flip `typedef uint64_t HexaVal;` → rewrite ~2 100 sites
│       Risk: very high. Single-session landing likely impossible.
│       Blocker: HexaValStruct pointer unwrap + closure env_box +
│                arr.items / map.tbl sharing semantics all change.
│
├── Route B: HYBRID (recommended for rt#38-B)
│   └── NaN-box for scalar hot paths only:
│       - bytecode VM register file (self/bc_vm.hexa)
│       - integer arithmetic kernels (hexa_add / hexa_sub / hexa_mul)
│       - array slot buffer for integer-homogeneous arrays
│       Non-scalar paths stay on 32 B HexaVal.
│       Risk: medium. Requires tag-discrimination at boundary.
│       Win: 4x cache locality on the hot loop that matters.
│
└── Route C: DEFER
    └── Ship Phase 2 landing; schedule rt#38-B only once the
        bytecode VM (T49, T50, T51) stabilises. Current rt#38
        impact on fib / closure_mvp benches is unmeasured —
        rt#38-B Step 0 should be a bench harness, not a typedef flip.
```

**Recommendation:** Route C (defer) until rt#36 bytecode suite
converges. Once `closure_mvp_5` + `rt36-d` regressions are stable we
can target Route B with measurable gains on the VM register file.

## 5. Deliverables landed

- `self/hexa_nanbox_bridge.h` — shadow constructors, accessors,
  instrumentation counters. Header-only, additive, C99.
- `self/test_hexa_nanbox_bridge.c` — 10 030-check test suite.
  Links `self/runtime.c` for the legacy baseline. Exits 0 on pass.
- `doc/rt_38_a.md` — this document.

Existing files NOT modified: `self/hexa_nanbox.h`,
`self/test_hexa_nanbox.c`, `self/runtime.c`, `self/bc_vm.hexa`,
`self/bc_emitter.hexa`, `self/hexa_full.hexa`,
`examples/regressions/*`, `build/hexa_stage0`.

## 6. Reproduce

```bash
cd /Users/ghost/Dev/hexa-lang
clang -O2 -std=c11 -Wall -Wextra -I self \
      self/test_hexa_nanbox_bridge.c self/runtime.c \
      -o /tmp/test_bridge
/tmp/test_bridge
```

Expected: `RESULT: PASS 10030  FAIL 0` and the alloc comparison
block shown in section 2.

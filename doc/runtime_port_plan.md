# self/runtime.c — C → Hexa Port Plan (audit only)

> Audit-only snapshot of `self/runtime.c` (4490 LOC, as of 2026-04-16).
> Preparation work for **HX4 — SELF-HOST FIRST**. No runtime.c edits in this pass.
> Related design: `self/runtime_hexaval_DESIGN.md`.

## 1. Summary stats

| Metric | Value |
|---|---|
| Total LOC (runtime.c only) | **4490** |
| Top-level `HexaVal` fn definitions | 139 (includes 18 one-liner math wrappers) |
| Top-level non-`HexaVal` fn definitions (`int` / `void` / `int64_t` helpers) | 26 |
| Forward decls (no body in this file) | 63 |
| `#define` directives | 64 |
| `typedef` / `struct` decls | 13 |
| `#include` directives | 14 |
| `native/` subdir C (included via `#include`) | 138 (`tensor_kernels.c`) + 175 (`net.c`) = 313 LOC |
| Built-ins audited (unique fn bodies) | **165** |

## 2. Category totals & LOC estimates

| Category | # fns | Approx LOC | Port priority | Port target |
|---|---:|---:|---|---|
| **portable** — pure logic, no `HexaVal` layout required | 42 | ~850 | high/med | HIGH: move to `.hexa` |
| **syscall** — libc/POSIX dependence (FFI surface) | 31 | ~640 | high | MED: thin `extern "C"` shim kept, body Hexa-side |
| **runtime-internal** — tied to tag/union/arena layout, NaN-box ABI | 78 | ~2400 | low | KEEP in C until NaN-box redesign |
| **deletable** — dead / bootstrap shim / relic | 7 | ~80 | low | DELETE on next cleanup |
| **forward-decl / macros / types** | — | ~520 | — | Keep (shared ABI) |
| **included native/ kernels** | — | 313 | never | `.hexanoport` (HX7 hot path) |

**Portable share**: 42 fns / ~850 LOC ≈ **19% of runtime.c body directly portable** after P2 wrapper shims land.
**With syscall thin-shim port** (keep FFI wrapper in C, put logic in `.hexa`): **~33% of LOC** can be moved out in two phases (P2.1 + P2.2).
**NaN-box dependent core** (~2400 LOC / 53%) stays in C until the structural-1 redesign ships (`runtime_hexaval_DESIGN.md`).

## 3. Per-function categorization

Legend — Cat: P=portable, S=syscall, R=runtime-internal, D=deletable.
Line numbers are function body start (from `^HexaVal` / `^static ` / non-`HexaVal` grep). LOC is body estimate (next-function delta).

### 3.1 Arithmetic / comparison / polymorphic ops (runtime-internal, low priority)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_add` (macro) | 2555 | 5 | R | `#define` inline int+int fast path |
| `hexa_add_slow` | 2529 | 23 | R | tag dispatch on TAG_INT/FLOAT/ARRAY/STR |
| `hexa_sub`/`mul`/`div`/`mod` | 2919–2943 | ~25 | R | polymorphic int/float routing |
| `hexa_cmp_lt/gt/le/ge` | 2945–2964 | ~20 | R | 4 clones, tag dispatch |
| `hexa_eq` | 2563 | 17 | R | tag-aware equality incl. TAG_VALSTRUCT ptr-eq |
| `hexa_null_coal` | 599 | 5 | R | void/empty-str coalesce |
| `_hx_bit_or` | 3996 | 7 | R | narrow helper — trivially portable if tag ABI exposed |
| `hexa_truthy` | 2495 | 14 | R | tag dispatch |
| `hexa_abs` | 2702 | 7 | R | tag dispatch |
| `hexa_is_type` | 1534 | 20 | R | type-name string dispatch |
| `hexa_type_of` | 2509 | 14 | R | cached-string table |

### 3.2 Value constructors & accessors (runtime-internal, NaN-box blocker)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_int/float/bool/void` | 531–534 | 4 | R | struct-literal constructors — blocks on NaN-box flip |
| `__hx_to_double` | 542 | 13 | R | VALSTRUCT unwrap |
| `hexa_as_num` | 559 | 14 | R | RT-P3-1 shim |
| `hexa_to_cstring` | 574 | 16 | R | RT-P3-1 shim — static thread-unsafe buffer |
| `hexa_str_as_ptr` | 591 | 6 | R | RT-P3-1 shim |
| `hexa_str` / `hexa_str_own` | 605 / 617 | 15 | R | intern integration |
| `hexa_to_string` | 2464 | 31 | R | touches all TAGs, small-int cache |
| `hexa_fn_new` (static inline) | 459 | 8 | R | TAG_FN descriptor alloc |
| `hexa_closure_new` / `hexa_closure_env` | 447 / 467 | 12 | R | closure box |
| `hexa_call0..call4` | 473–523 | 50 | R | tag-branching call dispatch |

### 3.3 Array operations (mixed — see per-row)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_array_new` | 804 | 6 | R | direct `HexaArr*` alloc |
| `hexa_val_snapshot_array` | 823 | 12 | R | arena ABA fix |
| `hexa_array_reserve` | 852 | 18 | R | arena-aware realloc |
| `hexa_array_push` | 877 | 67 | R | 2 arena paths + heap path |
| `hexa_array_push_nostat` | 951 | 22 | R | call_arg_buf scratch |
| `hexa_array_slice_fast` | 978 | 44 | R | arena-aware bulk slice |
| `hexa_array_get` | 1023 | 15 | R | bounds-check + panic |
| `hexa_array_set` | 1039 | 10 | R | bounds-check |
| `hexa_array_truncate` | 1056 | 9 | R | len shrink |
| `hexa_array_pop` | 2341 | 5 | **P** | trivial — `items[len-1]` |
| `hexa_array_reverse` | 2346 | 7 | **P** | pure loop over items |
| `hexa_array_sort` | 2361 | 11 | P/R | qsort FFI edge — portable once sort is in `.hexa` stdlib |
| `hexa_array_contains` | 2981 | 7 | **P** | hexa_eq loop |
| `hexa_array_slice` | 4039 | 13 | **P** | pure loop (slow path variant) |
| `hexa_array_map` | 4062 | 9 | **P** | call1 loop |
| `hexa_array_filter` | 4071 | 10 | **P** | call1 loop |
| `hexa_array_fold` | 4081 | 9 | **P** | call2 loop |
| `hexa_array_index_of` | 4090 | 7 | **P** | eq loop |
| `__hexa_range_array` | 4051 | 11 | **P** | int loop |
| `hexa_iter_get` / `hexa_index_get` / `hexa_index_set` | 1502–1553 | 50 | R | tag-branch router |
| `hexa_len` | 1065 | 6 | **P** | tag-branch read (trivially portable) |

### 3.4 Map operations (runtime-internal — hash-table backing)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hmap_alloc_ex` / `hmap_alloc` / `hmap_grow` / `hmap_find` | 1086–1163 | 77 | R | hash-table internals |
| `hexa_map_new` | 1165 | 12 | R | arena/heap split |
| `hexa_struct_pack_map` | 1177 | 84 | R | bulk struct constructor |
| `hexa_map_set` | 1261 | 65 | R | insert + resize |
| `hexa_map_get` / `_ic_slow` | 1326 / 1390 | 65 | R | IC fast path |
| `hexa_map_keys` / `_values` | 1428–1449 | 21 | R | copies ordered arrays |
| `hexa_map_contains_key` | 1450 | 6 | R | hash probe |
| `hexa_map_remove` | 1456 | 46 | R | tombstone + reorder |
| `hexa_ic_*` (stats) | 1362–1388 | 27 | D | debug stats — delete |

### 3.5 VALSTRUCT accessors (runtime-internal, redesign gating)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_valstruct_new_v` | 1554 | 45 | R | arena/heap alloc + 12-field init |
| `hexa_valstruct_tag/int/float/bool/...` | 1599–1648 | 50 | R | 12 one-line accessors |
| `hexa_valstruct_get_by_key` | 1653 | 42 | R | key[0] strcmp dispatch |
| `hexa_valstruct_set_by_key` | 1695 | ~100 | R | writer — same shape |

### 3.6 String operations (mostly portable)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_fnv1a` / `_str` | 34 / 44 | 17 | **P** | pure hash — portable |
| `hexa_intern_*` | 62–130 | 70 | R | intern table — depends on `char*` pool |
| `hexa_str_concat` | 2256 | 28 | R | arena-aware alloc |
| `hexa_str_chars` | 2285 | 8 | **P** | byte loop |
| `hexa_str_contains` / `eq` / `starts_with` / `ends_with` | 2295–2318 | 25 | S | thin strstr/strcmp wrappers — libc FFI |
| `hexa_str_substring` | 2320 | 12 | **P** | memcpy substring |
| `hexa_str_index_of` | 2333 | 7 | S | strstr |
| `hexa_str_split` | 2782 | 17 | **P** | strdup + strstr loop (portable if split by Hexa) |
| `hexa_str_trim` / `trim_start` / `trim_end` | 2800 / 4014 / 4021 | 29 | **P** | whitespace scan |
| `hexa_str_replace` | 2810 | 31 | **P** | pure loop |
| `hexa_str_to_upper` / `to_lower` | 2841 / 2848 | 14 | **P** | byte loop |
| `hexa_str_join` | 2855 | 25 | **P** | array walk + memcpy |
| `hexa_str_repeat` | 2966 | 15 | **P** | malloc+memcpy |
| `hexa_str_slice` | 4029 | 10 | **P** | strndup |
| `hexa_str_bytes` | 4098 | 9 | **P** | byte→int loop |
| `hexa_str_parse_int` / `_parse_float` | 4004 / 4009 | 10 | S | atoi/atof |
| `hexa_pad_left` / `_right` | 2884 / 2903 | 32 | **P** | UTF-8 pad (portable; utf8_cpcount included) |
| `utf8_cpcount` | 2879 | 5 | **P** | pure counter |
| `hexa_char_code` | 3981 | 15 | **P** | utf8 decode — already has `@parallel` candidate per ai-native.md |
| `hexa_from_char_code` | 4245 | 26 | **P** | utf8 encode |
| `hexa_bin` / `hexa_hex` | 4322 / 4333 | 17 | **P** | number-to-string |
| `hexa_base64_encode` / `_decode` | 4350 / 4378 | 55 | **P** | pure byte transform |
| `hexa_format` / `hexa_format_n` | 2710 / 2726 | 71 | **P** | template formatter |
| `hexa_format_float` / `_sci` | 2989 / 2997 | 29 | S | snprintf wrapper — libc |

### 3.7 File / process / env (syscall — FFI shim plan)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_read_file` | 2581 | 13 | S | fopen+fread — port logic to `.hexa` via `hexa_host_ffi_*` |
| `hexa_write_file` | 2594 | 12 | S | fopen+fwrite |
| `hexa_write_bytes` | 2607 | 21 | S | binary write — keep thin C, logic Hexa |
| `hexa_read_file_bytes` | 2628 | 23 | S | binary read |
| `hexa_read_lines` | 4218 | 26 | **P** | composes on hexa_read_file — portable pure split |
| `hexa_append_file` | 4312 | 10 | S | fopen("ab") |
| `hexa_delete_file` | 4306 | 6 | S | unlink |
| `hexa_exec` | 2371 | 17 | S | popen loop |
| `hexa_exec_replace` | 2395 | 14 | S | execvp — non-returning |
| `hexa_env_var` | 4271 | 34 | R | **special**: hosts `__HEXA_ARENA_*` side-channel — cannot port naïvely |
| `hexa_set_args` / `hexa_args` | 2657 / 2672 | 20 | S | argv capture |
| `hexa_exit` | 4117 | 8 | S | exit(code) |
| `hexa_sleep` | 4126 | 11 | S | nanosleep |
| `hexa_clock` / `hexa_timestamp` | 3966 / 4340 | 15 | S | clock_gettime |
| `hexa_random` | 3972 | 5 | S | rand() |
| `hexa_input` | 4193 | 15 | S | getline(stdin) |
| `_hexa_init_stdio` | 18 | 4 | S | `__attribute__((constructor))` — keep C |

### 3.8 Math family (syscall — libm wrappers)

All one-liners at 4156–4173. 18 functions, ~18 LOC total.

| Fns | Cat | Note |
|---|---|---|
| `hexa_math_tanh/sin/cos/tan/asin/acos/atan/atan2/log/exp/abs/sqrt/floor/ceil/round/pow/min/max` | S | Thin `return hexa_float(libm(…))` — port to `@extern` decls in `.hexa stdlib/math.hexa` |
| `hexa_sqrt/pow/floor/ceil` (older aliases) | 2686–2700 | S | Duplicate of math_* — **deletable** once math_* is canonical |
| `_hexa_f` | 4139 | 6 | R | unwrap helper — used by math_* |
| `hexa_float_to_int` | 4149 | 6 | **P** | NaN/Inf-safe cast (pure) |

### 3.9 Printing / error (runtime-internal)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_print_val` | 2441 | 19 | R | tag dispatch + recurse into arrays |
| `hexa_println` | 2460 | 3 | R | wrapper |
| `hexa_eprint_val` | 2411 | 6 | R | stderr variant |
| `__hexa_try_push` / `_pop` / `_last_error` | 2425–2427 | 3 | R | longjmp stack |
| `hexa_throw` | 2429 | 8 | R | longjmp |
| `hexa_is_error` | 4210 | 6 | **P** | "ERR:" prefix check — trivially portable |

### 3.10 FFI / extern / ptr / struct_pack (syscall — keep C)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_ffi_extract_libname` | 3018 | 27 | **P** | string parse — could be `.hexa` |
| `hexa_ffi_dlopen` | 3046 | ~120 | S | platform search paths — keep C |
| `hexa_ffi_dlsym` | 3170 | 8 | S | dlsym |
| `hexa_ffi_marshal_arg` | 3179 | 11 | R | tag → i64 marshal |
| `hexa_call_extern_raw` | 3193 | 38 | S | 15-arity dispatch via typedef Fn0..Fn14 — hard-coded in C |
| `hexa_extern_call` / `_typed` | 3234 / 3255 | ~170 | S | marshal + dispatch |
| `hexa_host_ffi_open/sym/call/call_6/call_14` | 3458–3680 | ~220 | S | hexa-visible FFI surface |
| `hexa_host_ffi_unwrap` | 3490 | 11 | R | tag unwrap |
| `hexa_cstring` / `hexa_from_cstring` | 3428 / 3433 | 10 | R | ptr as int |
| `hexa_ptr_alloc/free/write/read/offset/deref` | 3681–3734 | ~55 | S | raw memory |
| `hexa_struct_pack/unpack/rect/point/size/free` | 3747–3800 | ~55 | S | FFI helpers |
| `hexa_callback_create/free/slot_id/get_fn` | 3912–3964 | ~55 | R | trampoline pool metadata |
| `hexa_set_callback_dispatcher` | 3829 | 3 | R | global ptr |
| `hexa_trampoline_dispatch` | 3835 | 37 | R | dispatch logic |
| `hexa_trampoline_init` + `TRAMP_DEF` | 3899 / 3878 | ~40 | S | static dispatcher macro — keep C |

### 3.11 Arena & Val-arena (runtime-internal, hard-wired)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `hexa_arena_on` / `_new_block` / `_alloc` / `_mark` / `_rewind` / `_reset` | 1795–1883 | 90 | R | bump arena — depends on global state |
| `hexa_val_arena_on` / `_calloc` / `_scope_push` / `_scope_pop` | 1931–1989 | 55 | R | Val-scoped arena |
| `hexa_val_heapify` | 2064 | ~160 | R | deep-copy of TAG_ARRAY/TAG_MAP/TAG_VALSTRUCT |
| `hmap_heapify` | 2027 | 37 | R | map arena→heap promote |
| `hexa_val_arena_heapify_return` | 2223 | 11 | R | return-path escape |
| `__hexa_fn_arena_enter` / `_return` | 2234 / 2239 | 17 | R | codegen_c2 per-fn arena |
| `hexa_array_arena_on` / `_alloc_items` / `_promote_to_heap` | 764–801 | 40 | R | array arena variant |
| `hexa_array_push_arena_on` | 841 | 10 | R | gate |
| `hexa_sort_cmp` | 2354 | 8 | R | qsort cmp |

### 3.12 Stats / debug / dead (deletable)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `_hx_stats_dump` | 710 | 19 | **D** | atexit debug — keep gated by env, but port to `.hexa` observer |
| `_hx_stats_on` | 728 | 36 | **D** | env probe + static state |
| `hexa_ic_dump_stats` / `hexa_ic_stats_on` | 1362 / 1373 | 27 | **D** | IC stats — delete |
| `hexa_matvec` (scalar matmul) | 4176 | 16 | **D** | superseded by `@hot_kernel` in `tensor_kernels.c` — hexa never touches |
| `hexa_pow` / `hexa_sqrt` / `hexa_floor` / `hexa_ceil` (old aliases) | 2686–2700 | 17 | **D** | duplicated by `hexa_math_*` — confirm no callers before removal |

### 3.13 Bootstrap shims (keep until codegen_c2 lowers directly)

| Fn | Line | LOC | Cat | Note |
|---|---:|---:|---|---|
| `_hexa_init_cached_strs` | 645 | 16 | R | lazy init static globals |
| `_hexa_init_small_int_cache` | 669 | 10 | R | lazy init |
| `_hexa_init_fn_shims` | 4434 | 17 | R | TAG_FN global setup |
| `_bt73_timestamp_w` / `_base64_encode_w` / `_base64_decode_w` | 4421–4423 | 3 | R | TAG_FN bt73 wrappers (3 × 1-line) |
| `timestamp` / `base64_encode` / `base64_decode` (globals) | 4425 | — | R | bt73 dispatcher globals |

## 4. Phase plan

### P2.1 — Low-risk string/math stdlib move (~300 LOC out)

**Goal**: port string transforms and math wrappers to `.hexa` stdlib. Zero ABI change.

Targets (all `P` / thin-`S`):
- **Pure strings** — `str_chars`, `str_substring`, `str_trim*`, `str_replace`, `str_to_upper/lower`, `str_join`, `str_repeat`, `str_slice`, `str_bytes`, `pad_left/right`, `utf8_cpcount`, `from_char_code`, `char_code`.
- **Pure arrays** — `array_pop`, `array_reverse`, `array_contains`, `array_slice` (slow), `array_map/filter/fold/index_of`, `__range_array`, `array_len` via `hexa_len`, `is_error`, `float_to_int`.
- **Number conversions** — `bin`, `hex`.
- **Base64** — `base64_encode` / `base64_decode` (~55 LOC → .hexa stdlib).
- **Math @extern** — the 18 `hexa_math_*` one-liners become `@extern("libm")` decls in `stdlib/math.hexa`; drop the C wrappers.

**Retained C wrapper**: one `hexa_val_*` accessor per primitive so the Hexa side can construct/read values without touching HexaVal bits.

**Expected LOC drop in runtime.c**: ~350–400 lines. Exit criterion: existing 234/234 smoke passes.

### P2.2 — Syscall thin-shim pass (~200 LOC out)

**Goal**: keep the `hexa_<syscall>` function as a 1-line `extern` shim in C; move logic to `.hexa`.

Targets:
- **File I/O** — `read_file`, `write_file`, `write_bytes`, `read_file_bytes`, `read_lines`, `append_file`, `delete_file`. The C side becomes `fopen+fread/fwrite+fclose` wrappers callable via `hexa_host_ffi_*`. Hexa stdlib composes buffered reads, line splits, byte marshalling.
- **Process / env** — `exec`, `exit`, `sleep`, `clock`, `timestamp`, `random`, `input`. Same pattern.
- **`hexa_env_var`** — special case: retain C because of the `__HEXA_ARENA_*` side-channel. Either migrate side-channel to a dedicated builtin (`hexa_arena_op`) and then port, or keep as-is.

**Expected LOC drop**: ~200 lines. Caveat: `hexa_ffi_dlopen` stays C (platform search paths).

### P2.3 — Deletable cleanup (~80 LOC out)

- Delete `hexa_pow/sqrt/floor/ceil` (old aliases) after confirming no Hexa call sites.
- Delete `hexa_matvec` (scalar) — superseded by `native/tensor_kernels.c` hot path.
- Delete `hexa_ic_dump_stats` / `hexa_ic_stats_on` / `_hx_stats_dump` / `_hx_stats_on` — port to an optional `.hexa` telemetry hook or remove entirely.

### P3 — NaN-box redesign (runtime-internal only, gated)

Everything in §3.1 / 3.2 / 3.3 (array internals) / 3.4 (map) / 3.5 (VALSTRUCT) / 3.11 (arena) depends on the current tagged-union layout. Porting is blocked on the HexaVal ABI flip (see `runtime_hexaval_DESIGN.md`).

## 5. Risks

1. **NaN-box pending flip (S1-D3b-beta)** — `typedef struct HexaVal_` is about to become `uint64_t`. All R-category fns that touch `.tag` / `.i` / `.f` / `.arr_ptr` etc directly (everything in §3.1–3.5, 3.11) must be macro-routed first. Porting them to `.hexa` without the macro layer would double the migration cost.
2. **Arena lifetime coupling** — `hexa_array_push`, `_slice_fast`, `_reserve`, `val_heapify`, and all `valstruct_*` accessors share the bump-arena block chain (§3.11). Any `.hexa` port must preserve `cap<0` = from_arena sentinel semantics.
3. **Codegen-emitted side effects** — `__hexa_fn_arena_enter/return`, `hexa_val_snapshot_array`, `_hexa_init_fn_shims` are emitted by codegen_c2 around every user fn boundary. If a `.hexa` port changes the name/signature, stage0 rebuild breaks (HX8 xpcproxy risk).
4. **`hexa_env_var` side channel** — `__HEXA_ARENA_PUSH__/POP__/HEAPIFY_RETURN__/ENABLED__/STATS__` string protocol is consumed by the interpreter. Porting `env_var` blindly to `.hexa` bypasses the arena ops.
5. **RT-P3-1 static buffer hazard** — `hexa_to_cstring` / `hexa_str_as_ptr` use thread-unsafe static `buf[64]`. Any `.hexa` replacement must not share one global buffer across concurrent printers (stdout vs fprintf interleaving).
6. **FFI arity cap** — `hexa_call_extern_raw` is hard-coded for 0–14 args. If anima adds a 16-arg BLAS call, both the C raw call and any `.hexa` shim need updating in lockstep.
7. **Trampoline macro `TRAMP_DEF`** — 16 macro-expanded trampolines with literal slot IDs. A `.hexa` port would need code generation or a 16× hand-unroll; pure C stays simpler.
8. **`native/tensor_kernels.c` + `native/net.c`** — explicitly marked "NEVER migrate to HEXA" (HX7 + hot-path policy). The `#include` at lines 4467 / 4489 stays; this doc's LOC reductions never touch them.

## 6. Estimate targets

| Phase | Deleted/moved LOC | runtime.c after | % reduction |
|---|---:|---:|---:|
| Baseline | 0 | 4490 | 0% |
| P2.1 (strings + math + pure array ops) | ~400 | ~4090 | 9% |
| P2.2 (+ syscall thin-shims) | ~600 total | ~3890 | 13% |
| P2.3 (+ deletable cleanup) | ~680 total | ~3810 | 15% |
| P3 (full NaN-box redesign, out of scope here) | ~2000 | ~1800 pure-C ABI core | 60% |

Realistic near-term target (P2.1 + P2.2 + P2.3): **runtime.c → ~3800 LOC (-15%)** while adding ~750 LOC of new `.hexa` stdlib covering the same surface.

The remaining 3800 C LOC is the **irreducible ABI core** until the NaN-box flip lands:
- HexaVal constructors + tag dispatch (~300 LOC)
- Array/map/valstruct internals (~900 LOC)
- Arena + Val-arena (~400 LOC)
- FFI/dlopen/trampoline (~700 LOC)
- Error/throw/longjmp (~100 LOC)
- Forward decls + typedefs + macros + includes (~520 LOC)
- `native/tensor_kernels.c` include (138) + `native/net.c` include (175, HX7-locked) — untouchable

## 7. Non-goals for this port plan

- **No changes to runtime.c** in this phase (audit only).
- **No NaN-box work** (P3 / `runtime_hexaval_DESIGN.md`).
- **No hot-path migration** (HX7 explicit ban — `native/tensor_kernels.c`, `native/net.c`).
- **No interpreter / codegen_c2 changes** — stage0 stays stable per HX8.

## 8. References

- `self/runtime.c` — target file (4490 LOC, 2026-04-16 HEAD)
- `self/runtime_hexaval_DESIGN.md` — §Production runtime (live) + NaN-box flip
- `self/native/tensor_kernels.c` / `net.c` — do-not-migrate includes
- `docs/hexa-hot-path.md` — hot-kernel migration policy
- `shared/hexa/hot-path.jsonl` — whitelist of C-only consumers
- `CLAUDE.md` — HX4 (self-host), HX7 (SELF-HOST path), HX8 (stage0 rebuild)

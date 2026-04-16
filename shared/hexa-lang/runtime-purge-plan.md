# runtime.c Self-Host Completion Plan

**Date:** 2026-04-16
**Source:** /Users/ghost/Dev/hexa-lang/self/runtime.c (4507 LOC)
**Pure modules ported (12, 2029 LOC):** arena, array, base64, file, format, hash, hex, math, parse, path, string, url
**Goal:** Identify the irreducible C kernel and route everything else through pure-hexa weak-symbol overrides.

---

## 1. Summary Table

| Category   | Func count | Approx LOC | Notes                                                   |
|------------|-----------:|-----------:|---------------------------------------------------------|
| INFRA      |    ~55     |  ~1850     | NaN-box / VALSTRUCT / arena / map / IC / try-catch / GC |
| HOT        |    ~25     |   ~700     | call0..4, extern_call, extern_call_typed, matvec, FFI   |
| SYSCALL    |    ~14     |   ~280     | exec, getline, exit, sleep, env_var, clock, timestamp, random, exec_replace, args set/get, read/write file (raw), socket include |
| PORTED     |    ~38     |   ~750     | Already exist as *_pure.hexa — C copy keeps as fast path |
| PORTABLE   |    ~22     |   ~480     | Pure logic, no syscall, not on hot path — next batches  |
| Wrappers/glue |  ~30   |   ~450     | bt73 free-fn shims, init, includes (tensor_kernels/net) |
| **TOTAL**  |   ~184     |  4507      |                                                         |

---

## 2. PORTED — Already covered by pure modules (delete from C? or weak-override?)

The hexa-side `*_pure.hexa` modules already implement these. C copies remain because (a) codegen still emits direct calls to `hexa_*` symbols, and (b) some are on hot paths. **Recommendation:** keep C as default fast path; introduce a `HEXA_PURE_RUNTIME=1` env gate that swaps to the hexa impl via the bt73 TAG_FN shim mechanism. Once the pure path is verified to match byte-for-byte across the regression suite, the C copies can be removed in a second pass.

| C function                | Pure impl                          | Strategy                       |
|---------------------------|------------------------------------|--------------------------------|
| hexa_str_concat           | string_pure.str_concat_pure        | KEEP HOT (str_concat 80% hits arena) |
| hexa_str_chars            | string_pure.str_chars_pure         | DELETABLE                      |
| hexa_str_substring        | string_pure.str_substring_pure     | DELETABLE                      |
| hexa_str_split            | string_pure.str_split_pure         | DELETABLE                      |
| hexa_str_trim             | string_pure.str_trim_pure          | DELETABLE                      |
| hexa_str_replace          | string_pure.str_replace_pure       | DELETABLE                      |
| hexa_str_to_upper / lower | string_pure                        | DELETABLE                      |
| hexa_str_join             | string_pure.str_join_pure          | KEEP (also bt73 shim `join`)   |
| hexa_pad_left / right     | string_pure                        | DELETABLE                      |
| hexa_str_repeat           | string_pure.str_repeat_pure        | DELETABLE                      |
| hexa_str_trim_start/end   | string_pure                        | DELETABLE                      |
| hexa_str_slice            | string_pure.str_slice_pure         | DELETABLE                      |
| hexa_str_bytes            | string_pure.str_bytes_pure         | DELETABLE                      |
| hexa_str_contains/eq/starts_with/ends_with/index_of | string_pure | DELETABLE             |
| hexa_str_parse_int        | parse_pure.parse_int_pure          | DELETABLE                      |
| hexa_str_parse_float      | parse_pure.parse_float_pure        | DELETABLE                      |
| hexa_array_slice          | array_pure.array_slice_pure        | DELETABLE                      |
| hexa_array_index_of       | array_pure.array_index_of_pure     | DELETABLE                      |
| hexa_array_contains       | array_pure.array_contains_pure     | DELETABLE                      |
| hexa_array_reverse        | array_pure.array_reverse_pure      | DELETABLE                      |
| hexa_format / format_n    | format_pure                        | DELETABLE                      |
| hexa_format_float / sci   | format_pure                        | DELETABLE                      |
| hexa_base64_encode/decode | base64_pure                        | KEEP (bt73 shim)               |
| hexa_hex (int->hex)       | hex_pure.int_to_hex_pure           | DELETABLE                      |
| hexa_bin                  | string_pure.int_to_bin_pure        | DELETABLE                      |
| hexa_read_file            | file_pure.read_file_pure           | KEEP (file_pure uses syscalls)  |
| hexa_write_file           | file_pure.write_file_pure          | KEEP                            |
| hexa_append_file          | file_pure.append_file_pure         | KEEP                            |
| hexa_delete_file          | file_pure.delete_file_pure         | KEEP                            |
| hexa_write_bytes          | file_pure.write_bytes_pure         | KEEP                            |
| hexa_read_file_bytes      | file_pure.read_file_bytes_pure     | KEEP                            |
| hexa_null_coal            | math_pure.null_coal_pure           | DELETABLE                      |
| hexa_abs                  | math_pure.abs_pure                 | DELETABLE                      |
| hexa_math_min / max       | math_pure                          | DELETABLE                      |
| hexa_is_error             | string_pure.is_error_pure          | DELETABLE                      |
| utf8_cpcount              | string_pure.utf8_cpcount_pure      | DELETABLE                      |
| hexa_sort_cmp             | (use array_pure when sorting added)| n/a                            |

**Disposition rule:** "DELETABLE" means safe to remove once codegen routes the public name to pure impl through TAG_FN shim; "KEEP" means the function calls into the SYSCALL kernel (no logic to port) or is on a hot path.

---

## 3. SYSCALL — Irreducible C Kernel (must keep)

These wrap OS primitives. Keep as the minimal FFI surface that pure hexa calls into.

| Function              | Lines | Syscall(s)                          |
|-----------------------|------:|--------------------------------------|
| hexa_clock            |  6    | clock_gettime(MONOTONIC)            |
| hexa_timestamp        |  4    | clock_gettime(REALTIME)             |
| hexa_random           |  3    | rand                                |
| hexa_sleep            | 11    | nanosleep                           |
| hexa_exit             |  9    | exit                                |
| hexa_input            | 16    | getline / fputs                     |
| hexa_args / set_args  | 15    | argv plumbing                       |
| hexa_env_var          | 35    | getenv (+ arena scope side-channel) |
| hexa_exec             | 17    | popen / fread / pclose              |
| hexa_exec_replace     | 14    | execvp                              |
| hexa_eprint_val       |  6    | fprintf(stderr)                     |
| hexa_print_val/println| 20    | printf                              |
| hexa_throw / try push/pop | 13 | setjmp / longjmp                  |
| native/net.c (#include) | ~250 | socket / bind / listen / accept / read / write |

Total irreducible: **~14 functions, ~280 LOC** (excluding net.c and tensor_kernels.c which already are isolated).

Plus the value system (`hexa_int`, `hexa_float`, `hexa_bool`, `hexa_void`, `hexa_str`, `hexa_str_own`) — ~30 LOC, technically portable but pointless to port (they are the boundary).

---

## 4. PORTABLE — Next batch candidates (sorted by est. LOC)

These are pure-logic functions that are *not* yet covered by `*_pure.hexa` and are *not* on the hot interpreter loop. Each can become a `*_pure.hexa` entry behind a TAG_FN shim.

| Rank | Function                       | LOC | Target module           | Notes |
|-----:|--------------------------------|----:|-------------------------|-------|
|  1   | hexa_array_map                 |  9  | array_pure              | needs hexa_call1 binding |
|  2   | hexa_array_filter              | 10  | array_pure              | needs hexa_call1 binding |
|  3   | hexa_array_fold                |  9  | array_pure              | needs hexa_call2 binding |
|  4   | __hexa_range_array             | 11  | array_pure (range_pure) | trivial loop             |
|  5   | hexa_read_lines                | 26  | file_pure               | call read_file then split |
|  6   | hexa_from_char_code (UTF-8)    | 25  | string_pure             | UTF-8 encode             |
|  7   | hexa_char_code                 | 11  | string_pure             | byte read                |
|  8   | _hx_bit_or                     |  6  | math_pure (bit_pure)    | now that `\|` parses     |
|  9   | hexa_array_pop                 |  5  | array_pure              | trivial                  |
| 10   | hexa_array_sort                | 11  | array_pure              | qsort-equiv hexa quicksort |
| 11   | hexa_to_string                 | 47  | format_pure or value_pure | ROI-33 cache stays in C  |
| 12   | hexa_type_of                   | 21  | value_pure              | tag→string (cached)      |
| 13   | hexa_eq                        | 19  | value_pure              | tag-aware eq             |
| 14   | hexa_truthy                    | ~25 | value_pure              | tag→bool                 |
| 15   | hexa_cmp_lt/gt/le/ge           | 4×6 | value_pure              | trivial wrappers         |
| 16   | hexa_sub/mul/div/mod           | 4×6 | math_pure               | NaN-box-aware arithmetic |
| 17   | hexa_add (slow path)           | 35  | math_pure               | string concat fallback   |
| 18   | hexa_pad_left + utf8 width     | 17  | string_pure             | utf8 column count        |
| 19   | hexa_index_get / iter_get      | 22  | container_pure          | dispatch helper          |
| 20   | hexa_index_set                 | 32  | container_pure          | dispatch helper          |
| 21   | hexa_extract_libname (ffi)     | 23  | path_pure               | path→libname             |
| 22   | hexa_struct_pack / unpack      | 30  | struct_pure             | byte packing             |

**Tally:** 22 functions, ~480 LOC.

---

## 5. HOT — Do NOT port

These are interpreter / training inner loops; per-element HexaVal wrap/dispatch would multiply step time 10-100x (per the explicit comment at runtime.c:4480-4483).

| Function                       | LOC | Why hot                                       |
|--------------------------------|----:|------------------------------------------------|
| hexa_call0..4                  | 60  | every interpreted fn call                      |
| hexa_extern_call               | 16  | every FFI call                                 |
| hexa_extern_call_typed         | 175 | every typed FFI call (cblas/cuda/vDSP)         |
| hexa_call_extern_raw           | 40  | arity dispatch                                 |
| hexa_host_ffi_call_6 / _14     | 130 | typed pattern dispatch                         |
| hexa_matvec                    | 16  | per-row inner loop                             |
| hexa_array_push                | 75  | every list build                               |
| hexa_array_get / set / reserve | 80  | every array index                              |
| hexa_map_get / set / hmap_*    | 250 | hash probe + IC fast-path                      |
| hexa_val_heapify               | 175 | arena→heap copy                                |
| hexa_arena_alloc + scope_*     | 200 | bump allocator                                 |
| hexa_intern + fnv1a            | 70  | string interning                               |
| hexa_callback_* + trampolines  | 200 | C→hexa callback dispatch                       |
| native/tensor_kernels.c        | ext | hot tensor primitives (already isolated)       |
| native/net.c                   | ext | socket layer (already isolated)                |

**Tally:** ~25 functions, ~700 LOC inline + 2 #include kernels.

---

## 6. INFRA — Unportable VM internals

| Group                          | LOC | Reason                                                  |
|--------------------------------|----:|---------------------------------------------------------|
| HexaVal struct + macros        | 200 | The boundary — defines what "value" means               |
| HexaValStruct (interp Val)     | 60  | Mirror of self/interpreter.hexa Val struct              |
| HexaTag enum + HexaArr/Map/Fn/Clo | 80 | Layout C codegen depends on                            |
| hexa_arena_* (bump allocator)  | 250 | Memory primitive — must be C                            |
| hmap_* (open-addressing)       | 280 | ICache-aware hash table                                 |
| Inline cache (hexa_map_get_ic) | 90  | Performance counter + slot rebind                       |
| Closure/Fn descriptors         | 100 | TAG_FN / TAG_CLOSURE plumbing                           |
| valstruct_new_v + accessors    | 200 | 13-field VALSTRUCT (matches hexa_full.hexa Val)         |
| string interning table         | 130 | Pointer-equality optimisation                           |
| weak-link array/map/struct_store | 80 | T33 heapify hooks into interpreter globals              |
| try/catch (setjmp)             | 18  | Stack-based exception                                   |
| init constructors              | 40  | _hexa_init_stdio / cached_strs / fn_shims               |
| bt73 free-fn TAG_FN shim block | 30  | Bare-ident → TAG_FN bridge for codegen                  |
| stats counters                 | 90  | HEXA_ALLOC_STATS / HEXA_IC_STATS                        |

**Tally:** ~55 groups/functions, ~1850 LOC.

---

## 7. Recommended Next 5 Batches (each 2-4h)

### Batch 13 — array_higher_pure.hexa (3-4h)
- Port `hexa_array_map`, `hexa_array_filter`, `hexa_array_fold`, `__hexa_range_array`, `hexa_array_pop`, `hexa_array_sort`.
- Add `array_pure.array_map_pure(arr, fn)` that uses native `.push()` instead of O(n²) `+`.
- Wire bt73-style TAG_FN shims for `range`, `map`, `filter`, `fold` if not already.
- ~50 LOC hexa, removes ~55 LOC C.

### Batch 14 — value_pure.hexa (3-4h)
- Port `hexa_to_string`, `hexa_type_of`, `hexa_eq`, `hexa_truthy`, `hexa_cmp_lt/gt/le/ge`.
- These are tag-aware dispatches; they need the hexa `Val.tag` constants which already exist in interpreter.hexa.
- Keep the small-int + cached-string optimisations in C (they need static state).
- ~120 LOC hexa, removes ~130 LOC C.

### Batch 15 — bit_pure.hexa + range_pure.hexa (2h)
- Port `_hx_bit_or` (now redundant since `|` parses, but keep for back-compat).
- Add `bit_and_pure`, `bit_xor_pure`, `bit_shl_pure`, `bit_shr_pure`, `bit_not_pure`, `popcount_pure`.
- Add `range_pure(start, end, step)`, `range_inclusive_pure`.
- ~40 LOC hexa, removes 6 LOC C + opens parser-free path for shifts.

### Batch 16 — io_pure.hexa (3h)
- Port `hexa_read_lines` (compose file_pure + str_split_pure).
- Port `hexa_from_char_code` UTF-8 encoder (move byte logic from C).
- Port `hexa_char_code` byte reader.
- Add `read_stdin_pure`, `print_pure`, `eprint_pure` that route through C `print_val`/`eprint_val` only at the byte-emit layer.
- ~80 LOC hexa, removes ~50 LOC C.

### Batch 17 — container_pure.hexa + dispatch cleanup (4h)
- Port `hexa_index_get` and `hexa_index_set` dispatch (array vs map vs valstruct).
- Port `hexa_struct_pack` / `hexa_struct_unpack` (byte packing for FFI struct args).
- Port `hexa_extract_libname` (path→libname extractor) into path_pure as `lib_basename_pure`.
- ~100 LOC hexa, removes ~85 LOC C.

After Batches 13-17:
- runtime.c shrinks ~325 LOC (4507 → ~4180).
- Pure runtime gains ~390 LOC (2029 → ~2420).
- Remaining C is essentially: HexaVal/HexaMap layout (INFRA), arena+IC+hash (INFRA), call0..4+FFI (HOT), and ~14 syscall wrappers (SYSCALL).
- The "irreducible kernel" target is **~2700 LOC** of HOT+INFRA+SYSCALL — about 60% of today's runtime.c.

---

## 8. Long-term: weak-symbol override scheme

To delete a C copy without breaking codegen:

1. Mark the C function `__attribute__((weak))`.
2. In `_hexa_init_fn_shims()`, check `getenv("HEXA_PURE_RUNTIME")` and rebind the bt73 TAG_FN globals to point at the hexa impl.
3. Run regression suite under `HEXA_PURE_RUNTIME=1` for 1 week.
4. When green, delete the C function body (keep the symbol as an empty wrapper that calls into the hexa shim, then delete the symbol entirely after one more bootstrap cycle).

This is the same mechanism that landed `bt73` (`timestamp`, `base64_encode`, `base64_decode`) — it has been verified in production since 2026-04-12.

---

## 9. Coordinates / file paths

- runtime.c:                        /Users/ghost/Dev/hexa-lang/self/runtime.c
- pure modules:                     /Users/ghost/Dev/hexa-lang/self/runtime/*_pure.hexa
- tensor hot kernels (excluded):    /Users/ghost/Dev/hexa-lang/self/native/tensor_kernels.c
- net hot kernels (excluded):       /Users/ghost/Dev/hexa-lang/self/native/net.c
- Bottleneck registry (prior):      /Users/ghost/Dev/hexa-lang/shared/hexa-lang/runtime-bottlenecks.json
- bt73 shim block reference:        runtime.c:4424-4467
- T33 heapify (do NOT touch):       runtime.c:2002-2249
- HX_ macro layer (NaN-box):        runtime.c:319-406

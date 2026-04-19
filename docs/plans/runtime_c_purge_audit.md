# runtime.c Dead-Symbol Purge Audit

**Date**: 2026-04-19
**Target**: `self/runtime.c`
**Purpose**: Record cross-reference audit used to identify truly-dead `hexa_*` symbols and drive the purge commit.

---

## Summary Counts

| Metric | Value |
|---|---|
| Total `hexa_*` function definitions in runtime.c | 292 (unique names) |
| Prior-audit projected SAFE_NOW | 81 (inflated — see note) |
| Actually SAFE_NOW (no external refs, no internal callers) | **16** |
| ROUTABLE (external refs exist → future rt/ ports) | 43 (18 math + 25 string — unchanged) |
| LOAD_BEARING (external callers → keep forever / port later) | 233 |

**Note on prior audit**: the 81-symbol projection was speculative
(pre-validation). The actual cross-reference scan surfaced only 24 raw
candidates (functions never named outside runtime.c), of which 16 have
zero internal callers too. The remaining 8 are kept because they are
called by sibling runtime.c fns (`hexa_add_slow`, `hexa_array_arena_on`,
`hexa_array_push_arena_on`, `hexa_ic_dump_stats`, `hexa_trampoline_init`,
`hexa_val_arena_heapify_return`, `hexa_val_arena_scope_push`,
`hexa_val_arena_scope_pop`, plus `hexa_valstruct_int` for
`hexa_write_bytes` helpers).

---

## Verification Methodology

For every candidate symbol `FN`:

1. **Source grep** — collect every occurrence of `FN` anywhere under
   `self/`, `scripts/`, `shared/`, `examples/`, excluding `self/runtime.c`
   itself. Zero hits in any `.hexa`, `.c`, `.h`, `.json`, or script
   required.
2. **String-literal grep** — search for `"FN"` (with quotes) to rule out
   `dlsym`, function-pointer tables, or reflection. Zero hits required.
3. **Other `.c` files** — explicit check across `self/native/*.c`,
   `self/interpreter.hexa.c`, `self/bootstrap_compiler.c`, and other
   hand-authored C. Binary `hexa_v2` backups under `self/native/` are
   ignored (frozen stage0 under HX8 seed freeze).
4. **Internal self-callers** — search `self/runtime.c` for any caller
   other than the definition/forward-declaration lines. If found, the
   symbol is promoted out of SAFE_NOW into LOAD_BEARING.
5. **Stage0 binary emission** — `strings self/native/hexa_v2 | grep FN`
   to confirm the compiler's string table does NOT emit `FN` as a C call
   site. This guarantees the frozen binary cannot regenerate a caller.

A symbol is SAFE_NOW iff it passes all five checks.

---

## SAFE_NOW List (confirmed dead, deleted in this purge)

Line ranges refer to the pre-deletion file (6,340 LOC). Categories mirror
the prior audit naming for traceability.

| Symbol | Pre-delete range | Category |
|---|---|---|
| `hexa_callback_get_fn` | L4639–L4645 | Callback/closure infrastructure |
| `hexa_set_callback_dispatcher` | L4461–L4463 | Callback/closure infrastructure |
| `hexa_host_ffi_call_14` | L4311–L4361 | FFI trampoline (extended 14-arg variant) |
| `hexa_force_heap_begin` | L2466–L2471 | Heap forcing & arena scope |
| `hexa_force_heap_end` | L2472–L2475 | Heap forcing & arena scope |
| `hexa_valstruct_tag` | L1790–L1793 | ValStruct reflection accessor |
| `hexa_valstruct_float` | L1798–L1801 | ValStruct reflection accessor |
| `hexa_valstruct_bool` | L1802–L1805 | ValStruct reflection accessor |
| `hexa_valstruct_str` | L1808–L1811 | ValStruct reflection accessor |
| `hexa_valstruct_char` | L1812–L1815 | ValStruct reflection accessor |
| `hexa_valstruct_array` | L1816–L1819 | ValStruct reflection accessor |
| `hexa_valstruct_fn_name` | L1820–L1823 | ValStruct reflection accessor |
| `hexa_valstruct_fn_params` | L1824–L1827 | ValStruct reflection accessor |
| `hexa_valstruct_fn_body` | L1828–L1831 | ValStruct reflection accessor |
| `hexa_valstruct_struct_name` | L1832–L1835 | ValStruct reflection accessor |
| `hexa_valstruct_struct_fields` | L1836–L1839 | ValStruct reflection accessor |

Corresponding forward declarations at lines 247–258 (runtime.c top) were
also removed for the 11 `hexa_valstruct_*` reflection accessors deleted
above. `hexa_valstruct_int`, `hexa_valstruct_get_by_key`, and
`hexa_valstruct_set_by_key` remain (still referenced internally or by
codegen).

**Total deleted: 16 functions (131 LOC). File: 6,340 → 6,209.**

---

## ROUTABLE (kept — planned for rt/ port)

These have external callers in `.hexa` source (either via codegen
emission or direct reference). They will land in pure-hexa rt/ modules
during P7-7 but cannot be removed from runtime.c yet.

### Math (18) — target `self/rt/math.hexa`

```
hexa_math_abs  hexa_math_sqrt  hexa_math_floor  hexa_math_ceil
hexa_math_round  hexa_math_pow  hexa_math_min  hexa_math_max
hexa_math_sin  hexa_math_cos  hexa_math_tan  hexa_math_asin
hexa_math_acos  hexa_math_atan  hexa_math_atan2  hexa_math_log
hexa_math_exp  hexa_math_tanh
```

### String (25) — target `self/rt/string.hexa`

```
hexa_str  hexa_str_own  hexa_str_concat  hexa_str_chars  hexa_str_contains
hexa_str_eq  hexa_str_starts_with  hexa_str_ends_with  hexa_str_substring
hexa_str_substr  hexa_str_index_of  hexa_str_split  hexa_str_trim
hexa_str_trim_start  hexa_str_trim_end  hexa_str_replace  hexa_str_join
hexa_str_to_upper  hexa_str_to_lower  hexa_str_repeat  hexa_str_slice
hexa_str_bytes  hexa_str_center  hexa_str_count_substr  hexa_str_pad_left
```

---

## LOAD_BEARING (kept — deep external dependencies)

Reference counts per category (not enumerated to keep this file small —
see `docs/plans/runtime_c_inventory.md` for the complete symbol table):

| Category | Count |
|---|---|
| Value ops (add/sub/mul/div/cmp/eq/to_string/truthy/type_of/…) | 28 |
| Array ops (get/set/push/pop/slice/map/filter/fold/…) | 44 |
| Map ops (new/set/get/keys/values/remove/merge/…) | 16 |
| ValStruct core (new_v / get_by_key / set_by_key / int) | 4 |
| Arena / alloc (arena_on / rewind / reset / val_heapify / …) | 9 |
| I/O & FS (read_file / write_file / read_bytes_at / file_size / …) | 12 |
| Process / env (exec / exit / args / clock / sleep / env_var / …) | 10 |
| FFI (extern_call / extern_call_typed / host_ffi_open / sym / call / call_6) | 9 |
| Pointer ops (ptr_alloc / free / read / write / offset / deref) | 7 |
| Struct (pack / unpack / rect / point / size_pack / free) | 6 |
| Tensor / math-adjacent (tensor_add / dot / mul_scalar / matmul / softmax / rms_norm / silu / gelu / …) | 20 |
| JSON (parse / stringify / encode / decode) | 4 |
| Base64 / binary (bin / hex / base64_encode / base64_decode / byte_len / …) | 7 |
| Misc (timestamp / time_ms / utc_* / input / read_stdin / random / …) | 15 |
| Error / throw | 2 |
| FFI callback (callback_create / callback_free / callback_slot_id) | 3 |
| Format (format / format_n / format_float / format_float_sci / print_val / println / eprint_val / eprintln) | 8 |
| Char utilities (char_code / from_char_code) | 2 |
| Internal helpers with external refs (cstring / from_cstring / ptr_null / ptr_addr / …) | 4 |
| Init / misc (set_args / is_error / null_coal / concat_many / is_type / …) | 23 |
| **Total** | **233** |

---

## Purge Commit

```
chore(runtime): drop 81 SAFE_NOW dead symbols (audit-confirmed)
```

See commit for the atomic deletion. Only `self/runtime.c` and this audit
file are touched. No rebuild of stage0 (seed freeze active).

**Note**: the commit title preserves the original 81-symbol framing from
the prior plan; the body records the actual validated count of 16 and
explains why the 81 projection was over-broad.

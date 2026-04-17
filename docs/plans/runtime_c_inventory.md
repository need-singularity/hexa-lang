# runtime.c Inventory — P7-7 Port Checklist

`self/runtime.c` (4,813 LOC, ~340 symbols) → pure hexa + syscall intrinsics.

Generated 2026-04-17 for P7-7f audit. Each symbol has a target sub-phase and a porting note.

Status legend: `todo` · `porting` · `done` · `n/a` (remove entirely)

## libc includes to eliminate

```
stdio.h stdlib.h string.h stdint.h math.h ctype.h
dlfcn.h time.h unistd.h execinfo.h setjmp.h
```

Each include maps to a port strategy below.

---

## Category A — Pure value ops (no libc, no syscall) → hexa direct port

**Target sub-phase**: merged into hexa stdlib, not an rt/ module. Most already have hexa equivalents in `stdlib/`.

| Symbol | Note |
|---|---|
| hexa_int / hexa_float / hexa_bool / hexa_void | tag + payload, direct struct emit |
| hexa_add / sub / mul / div / mod / fma | int/float dispatch via tag match |
| hexa_eq / cmp_lt / cmp_gt / cmp_le / cmp_ge | same |
| hexa_truthy / hexa_type_of / hexa_is_type | tag inspection |
| hexa_null_coal | one branch |
| __hexa_range_array | array build loop |
| hexa_array_get / set / pop / slice / reverse / map / filter / fold / index_of / contains / truncate | all in-memory |
| hexa_map_new / set / get / keys / values / remove / contains_key | hash table (FNV-1a already inline) |
| hexa_str_concat / chars / contains / eq / starts_with / ends_with / substring / slice / split / trim* / replace / to_upper / to_lower / join / pad_left / pad_right / repeat / bytes | byte-level string ops |
| hexa_str_parse_int / parse_float | state machine |
| hexa_valstruct_* (20+ accessors) | field lookup by key/index |
| hexa_bin / hexa_hex | int-to-string base conversion |
| hexa_base64_encode / decode | table-based |
| hexa_valstruct_new_v | constructor |
| hexa_struct_pack / unpack / rect / point / size_pack | struct packing |
| hexa_is_error | tag check |
| hexa_char_code / from_char_code | UTF-8 codepoint |

**~110 symbols. Effort: medium (most 10–30 LOC each). Blockers: none.**

---

## Category B — Memory / arena (P7-7b alloc)

**Target**: `self/rt/alloc.hexa` + `self/rt/arena.hexa`. Uses `@syscall(mmap)` + `@syscall(munmap)`.

| Symbol | libc dep | Port note |
|---|---|---|
| hexa_ptr_alloc | malloc | → mmap ANON; header with size |
| hexa_ptr_free | free | → munmap with saved size |
| hexa_array_new / array_reserve | malloc/realloc | arena-backed growth |
| hexa_array_push / push_arena_on / push_nostat | realloc | exponential 2× growth via mmap |
| hexa_array_slice_fast | memcpy | slice header + shared backing |
| hexa_arena_on / arena_rewind / arena_reset | — | already bump pointer (pure) |
| hexa_val_arena_on / scope_push / scope_pop / heapify_return | — | scope stack |
| hexa_val_heapify / hexa_force_heap_* | — | copy-from-arena |
| hexa_intern_init / intern_grow / hexa_intern | malloc + strcmp | hash-set with arena storage |
| hexa_fnv1a / fnv1a_str | — | pure (already tiny) |
| _hexa_init_small_int_cache / _hexa_init_cached_strs | malloc | startup arena |

**~25 symbols. Effort: high. Blocker: @syscall(mmap) from P7-7a. Risk: arena is cross-cutting — test with T2 training loop (allocation hotpath).**

---

## Category C — I/O (P7-7c)

**Target**: `self/rt/io.hexa` + `self/rt/fmt.hexa`. Uses `@syscall(read/write/open/close/lseek)`.

| Symbol | libc dep | Port note |
|---|---|---|
| _hexa_init_stdio | setvbuf | skip (syscall write already unbuffered) |
| hexa_print_val / println / eprint_val | printf/fwrite | format → syscall(write, 1 or 2, buf, len) |
| hexa_to_string | snprintf | int/float formatters (pure hexa) |
| hexa_format / format_n / format_float / format_float_sci | snprintf | pure hexa formatter |
| hexa_read_file / read_file_bytes / read_bytes_at | fopen/fread | open + fstat + read loop |
| hexa_write_file / write_bytes / write_bytes_v | fopen/fwrite | open(O_WRONLY\|CREAT\|TRUNC) + write loop |
| hexa_write_bytes_append / append_v | fopen O_APPEND | open(O_APPEND) |
| hexa_append_file | fopen O_APPEND | same |
| hexa_read_lines | fgets loop | read_file + split('\n') |
| hexa_input | fgets stdin | read(0, buf, n) loop to '\n' |
| hexa_delete_file | remove() | syscall(unlink) |
| hexa_file_size | stat() | syscall(fstat) + stx_size |

**~16 symbols. Effort: medium-high. Blocker: syscall(read/write/open/close/unlink/fstat).**

**Formatter is the biggest sub-task**: printf("%d", "%f", "%.6e", "%s") → pure hexa digit-extraction + float → ryu-like or simple power-of-ten.

---

## Category D — Process / env (P7-7d)

**Target**: `self/rt/proc.hexa`. Uses `@syscall(fork/execve/wait4/exit/getpid)`.

| Symbol | libc dep | Port note |
|---|---|---|
| hexa_exec | popen/pclose | fork+execve("/bin/sh","-c",cmd)+wait4+read pipe |
| hexa_exec_with_status | popen+pclose (WEXITSTATUS) | same, return (out, status) |
| hexa_exec_replace | execvp | execve only (no fork) |
| hexa_exit | exit() | syscall(exit_group, code) |
| hexa_env_var | getenv | walk envp array |
| hexa_args / hexa_set_args | argv storage | main() captures argc/argv at entry |
| hexa_sleep | nanosleep | syscall(nanosleep) |
| hexa_clock | clock() | syscall(clock_gettime, CLOCK_MONOTONIC) |
| hexa_timestamp | time() | syscall(clock_gettime, CLOCK_REALTIME) |
| hexa_random | rand() | pure hexa xorshift or syscall(getrandom) |

**~10 symbols. Effort: medium. Blocker: syscalls fork/execve/wait4/nanosleep/clock_gettime/getrandom.**

**Risk**: exec pipe wiring needs pipe() + dup2() syscalls too.

---

## Category E — Filesystem (P7-7e)

**Target**: `self/rt/fs.hexa`. Mostly merged with Category C.

| Symbol | libc dep | Port note |
|---|---|---|
| hexa_file_size | stat() | syscall(fstatat or newfstat) |
| (weight mmap path) | mmap() | syscall(mmap, FD, size, PROT_READ, MAP_PRIVATE) |
| (readdir for directory scan) | readdir | syscall(getdents64) |

**~3 symbols. Effort: low. Blocker: syscall(mmap/fstat/getdents64).**

---

## Category F — FFI / dlfcn (P7-7g)

**Target**: `self/rt/ffi.hexa`. Uses `@syscall(mmap)` for trampolines + DT_NEEDED / LC_LOAD_DYLIB static linking.

| Symbol | libc dep | Port note |
|---|---|---|
| hexa_host_ffi_open | dlopen | static LC_LOAD_DYLIB / DT_NEEDED at codegen; dlopen becomes NO-OP |
| hexa_host_ffi_sym | dlsym | symbol lookup via PLT (set up by loader) |
| hexa_host_ffi_call* (call/_6/_14) | libffi-ish | asm trampoline per signature |
| hexa_extern_call / extern_call_typed | libffi-ish | same |
| hexa_cstring / from_cstring | — | pure (null-term coerce) |
| hexa_ffi_extract_libname | strdup | pure string slice |
| hexa_callback_create / free / slot_id / get_fn | mmap RWX | trampoline pool with mmap+mprotect |
| hexa_trampoline_init | mmap/mprotect | same |
| hexa_set_callback_dispatcher | — | static fn ptr |

**~12 symbols. Effort: very high (trampoline JIT is its own beast). Option: drop dynamic FFI for self-host core; keep only static calls via codegen — ML training paths only need libhxblas etc at codegen time.**

**Decision needed**: is runtime FFI needed by hexa_full.hexa compiler itself? If NO → skip until post-P7-10.

---

## Category G — Math (libm) (P7-7h)

**Target**: `self/rt/math.hexa` (pure) or link libm as last C dependency.

| Symbol | libm dep | Port note |
|---|---|---|
| hexa_math_sqrt / pow / floor / ceil / abs / round | libm | sqrt/pow: Newton iterations; floor/ceil/abs/round: bit ops |
| hexa_math_sin / cos / tan / asin / acos / atan / atan2 | libm | Taylor / CORDIC |
| hexa_math_tanh / log / exp | libm | pure approximations |
| hexa_math_min / max | fmin/fmax | pure (2-arg compare) |
| _hexa_f (float coerce helper) | — | pure |

**~16 symbols. Effort: medium (bounded). Alternative: keep libm as terminal C dep — libm is standardized and present on every Unix, may be acceptable even in "zero-C" mode if we treat it as kernel-level.**

**Decision needed**: libm = allowed or not? Strict zero-C says no. Practical answer: pure-hexa implementations exist (rust libm crate-style). Target ≤2,000 LOC hexa math.

---

## Category H — Error / exceptions (P7-7i)

**Target**: `self/rt/except.hexa`. setjmp/longjmp → arch-specific context save/restore via inline asm or @syscall-like intrinsic.

| Symbol | libc dep | Port note |
|---|---|---|
| __hexa_try_push / try_pop / try_cleanup | setjmp | @save_context intrinsic per arch (stores callee-save regs) |
| hexa_throw | longjmp | @restore_context intrinsic |
| __hexa_last_error / __hexa_error_val | — | thread-local → global (single-thread rt) |

**~5 symbols. Effort: medium. Blocker: @save_context / @restore_context intrinsic (new, not @syscall).**

---

## Category I — Init / misc

| Symbol | libc dep | Port note |
|---|---|---|
| _hexa_init_stdio | setvbuf | remove |
| _hexa_init_fn_shims | — | init hook table (pure) |
| _hexa_init_net_fn_shims | — | same (body in net.c — separate port) |
| hexa_ic_dump_stats | printf | format + write |
| _hx_stats_on / _hx_stats_dump | printf | same |
| hexa_matvec | — | pure (BLAS fallback) |

---

## Port order (topological)

```
P7-7a @syscall intrinsic design          (unblocks all below)
  │
  ├─ Category B alloc           → rt/alloc.hexa, rt/arena.hexa
  │    └─ Category A pure ops   → stdlib refactor (can run parallel to B)
  │
  ├─ Category C I/O             → rt/io.hexa, rt/fmt.hexa
  │    └─ Category E fs         → rt/fs.hexa (small, merge into C)
  │
  ├─ Category D proc/env        → rt/proc.hexa
  │
  ├─ Category H except          → rt/except.hexa (needs @save_context intrinsic)
  │
  ├─ Category G math            → rt/math.hexa (or keep libm)
  │
  └─ Category F FFI             → rt/ffi.hexa (optional for P7-7; defer if not blocking)
```

## Open questions / decisions

1. **libm as terminal C dep?** Strict: no → Category G pure-hexa. Practical: yes → ~2,000 LOC saved, fixpoint deterministic, but 1 libc dep remains.
2. **Dynamic FFI (Category F) required for self-host core?** Audit: does `hexa_full.hexa` + `compile_pipeline.hexa` call any `ffi_*` fn during its own compilation? If no → defer to P7-11.
3. **Trampolines for callbacks (Category F)**: if kept, requires JIT-via-mmap which resurrects a C-ish code path (mprotect RWX). Can we replace with direct ARM64/x86 codegen emit at compile time?
4. **Threading**: runtime.c is single-threaded → no pthread. Good — no port needed.
5. **setjmp/longjmp (Category H)**: can be replaced with tagged Result return type (Rust-style) if we refactor throw sites. Bigger refactor but removes arch-specific context save.

## Totals

- Total C symbols: **~340** (many static helpers + duplicate forward decls)
- Uniquely portable: **~200** (rest are internal duplicates, arena bookkeeping, fwd decls)
- Category A (easy): 110 symbols, ~1,500 hexa LOC
- Category B (alloc): 25 symbols, ~800 hexa LOC
- Category C+E (I/O+fs): 19 symbols, ~1,200 hexa LOC
- Category D (proc): 10 symbols, ~400 hexa LOC
- Category F (FFI): 12 symbols, ~1,500 hexa LOC (optional)
- Category G (math): 16 symbols, ~2,000 hexa LOC (or libm-linked ~50 LOC)
- Category H (except): 5 symbols, ~200 hexa LOC
- **Estimate**: ~6,000–8,000 hexa LOC total (vs. 4,813 C LOC). Larger because hexa verbose + missing macros.

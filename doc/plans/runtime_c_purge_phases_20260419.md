# runtime.c Purge Phases — ROI #169 Refinement (2026-04-19)

**Scope**: Refine Phase A/B/C/D into executable steps.
**Parent**: `runtime_c_purge.md` (DAG, 313-symbol table).
**Status**: Phase A done. Gates #152 / #153 / #162 all `done` → **Phase B/C/D unblocked**.
**Policy**: total purge — no `@hot_kernel` exception (`feedback_c_total_purge_after_fixpoint.md`).

---

## Target surface

| File | LOC | Phase |
|---|---:|---|
| `self/runtime.c` | 6,331 | B (all 313 symbols) |
| `self/native/*.c` × 16 | 24,568 | C |
| `self/bootstrap_compiler.c` | 1,493 | D |
| **Total C** | **32,392** | → **0** |

`self/rt/*.hexa` already landed: core, alloc, array_ops, convert, except, fmt, fs, intern, io, json, map_ops, math, math_abi, proc, string, string_abi, syscall (17 modules + 16 test files). Missing: `tensor.hexa`, `net.hexa`, `ffi.hexa`, `encoding.hexa`, `valstruct.hexa`.

---

## Phase A — Inventory (DONE 2026-04-19)

Delivered in `runtime_c_purge.md` §1, §4:
- 313 `hexa_*` + 11 `_hx*/_hexa_*` symbols categorised (A–H + Z).
- Refcount table (`ext` callsites / `int` self-ref).
- 8-wave DAG; Wave-0 dead-leaves identified (11 symbols / ~15 LOC).

Exit gate: **met** (ROI #169 closed).

---

## Phase B — runtime.c 박멸 (hexa-only replace)

Execute Wave 0 → 7 from `runtime_c_purge.md` §2. Each wave = 1 commit + `hexa tool/rebuild_stage0.hexa` fixpoint 4/4 IDENTICAL + anima t1/t2 regression gate.

| Wave | Symbols | Target module(s) | Pre-req | Hours |
|---|---:|---|---|---:|
| B0 dead-strip | 11 | — (drop) | none | 2 |
| B1a pure-leaf wire-up | ~160 | rt/{string, array_ops, map_ops, core, math, fmt} | B0 | 6 |
| B1b new pure modules | ~40 | rt/{json, encoding, valstruct, tensor} **new** | B1a | 10 |
| B2 alloc/ptr/closure | 33 | rt/alloc + codegen intrinsic | `@syscall(mmap)` | 10 |
| B3 fmt/io | 24 | rt/fmt, rt/io (exist — wire) | B2 + `@syscall(write/open/read/close/unlink/fstat)` | 6 |
| B4 proc/net | 23 | rt/proc (exist), rt/net **new** | `@syscall(fork/execve/wait4/…/socket/…)` | 8 |
| B5 except | 2 | rt/except (exist) | `@save_context` / `@restore_context` intrinsic | 4 |
| B6 ffi | 19 | rt/ffi **new** (optional — static-link path) | codegen DT_NEEDED | 6 (defer P7-11) |
| B7 math (libm) | 18 | rt/math (exist, libm-shim) → pure hexa | Option α strict | 8 |
| **B-total** | **313** | | | **~60h** |

**Verify per wave**: fixpoint 4/4, rt/ tests 100%, anima t1/t2 no regression, `grep -c "hexa_" runtime.c` decreases monotonically to 0.

---

## Phase C — self/native/*.c 박멸

Triggered after Phase B-Wave 7 + #162 closure (done). 16 files / 24,568 LOC.

| Group | Files | LOC | Replacement |
|---|---|---:|---|
| Frontend / codegen | hexa_cc, parser_v2, lexer_v2, type_checker_v2, codegen_c2_v2, gpu_codegen_stub | 20,892 | `self/hexa_full.hexa` + `self/{parser,lexer,type_checker}.hexa` + `self/codegen/` |
| Legacy I/O | net.c | 175 | `rt/net.hexa` (B4) |
| @hot_kernel (no exception) | hxblas, hxccl, hxflash, hxlayer, hxlmhead, hxqwen14b, hxvdsp, hxvocoder, tensor_kernels | 3,501 | pure-hexa NEON/SVE kernels (ROI #139); `vocoder.hexa` already at 1,922× |

**Exit**: `self/native/` → 0 files. Rebuild gate: T2 training loop throughput ≥ pre-purge × 0.95.

---

## Phase D — bootstrap_compiler.c 박멸

Single file, 1,493 LOC. Gate: `hexa_v2` can rebuild its own seed on-disk (pre-reqs #152 + #153 — both done). Remove `.c`, replace `hexa tool/rebuild_stage0.hexa` seed input with prior `hexa_v2` binary. Verify fixpoint 4/4 without any `.c` in tree.

**Exit**: `find self/ -name '*.c' | wc -l` = 0.

---

## Verification ladder (every wave)

1. `hexa tool/rebuild_stage0.hexa` → 4/4 IDENTICAL
2. `hexa run self/rt/test_*.hexa` → green
3. `anima t1 && anima t2` → physical-ceiling regression < 1 %
4. `wc -l self/runtime.c` monotonic ↓
5. Commit + push; no hooks skipped.

## Open decisions (carry from parent §6)

- libm Option α strict enforced (no C exception).
- Dynamic FFI (B6) deferred to P7-11 unless `hexa_full.hexa` imports it.
- `setjmp/longjmp` → `@save_context` intrinsic (not Result refactor).
- Trampoline RWX mmap replaced by compile-time codegen emit.

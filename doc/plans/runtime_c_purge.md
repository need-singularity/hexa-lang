# runtime.c Purge DAG & Symbol Map — ROI #169

**Date**: 2026-04-19
**Scope**: design only (실제 실행은 P7-9 fixpoint 직후)
**Target**: `self/runtime.c` (6,331 LOC, 313 `hexa_*` + 11 `_hx/_hexa_*` = 324 export symbols)
**Policy**: purge 시 `@hot_kernel` 예외 없음 (memory `feedback_c_total_purge_after_fixpoint.md`)
**Related**: `runtime_c_inventory.md` (포팅 카탈로그), `runtime_c_purge_audit.md` (2026-04-19 1-wave dead-strip)

---

## 1. 요약 — 카테고리 분포 (hexa_* 313 심볼)

| 카테고리 | 심볼수 | 대체 전략 | 타겟 모듈 |
|---|---:|---|---|
| A-value-ops | 56 | pure-fn | `self/rt/core.hexa` + 자동 intrinsic |
| A-pure-array | 49 | pure-fn | `self/rt/array_ops.hexa` |
| A-pure-string | 37 | pure-fn | `self/rt/string.hexa` |
| A-pure-map | 19 | pure-fn | `self/rt/map_ops.hexa` |
| A-tensor | 16 | pure-fn | `self/rt/tensor.hexa` (신규) |
| A-util | 3 | pure-fn | `self/rt/core.hexa` |
| A-valstruct | 4 | pure-fn | `self/rt/core.hexa` |
| A-encoding (bin/hex/b64) | 4 | pure-fn | `self/rt/string.hexa` |
| A-json | 4 | pure-fn | `self/rt/json.hexa` |
| B-alloc (arena/intern/heapify) | 16 | intrinsic+pure | `self/rt/alloc.hexa` + `@syscall(mmap)` |
| B-closure (call0..4) | 9 | intrinsic | codegen 특수 케이스 (direct emit) |
| B-ptr (ptr_alloc/read/write/…) | 8 | intrinsic | codegen 특수 케이스 + `@syscall` |
| C-fmt (to_string/format/println/…) | 9 | pure-fn + `@syscall(write)` | `self/rt/fmt.hexa` |
| C-io-fs (read_file/write_bytes/…) | 15 | `@syscall(open/read/write/unlink/fstat)` | `self/rt/io.hexa` + `fs.hexa` |
| D-proc-env (exec/exit/args/clock/…) | 16 | `@syscall(fork/execve/wait4/getrandom/…)` | `self/rt/proc.hexa` |
| D-net | 7 | `@syscall(socket/bind/listen/accept/…)` | `self/rt/net.hexa` (신규) |
| F-ffi (host_ffi/extern_call/trampoline/…) | 19 | intrinsic+codegen (정적 링크) | `self/rt/ffi.hexa` (옵셔널) |
| G-math (libm 래퍼 18) | 18 | pure-fn (또는 libm terminal-dep) | `self/rt/math.hexa` (존재) |
| H-except (throw/is_error) | 2 | intrinsic `@save_context`/`@restore_context` | `self/rt/except.hexa` (존재) |
| Z-ic (dev-only stats) | 2 | drop (leaf-dead, 즉시 제거) | — |
| **합계** | **313** | | |

레거시 `_hx_stats_*` (5) + `_hexa_init_*` (4) + `_hx_bit_or` (1) + `_hexa_f` (1) = 11 underscore 심볼도 같은 DAG 에 포함 (주로 init / 통계 훅).

---

## 1.5. Phase A–D 요약 (ROI #169 태스크 포맷)

내부 Wave 0-7 구조를 태스크 요구 Phase A/B/C/D 로 재매핑:

### Phase A — 인벤토리 (완료, 이 문서 섹션 1 + 섹션 4)

`self/runtime.c` = **6,331 LOC / 313 hexa_* + 11 underscore 심볼 = 324 export**. 카테고리:

| 카테고리 | 심볼수 | 의미 |
|---|---:|---|
| math (G-math) | 18 | libm 래퍼 (sin/cos/sqrt/…) |
| fmt (C-fmt) | 9 | to_string/format/println/print_val/… |
| proc (D-proc-env) | 16 | exec/exit/args/clock/random/sleep/timestamp/env_var/… |
| syscall / fs (C-io-fs) | 15 | read_file/write_file/write_bytes/file_size/stdin/… |
| io (C-fmt + fs subset) | 9+15 | 24 총 I/O exit points |
| net (D-net) | 7 | socket/bind/listen/accept/http_get |
| except (H-except) | 2 | throw/is_error |
| value-ops (A-value-ops) | 56 | add/sub/mul/div/eq/cmp/truthy/int/float/… |
| array (A-pure-array) | 49 | push/new/get/map/filter/sort/… |
| string (A-pure-string) | 37 | str/split/trim/replace/lines/… |
| map (A-pure-map) | 19 | map_get/set/keys/values/… |
| tensor (A-tensor) | 16 | matmul/rms_norm/softmax/swiglu/… |
| encoding + json (A-encoding + A-json) | 8 | bin/hex/base64/json_{encode,decode} |
| util + valstruct + struct pack (A-util + A-valstruct) | 7 | fnv1a/sort_cmp/valstruct_* |
| alloc (B-alloc) | 16 | arena/intern/heapify/snapshot |
| ptr (B-ptr) | 8 | ptr_alloc/read/write/offset/null |
| closure (B-closure) | 9 | call0..4/fn_new/closure_new/iter_get |
| ffi (F-ffi) | 19 | host_ffi_*/extern_call/dlopen/dlsym/trampoline |
| ic-dev (Z-ic) | 2 | ic_dump_stats/ic_stats_on (dev-only) |
| **TOTAL** | **313** | |

### Phase B — 심볼 → 대체 경로 매핑 (섹션 4 전체 테이블)

| 대체 전략 | 심볼수 | 타겟 모듈 | 상태 |
|---|---:|---|---|
| pure-fn (이미 `self/rt/` 존재) | ~160 | `rt/{core,array_ops,string,map_ops,tensor,math,fmt}.hexa` | 포팅 부분 완료 |
| pure-fn (신규 포팅 필요) | ~40 | `rt/{json,encoding,valstruct,struct_pack}.hexa` (신규) | 미착수 |
| intrinsic + codegen 직접 emit | ~35 | `self/codegen/` (P7-7 native) | 설계 완료 |
| `@syscall(…)` 기반 | ~40 | `rt/{io,fs,proc,net,except}.hexa` | 설계 완료, intrinsic 구현 필요 |
| drop (dead leaf) | ~15 | — | Wave 0 즉시 |
| **합계** | **~290 + 23 (rounded)** = 313 | | |

각 심볼의 ext/int refcount + 카테고리 + 전략 → 섹션 4 의 테이블 (313 rows).

### Phase C — `self/native/*.c` 처분 (P7-7 fixpoint 시점)

| 파일 | LOC | 운명 | 대체 |
|---|---:|---|---|
| `hexa_cc.c` | 13,308 | **완전 삭제** | `self/hexa_full.hexa` (self-hosted parser/lexer/typer) + native codegen (P7-7) 완료 시 |
| `codegen_c2_v2.c` | 1,996 | **완전 삭제** | `self/codegen/` ARM64/x86_64 네이티브 백엔드 (ROI #162 P7-6a) |
| `parser_v2.c` | 3,169 | **완전 삭제** | `self/parser.hexa` |
| `lexer_v2.c` | 758 | **완전 삭제** | `self/lexer.hexa` |
| `type_checker_v2.c` | 1,697 | **완전 삭제** | `self/type_checker.hexa` (포팅 필요) |
| `net.c` | 175 | **완전 삭제** | `self/rt/net.hexa` (Wave 4, `@syscall`) |
| `hxblas_linux.c` | 672 | **@hot_kernel drop** | pure-hexa NEON/SVE kernel (ROI #139) |
| `hxccl_linux.c` | 403 | **@hot_kernel drop** | pure-hexa MPI/gloo 대체 |
| `hxflash_linux.c` | 579 | **@hot_kernel drop** | pure-hexa flash-attn |
| `hxlayer_linux.c` | 93 | **@hot_kernel drop** | pure-hexa layer fusion |
| `hxlmhead_linux.c` | 293 | **@hot_kernel drop** | pure-hexa LM head |
| `hxqwen14b.c` | 342 | **@hot_kernel drop** | pure-hexa Qwen weight loader |
| `hxvdsp_linux.c` | 226 | **@hot_kernel drop** | pure-hexa vDSP |
| `hxvocoder.c` | 455 | **@hot_kernel drop** | `self/vocoder.hexa` (이미 1,922x native 달성) |
| `tensor_kernels.c` | 138 | **@hot_kernel drop** | pure-hexa kernel |
| `gpu_codegen_stub.c` | 264 | **완전 삭제** | `self/codegen/gpu.hexa` |
| **합계** | **24,568** | | 메모리 `feedback_c_total_purge_after_fixpoint.md`: @hot_kernel 예외 없음 |

### Phase D — `self/bootstrap_compiler.c` 처분 (hexa_v2 circular fix 시점)

| 파일 | LOC | 운명 | Gate |
|---|---:|---|---|
| `self/bootstrap_compiler.c` | 1,493 | **완전 삭제** | hexa_v2 on-disk 가 스스로를 재빌드 가능 → bootstrap `.c` seed 불필요 |

---

## 1.6. Gate conditions (phase prerequisites)

| Phase | Gate (ROI) | 의미 |
|---|---|---|
| **Phase A** (inventory) | — | 이 문서. **완료** (2026-04-19). |
| **Phase B** (port + drop) | **#152 IC-slot fix** completed | hexa_v2 circular rebuild 가 정상화되어야 rt/ 모듈 증분 테스트 가능 (현재 seed freeze — `project_hexa_v2_circular_rebuild_20260417.md`) |
| **Phase B.Wave 2+** | **@syscall intrinsic** (P7-7a) | `self/codegen/` 에 `@syscall(name, arg0..5) → int64` lowering 필수 |
| **Phase C** (native/*.c drop) | **#153 P7-7 v3==v4 fixpoint** closure | hexa_full.hexa → native 컴파일러가 자기 자신을 빌드하는 fixpoint 닫힘 |
| **Phase C.@hot_kernel** | **#162 P7-6a native gap** closed | ARM64/x86_64 pure-hexa kernel 이 hxblas/hxflash/… 성능 회복 |
| **Phase D** (bootstrap_compiler.c) | **#152 fix** + **#153 fixpoint** 모두 | hexa_v2 가 circular rebuild 로 자기 seed 생성 가능 |

**Cross-ref**:
- ROI **#152** (hexa_v2 IC-slot circular rebuild fix): seed freeze 해제. Phase B/D 의 전제.
- ROI **#153** (P7-7 v3==v4 fixpoint closure): hexa-only 컴파일러 수렴. Phase C/D 의 전제.
- ROI **#162** (P7-6a hexa_full native gap analysis): `.c` → native 경로 확보. Phase C 의 @hot_kernel 교체 전제.

---

## 2. Purge DAG (ASCII)

```
┌───────────────────────────────────────────────────────────────────┐
│                      PRE-REQ (P7-7a/b 완료)                       │
│   @syscall intrinsic  +  mmap/munmap arena  +  direct-clang link  │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│  Wave 0 : DEAD LEAVES (no external ref, no load-bearing)          │
│    Z-ic (2)    +  arena_scope/snapshot helpers (4)                │
│    arena new_block/alloc_items (2)                                │
│    add_slow / map_get_ic_slow / sort_cmp (3)                      │
│    trampoline_dispatch/init (2, F-ffi defer 시 drop)              │
│    closure_env (1, call0..4 inline 화 후)                         │
│    → 즉시 drop 가능, 다른 심볼에 의존 없음                        │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│  Wave 1 : PURE LEAVES (A-*, G-math) — ext refs 존재하지만        │
│           runtime 내부 의존은 value-ops core 뿐                   │
│                                                                    │
│   A-encoding ──┐                                                  │
│   A-json     ──┼──► A-pure-string ──┐                             │
│   A-util     ──┘                    │                             │
│                                     ▼                             │
│   A-tensor  ──► A-pure-array ──► A-pure-map ──► A-value-ops       │
│                                                    │              │
│   A-valstruct ◄────────────────────────────────────┤              │
│   A-struct (pack/unpack) ◄─────────────────────────┘              │
│                                                                    │
│   G-math  (독립, libm 링크만 필요 — 최후에 수렴)                  │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│  Wave 2 : MEMORY/CLOSURE (B-alloc, B-ptr, B-closure)              │
│           @syscall(mmap) 필수 — rt/alloc.hexa 가 pivot            │
│                                                                    │
│   B-ptr (ptr_alloc/free/…) ──► @syscall(mmap/munmap)              │
│   B-alloc (arena/intern/heapify) ──► B-ptr                        │
│   B-closure (call0..4/fn_new/closure_new) ──► B-ptr               │
│           └── closure_env 은 Wave 0 에서 이미 제거                │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│  Wave 3 : I/O & FORMAT (C-fmt, C-io-fs)                           │
│                                                                    │
│   C-fmt (to_string/format/println) ──► A-pure-string (Wave 1)     │
│                                   └──► @syscall(write)            │
│   C-io-fs (read_file/write_bytes/file_size) ──► B-ptr (Wave 2)    │
│                                             └──► @syscall(open/   │
│                                                   read/write/     │
│                                                   close/unlink/   │
│                                                   fstat)          │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│  Wave 4 : PROCESS / NET (D-proc-env, D-net)                       │
│                                                                    │
│   D-proc-env (exec/exit/args/clock/random)                        │
│       ──► C-io-fs (Wave 3, pipe read/write)                       │
│       ──► @syscall(fork/execve/wait4/nanosleep/clock_gettime/     │
│                    getrandom/pipe2/dup2/exit_group)               │
│   D-net (socket/bind/listen/accept/…)                             │
│       ──► @syscall(socket/bind/listen/accept4/send/recv/close)    │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│  Wave 5 : EXCEPTIONS (H-except)                                   │
│                                                                    │
│   H-except (throw/is_error) ──► @save_context / @restore_context │
│   (arch-specific intrinsic — ARM64 stp/ldp callee-save regs,     │
│    x86_64 equivalent). 신규 intrinsic — `@syscall` 아님.          │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│  Wave 6 : FFI (F-ffi, optional)                                   │
│                                                                    │
│   F-ffi (host_ffi_open/sym/call/call_6/extern_call/callback/     │
│          trampoline) ──► B-alloc(rwx mmap) + codegen DT_NEEDED    │
│                                                                    │
│   DECISION: runtime dlopen 은 hexa_full.hexa 가 자체적으로는      │
│             안 부름 → P7-11 로 지연 가능. 정적 링크만 지원하면    │
│             Wave 6 통째로 drop 가능 (19 심볼 즉시 죽음).           │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌───────────────────────────────────────────────────────────────────┐
│  Wave 7 : G-math (libm terminal-dep)                              │
│                                                                    │
│   Option α (strict): 18 심볼 모두 pure-hexa 포팅 (~2000 LOC)      │
│                      → C 의존 0                                    │
│   Option β (pragmatic): libm 을 kernel-level dep 로 유지          │
│                      → 18 심볼은 shim 으로만 남김 (~50 LOC C)     │
│                                                                    │
│   CLAUDE.md 정책: C 박멸 = 예외 없음 → Option α 강제.             │
└───────────────────────────────────────────────────────────────────┘
                              │
                              ▼
                ┌───────────────────────────┐
                │  runtime.c = 0 LOC        │
                │  bootstrap_compiler.c = 0 │
                │  self/native/*.c = 0      │
                └───────────────────────────┘
```

### DAG 깊이

- Root (Wave 0, dead leaves) → Terminal (Wave 7, libm 포팅) = **8 레이어**.
- 최장 경로: `Wave 6 F-ffi.host_ffi_call` → `Wave 2 B-alloc.val_arena_on` → `B-ptr.ptr_alloc` → `@syscall(mmap)` → Wave 0 (no-dep) = **depth 5** 기능 의존.

### 병렬 가능 웨이브

- Wave 1 내부 A-* 카테고리는 전부 value-ops 만 의존 → 동시 포팅 가능 (이미 `self/rt/` 에 대부분 landing).
- Wave 3 fmt/io 는 상호 독립 (fmt 은 string 만 필요, io 는 ptr 만 필요) → 병렬.
- Wave 4 proc 와 net 은 완전 독립 → 병렬.

---

## 3. 1st-wave 10 leaf 심볼 (즉시 drop 가능)

이 10개는 **ext=0 이고 내부 refcount 도 낮거나 trivial 포팅 대상**. Sketch 는 `self/purge/runtime_sketches.hexa`.

| # | 심볼 | runtime.c 위치 | ext/int | 현 역할 | .hexa 대체 전략 |
|---:|---|---:|:---:|---|---|
| 1 | `hexa_ic_dump_stats` | L1469–1478 | 0/2 | dev-only IC hit-rate printf | **drop 전체** (HEXA_IC_STATS env-gated, prod 불필요) |
| 2 | `hexa_ic_stats_on` | L1480–1490 | 0/2 | env guard + atexit | **drop 전체** (Z-ic) |
| 3 | `hexa_add_slow` | — | 0/2 | `hexa_add` slow-path fallback | inline 후 drop (A-value-ops, 그대로 포팅) |
| 4 | `hexa_sort_cmp` | L2624–2629 | 0/2 | qsort compare (static) | `rt/array_ops.hexa` 에 이미 포팅된 timsort 내부 compare → drop |
| 5 | `hexa_str_own` | L649–651 | 0/36 | `(HexaVal){.tag=TAG_STR,.s=s}` 1-liner | intrinsic `@make_str(ptr)` 로 교체 — 36 내부 callsite 는 codegen 에서 직접 emit |
| 6 | `hexa_str_as_ptr` | — | 0/6 | `str → char*` unwrap | intrinsic `@str_ptr(s)` — ptr 카테고리로 이동 |
| 7 | `hexa_closure_env` | L489–492 | 0/6 | closure env 배열 extract | `call0..4` inline 후 dead (Wave 0) |
| 8 | `hexa_array_count` | L4794–4801 | 0/3 | `filter().len()` 동치 | pure-fn, 3줄 (rt/array_ops.hexa 에 이미 있으면 그대로 drop) |
| 9 | `hexa_array_reserve` | L943–960 | 0/3 | `realloc(cap)` helper | B-alloc pivot — `rt/alloc.hexa.array_reserve` 로 이관 |
| 10 | `hexa_array_shallow_clone` | L873–900 | 0/3 | ROI #148 alias-break helper | pure-fn (rt/array_ops.hexa) |

**즉시 drop 가능 (Wave 0 일부)**: #1, #2, #4, #7 (총 4 심볼 ~15 LOC)
**포팅 후 drop**: #3, #5, #6, #8, #9, #10 (총 6 심볼 ~60 LOC)

---

## 4. 전체 심볼 테이블 (ext/int refcount + 카테고리)

ext=외부 (self/·tool/·shared/·examples/ 의 `.hexa/.c/.h/.json`) 참조 횟수 — `runtime.c` 제외.
int=`runtime.c` 내부 참조 (self-decl 포함). ext=0 AND int<=5 = Wave 0 후보.

| Sym | ext | int | Cat | Strategy |
|---|---:|---:|---|---|
| hexa_bin | 58 | 1 | A-encoding | pure-fn |
| hexa_hex | 11 | 1 | A-encoding | pure-fn |
| hexa_base64_encode | 7 | 2 | A-encoding | pure-fn |
| hexa_base64_decode | 6 | 2 | A-encoding | pure-fn |
| hexa_json_decode | 5 | 1 | A-json | pure-fn |
| hexa_json_encode | 5 | 1 | A-json | pure-fn |
| hexa_json_parse | 5 | 2 | A-json | pure-fn |
| hexa_json_stringify | 5 | 2 | A-json | pure-fn |
| hexa_array_push | 1901 | 79 | A-pure-array | pure-fn |
| hexa_array_new | 1403 | 73 | A-pure-array | pure-fn |
| hexa_array_truncate | 25 | 1 | A-pure-array | pure-fn |
| hexa_array_get | 18 | 28 | A-pure-array | pure-fn |
| hexa_array_pop | 13 | 1 | A-pure-array | pure-fn |
| hexa_array_set | 10 | 2 | A-pure-array | pure-fn |
| hexa_array_reverse | 9 | 1 | A-pure-array | pure-fn |
| hexa_array_slice | 9 | 2 | A-pure-array | pure-fn |
| hexa_array_slice_fast | 9 | 2 | A-pure-array | pure-fn |
| hexa_array_sort | 9 | 1 | A-pure-array | pure-fn |
| hexa_array_push_nostat | 6 | 2 | A-pure-array | pure-fn |
| hexa_array_index_of | 4 | 2 | A-pure-array | pure-fn |
| hexa_array_any | 3 | 1 | A-pure-array | pure-fn |
| hexa_array_filter | 3 | 2 | A-pure-array | pure-fn |
| hexa_array_fold | 3 | 2 | A-pure-array | pure-fn |
| hexa_array_map | 3 | 3 | A-pure-array | pure-fn |
| hexa_array_partition | 3 | 1 | A-pure-array | pure-fn |
| hexa_array_all | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_chunk | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_drop | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_enumerate | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_fill | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_find | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_flat_map | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_flatten | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_for_each | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_frequencies | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_group_by | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_interleave | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_max | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_mean | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_min | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_product | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_rotate | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_sample | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_scan | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_swap | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_take | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_unique | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_window | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_zip | 2 | 1 | A-pure-array | pure-fn |
| hexa_array_contains | 1 | 2 | A-pure-array | pure-fn |
| hexa_array_arena_alloc_items | 0 | 4 | A-pure-array | pure-fn |
| hexa_array_arena_on | 0 | 5 | A-pure-array | pure-fn |
| hexa_array_count | 0 | 3 | A-pure-array | pure-fn |
| hexa_array_promote_to_heap | 0 | 6 | A-pure-array | pure-fn |
| hexa_array_push_arena_on | 0 | 2 | A-pure-array | pure-fn |
| hexa_array_reserve | 0 | 3 | A-pure-array | pure-fn |
| hexa_array_shallow_clone | 0 | 3 | A-pure-array | pure-fn |
| hexa_map_get_ic | 2898 | 2 | A-pure-map | pure-fn |
| hexa_map_get | 832 | 9 | A-pure-map | pure-fn |
| hexa_map_set | 580 | 12 | A-pure-map | pure-fn |
| hexa_map_new | 533 | 9 | A-pure-map | pure-fn |
| hexa_map_contains_key | 14 | 3 | A-pure-map | pure-fn |
| hexa_map_keys | 10 | 4 | A-pure-map | pure-fn |
| hexa_map_values | 10 | 1 | A-pure-map | pure-fn |
| hexa_map_remove | 8 | 1 | A-pure-map | pure-fn |
| hexa_map_from_array | 4 | 1 | A-pure-map | pure-fn |
| hexa_map_entries | 2 | 2 | A-pure-map | pure-fn |
| hexa_map_filter_keys | 2 | 1 | A-pure-map | pure-fn |
| hexa_map_invert | 2 | 1 | A-pure-map | pure-fn |
| hexa_map_map_values | 2 | 1 | A-pure-map | pure-fn |
| hexa_map_merge | 2 | 1 | A-pure-map | pure-fn |
| hexa_map_to_array | 2 | 1 | A-pure-map | pure-fn |
| hexa_map_all | 1 | 4 | A-pure-map | pure-fn |
| hexa_map_any | 1 | 4 | A-pure-map | pure-fn |
| hexa_map_count | 1 | 3 | A-pure-map | pure-fn |
| hexa_map_get_ic_slow | 0 | 2 | A-pure-map | pure-fn |
| hexa_str | 8615 | 79 | A-pure-string | pure-fn |
| hexa_str_join | 167 | 3 | A-pure-string | pure-fn |
| hexa_str_chars | 54 | 2 | A-pure-string | pure-fn |
| hexa_str_substring | 49 | 2 | A-pure-string | pure-fn |
| hexa_str_trim | 37 | 2 | A-pure-string | pure-fn |
| hexa_str_contains | 30 | 2 | A-pure-string | pure-fn |
| hexa_str_replace | 30 | 2 | A-pure-string | pure-fn |
| hexa_str_to_upper | 25 | 2 | A-pure-string | pure-fn |
| hexa_str_bytes | 24 | 2 | A-pure-string | pure-fn |
| hexa_str_starts_with | 24 | 1 | A-pure-string | pure-fn |
| hexa_str_split | 23 | 4 | A-pure-string | pure-fn |
| hexa_str_index_of | 21 | 1 | A-pure-string | pure-fn |
| hexa_str_ends_with | 16 | 1 | A-pure-string | pure-fn |
| hexa_str_parse_float | 16 | 2 | A-pure-string | pure-fn |
| hexa_str_parse_int | 16 | 3 | A-pure-string | pure-fn |
| hexa_struct_pack_map | 16 | 3 | A-pure-string | pure-fn |
| hexa_str_to_lower | 11 | 2 | A-pure-string | pure-fn |
| hexa_str_concat | 10 | 4 | A-pure-string | pure-fn |
| hexa_str_repeat | 10 | 2 | A-pure-string | pure-fn |
| hexa_str_trim_end | 8 | 2 | A-pure-string | pure-fn |
| hexa_str_trim_start | 8 | 2 | A-pure-string | pure-fn |
| hexa_str_eq | 5 | 1 | A-pure-string | pure-fn |
| hexa_str_lines | 5 | 1 | A-pure-string | pure-fn |
| hexa_str_slice | 5 | 2 | A-pure-string | pure-fn |
| hexa_str_center | 4 | 1 | A-pure-string | pure-fn |
| hexa_struct_unpack | 4 | 1 | A-pure-string | pure-fn |
| hexa_str_pad_left | 3 | 1 | A-pure-string | pure-fn |
| hexa_str_pad_right | 3 | 1 | A-pure-string | pure-fn |
| hexa_str_substr | 3 | 1 | A-pure-string | pure-fn |
| hexa_struct_free | 3 | 1 | A-pure-string | pure-fn |
| hexa_struct_pack | 3 | 1 | A-pure-string | pure-fn |
| hexa_struct_point | 3 | 1 | A-pure-string | pure-fn |
| hexa_struct_rect | 3 | 1 | A-pure-string | pure-fn |
| hexa_struct_size_pack | 3 | 1 | A-pure-string | pure-fn |
| hexa_str_count_substr | 1 | 3 | A-pure-string | pure-fn |
| hexa_str_as_ptr | 0 | 6 | A-pure-string | pure-fn |
| hexa_str_own | 0 | 36 | A-pure-string | pure-fn |
| hexa_matmul | 13 | 2 | A-tensor | pure-fn |
| hexa_tensor_add | 6 | 2 | A-tensor | pure-fn |
| hexa_tensor_mul_scalar | 6 | 2 | A-tensor | pure-fn |
| hexa_tensor_ones | 5 | 2 | A-tensor | pure-fn |
| hexa_tensor_slice | 5 | 2 | A-tensor | pure-fn |
| hexa_tensor_zeros | 5 | 2 | A-tensor | pure-fn |
| hexa_rms_norm | 4 | 2 | A-tensor | pure-fn |
| hexa_softmax | 4 | 2 | A-tensor | pure-fn |
| hexa_swiglu_vec | 4 | 2 | A-tensor | pure-fn |
| hexa_tensor_dot | 4 | 2 | A-tensor | pure-fn |
| hexa_matvec | 3 | 2 | A-tensor | pure-fn |
| hexa_argmax | 2 | 1 | A-tensor | pure-fn |
| hexa_gelu | 2 | 1 | A-tensor | pure-fn |
| hexa_hadamard | 2 | 1 | A-tensor | pure-fn |
| hexa_one_hot | 2 | 1 | A-tensor | pure-fn |
| hexa_silu | 2 | 1 | A-tensor | pure-fn |
| hexa_fnv1a | 2 | 2 | A-util | pure-fn |
| hexa_fnv1a_str | 1 | 11 | A-util | pure-fn |
| hexa_sort_cmp | 0 | 2 | A-util | pure-fn |
| hexa_valstruct_new_v | 5 | 3 | A-valstruct | pure-fn |
| hexa_valstruct_set_by_key | 2 | 3 | A-valstruct | pure-fn |
| hexa_valstruct_get_by_key | 0 | 3 | A-valstruct | pure-fn |
| hexa_valstruct_int | 0 | 5 | A-valstruct | pure-fn |
| hexa_truthy | 7396 | 19 | A-value-ops | pure-fn |
| hexa_int | 6491 | 115 | A-value-ops | pure-fn |
| hexa_eq | 5760 | 6 | A-value-ops | pure-fn |
| hexa_bool | 3751 | 64 | A-value-ops | pure-fn |
| hexa_add | 3345 | 7 | A-value-ops | pure-fn |
| hexa_index_get | 2507 | 4 | A-value-ops | pure-fn |
| hexa_void | 1637 | 51 | A-value-ops | pure-fn |
| hexa_len | 1609 | 3 | A-value-ops | pure-fn |
| hexa_float | 486 | 77 | A-value-ops | pure-fn |
| hexa_type_of | 464 | 2 | A-value-ops | pure-fn |
| hexa_sub | 436 | 2 | A-value-ops | pure-fn |
| hexa_cmp_lt | 263 | 4 | A-value-ops | pure-fn |
| hexa_mul | 231 | 2 | A-value-ops | pure-fn |
| hexa_div | 122 | 2 | A-value-ops | pure-fn |
| hexa_index_set | 79 | 1 | A-value-ops | pure-fn |
| hexa_cmp_gt | 67 | 3 | A-value-ops | pure-fn |
| hexa_mod | 42 | 2 | A-value-ops | pure-fn |
| hexa_to_cstring | 39 | 2 | A-value-ops | pure-fn |
| hexa_sqrt | 33 | 2 | A-value-ops | pure-fn |
| hexa_cmp_ge | 30 | 2 | A-value-ops | pure-fn |
| hexa_pow | 25 | 1 | A-value-ops | pure-fn |
| hexa_floor | 22 | 1 | A-value-ops | pure-fn |
| hexa_as_num | 18 | 3 | A-value-ops | pure-fn |
| hexa_abs | 16 | 2 | A-value-ops | pure-fn |
| hexa_char_code | 11 | 3 | A-value-ops | pure-fn |
| hexa_ceil | 10 | 1 | A-value-ops | pure-fn |
| hexa_fma | 10 | 2 | A-value-ops | pure-fn |
| hexa_from_char_code | 10 | 3 | A-value-ops | pure-fn |
| hexa_pad_left | 10 | 2 | A-value-ops | pure-fn |
| hexa_cmp_le | 8 | 2 | A-value-ops | pure-fn |
| hexa_count_poly | 7 | 1 | A-value-ops | pure-fn |
| hexa_to_float | 6 | 1 | A-value-ops | pure-fn |
| hexa_to_int | 6 | 1 | A-value-ops | pure-fn |
| hexa_concat_many | 5 | 3 | A-value-ops | pure-fn |
| hexa_dict_keys | 5 | 2 | A-value-ops | pure-fn |
| hexa_is_type | 5 | 2 | A-value-ops | pure-fn |
| hexa_pad_right | 5 | 2 | A-value-ops | pure-fn |
| hexa_sum | 5 | 2 | A-value-ops | pure-fn |
| hexa_to_bool | 5 | 1 | A-value-ops | pure-fn |
| hexa_byte_len | 4 | 2 | A-value-ops | pure-fn |
| hexa_log | 4 | 1 | A-value-ops | pure-fn |
| hexa_cos | 3 | 1 | A-value-ops | pure-fn |
| hexa_exp | 3 | 1 | A-value-ops | pure-fn |
| hexa_is_empty | 3 | 2 | A-value-ops | pure-fn |
| hexa_log10 | 3 | 1 | A-value-ops | pure-fn |
| hexa_null_coal | 3 | 2 | A-value-ops | pure-fn |
| hexa_round | 3 | 1 | A-value-ops | pure-fn |
| hexa_sin | 3 | 1 | A-value-ops | pure-fn |
| hexa_tan | 3 | 1 | A-value-ops | pure-fn |
| hexa_u_floor | 3 | 1 | A-value-ops | pure-fn |
| hexa_clamp | 2 | 1 | A-value-ops | pure-fn |
| hexa_contains_poly | 2 | 1 | A-value-ops | pure-fn |
| hexa_float_to_int | 2 | 1 | A-value-ops | pure-fn |
| hexa_log2 | 2 | 1 | A-value-ops | pure-fn |
| hexa_tanh | 1 | 1 | A-value-ops | pure-fn |
| hexa_add_slow | 0 | 2 | A-value-ops | pure-fn |
| hexa_arena_on | 3 | 2 | B-alloc | intrinsic+pure |
| hexa_arena_reset | 3 | 1 | B-alloc | intrinsic+pure |
| hexa_arena_rewind | 3 | 3 | B-alloc | intrinsic+pure |
| hexa_intern | 2 | 3 | B-alloc | intrinsic+pure |
| hexa_val_heapify | 2 | 32 | B-alloc | intrinsic+pure |
| hexa_arena_mark | 1 | 2 | B-alloc | intrinsic+pure |
| hexa_intern_grow | 1 | 2 | B-alloc | intrinsic+pure |
| hexa_intern_init | 1 | 2 | B-alloc | intrinsic+pure |
| hexa_arena_alloc | 0 | 12 | B-alloc | intrinsic+pure |
| hexa_arena_new_block | 0 | 3 | B-alloc | intrinsic+pure |
| hexa_val_arena_calloc | 0 | 10 | B-alloc | intrinsic+pure |
| hexa_val_arena_heapify_return | 0 | 2 | B-alloc | intrinsic+pure |
| hexa_val_arena_on | 0 | 9 | B-alloc | intrinsic+pure |
| hexa_val_arena_scope_pop | 0 | 3 | B-alloc | intrinsic+pure |
| hexa_val_arena_scope_push | 0 | 4 | B-alloc | intrinsic+pure |
| hexa_val_snapshot_array | 0 | 3 | B-alloc | intrinsic+pure |
| hexa_iter_get | 151 | 1 | B-closure | intrinsic |
| hexa_call2 | 19 | 8 | B-closure | intrinsic |
| hexa_fn_new | 13 | 8 | B-closure | intrinsic |
| hexa_call1 | 7 | 18 | B-closure | intrinsic |
| hexa_closure_new | 7 | 2 | B-closure | intrinsic |
| hexa_call0 | 6 | 4 | B-closure | intrinsic |
| hexa_call3 | 6 | 1 | B-closure | intrinsic |
| hexa_call4 | 6 | 1 | B-closure | intrinsic |
| hexa_closure_env | 0 | 6 | B-closure | intrinsic |
| hexa_ptr_alloc | 43 | 1 | B-ptr | intrinsic |
| hexa_ptr_write | 18 | 1 | B-ptr | intrinsic |
| hexa_ptr_addr | 17 | 1 | B-ptr | intrinsic |
| hexa_ptr_free | 13 | 1 | B-ptr | intrinsic |
| hexa_ptr_read | 7 | 2 | B-ptr | intrinsic |
| hexa_deref | 5 | 1 | B-ptr | intrinsic |
| hexa_ptr_null | 4 | 1 | B-ptr | intrinsic |
| hexa_ptr_offset | 4 | 1 | B-ptr | intrinsic |
| hexa_to_string | 385 | 19 | C-fmt | syscall+pure |
| hexa_println | 160 | 3 | C-fmt | syscall+pure |
| hexa_format_n | 22 | 3 | C-fmt | syscall+pure |
| hexa_eprint_val | 20 | 1 | C-fmt | syscall+pure |
| hexa_print_val | 14 | 5 | C-fmt | syscall+pure |
| hexa_format | 6 | 1 | C-fmt | syscall+pure |
| hexa_format_float | 5 | 1 | C-fmt | syscall+pure |
| hexa_format_float_sci | 5 | 1 | C-fmt | syscall+pure |
| hexa_eprintln | 2 | 2 | C-fmt | syscall+pure |
| hexa_read_file | 26 | 3 | C-io-fs | syscall |
| hexa_write_file | 20 | 2 | C-io-fs | syscall |
| hexa_append_file | 14 | 1 | C-io-fs | syscall |
| hexa_file_size | 12 | 2 | C-io-fs | syscall |
| hexa_delete_file | 11 | 1 | C-io-fs | syscall |
| hexa_input | 11 | 1 | C-io-fs | syscall |
| hexa_file_exists | 7 | 2 | C-io-fs | syscall |
| hexa_read_bytes_at | 7 | 2 | C-io-fs | syscall |
| hexa_read_file_bytes | 7 | 2 | C-io-fs | syscall |
| hexa_write_bytes | 7 | 4 | C-io-fs | syscall |
| hexa_write_bytes_append | 7 | 2 | C-io-fs | syscall |
| hexa_read_lines | 6 | 1 | C-io-fs | syscall |
| hexa_read_stdin | 6 | 1 | C-io-fs | syscall |
| hexa_write_bytes_v | 4 | 3 | C-io-fs | syscall |
| hexa_write_bytes_append_v | 3 | 2 | C-io-fs | syscall |
| hexa_net_connect | 6 | 1 | D-net | syscall(socket) |
| hexa_net_listen | 6 | 1 | D-net | syscall(socket) |
| hexa_net_accept | 5 | 1 | D-net | syscall(socket) |
| hexa_net_close | 5 | 1 | D-net | syscall(socket) |
| hexa_net_read | 5 | 1 | D-net | syscall(socket) |
| hexa_net_write | 5 | 1 | D-net | syscall(socket) |
| hexa_http_get | 4 | 1 | D-net | syscall(socket) |
| hexa_env_var | 32 | 2 | D-proc-env | syscall |
| hexa_set_args | 23 | 1 | D-proc-env | syscall |
| hexa_args | 21 | 1 | D-proc-env | syscall |
| hexa_exec | 20 | 3 | D-proc-env | syscall |
| hexa_random | 16 | 2 | D-proc-env | syscall |
| hexa_timestamp | 15 | 3 | D-proc-env | syscall |
| hexa_exit | 13 | 1 | D-proc-env | syscall |
| hexa_exec_with_status | 12 | 2 | D-proc-env | syscall |
| hexa_clock | 8 | 1 | D-proc-env | syscall |
| hexa_sleep | 8 | 1 | D-proc-env | syscall |
| hexa_exec_replace | 5 | 1 | D-proc-env | syscall |
| hexa_now_monotonic_s | 4 | 1 | D-proc-env | syscall |
| hexa_sleep_s | 4 | 1 | D-proc-env | syscall |
| hexa_utc_compact_now | 4 | 1 | D-proc-env | syscall |
| hexa_utc_iso_now | 4 | 1 | D-proc-env | syscall |
| hexa_time_ms | 3 | 2 | D-proc-env | syscall |
| hexa_from_cstring | 7 | 1 | F-ffi | intrinsic+codegen |
| hexa_callback_create | 6 | 1 | F-ffi | intrinsic+codegen |
| hexa_extern_call | 6 | 3 | F-ffi | intrinsic+codegen |
| hexa_cstring | 5 | 1 | F-ffi | intrinsic+codegen |
| hexa_ffi_dlopen | 5 | 2 | F-ffi | intrinsic+codegen |
| hexa_callback_free | 3 | 1 | F-ffi | intrinsic+codegen |
| hexa_callback_slot_id | 3 | 1 | F-ffi | intrinsic+codegen |
| hexa_ffi_dlsym | 3 | 1 | F-ffi | intrinsic+codegen |
| hexa_ffi_extract_libname | 3 | 2 | F-ffi | intrinsic+codegen |
| hexa_host_ffi_call_6 | 3 | 3 | F-ffi | intrinsic+codegen |
| hexa_extern_call_typed | 2 | 4 | F-ffi | intrinsic+codegen |
| hexa_ffi_marshal_arg | 2 | 4 | F-ffi | intrinsic+codegen |
| hexa_host_ffi_call | 2 | 3 | F-ffi | intrinsic+codegen |
| hexa_host_ffi_open | 2 | 2 | F-ffi | intrinsic+codegen |
| hexa_host_ffi_sym | 2 | 2 | F-ffi | intrinsic+codegen |
| hexa_call_extern_raw | 1 | 7 | F-ffi | intrinsic+codegen |
| hexa_host_ffi_unwrap | 1 | 27 | F-ffi | intrinsic+codegen |
| hexa_trampoline_dispatch | 0 | 2 | F-ffi | intrinsic+codegen |
| hexa_trampoline_init | 0 | 2 | F-ffi | intrinsic+codegen |
| hexa_math_tanh | 7 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_tan | 6 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_acos | 5 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_asin | 5 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_atan | 5 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_atan2 | 5 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_min | 5 | 2 | G-math | pure-fn(libm-opt) |
| hexa_math_sqrt | 5 | 2 | G-math | pure-fn(libm-opt) |
| hexa_math_abs | 4 | 2 | G-math | pure-fn(libm-opt) |
| hexa_math_ceil | 4 | 2 | G-math | pure-fn(libm-opt) |
| hexa_math_cos | 4 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_exp | 4 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_floor | 4 | 2 | G-math | pure-fn(libm-opt) |
| hexa_math_log | 4 | 1 | G-math | pure-fn(libm-opt) |
| hexa_math_max | 4 | 2 | G-math | pure-fn(libm-opt) |
| hexa_math_pow | 4 | 2 | G-math | pure-fn(libm-opt) |
| hexa_math_round | 4 | 2 | G-math | pure-fn(libm-opt) |
| hexa_math_sin | 4 | 1 | G-math | pure-fn(libm-opt) |
| hexa_throw | 18 | 1 | H-except | intrinsic(ctxsave) |
| hexa_is_error | 10 | 1 | H-except | intrinsic(ctxsave) |
| hexa_ic_dump_stats | 0 | 2 | Z-ic | drop(leaf-dead) |
| hexa_ic_stats_on | 0 | 2 | Z-ic | drop(leaf-dead) |

---

## 5. 실행 순서 (P7-9 fixpoint 직후, 자세한 checklist)

각 Wave 는 commit 1 + rebuild 1 (단, HX8 seed freeze 해제 후):

1. **Wave 0** — dead-strip 11 심볼 (`git commit`)
2. **Wave 1a** — `self/rt/string.hexa` + `array_ops.hexa` + `map_ops.hexa` active (이미 존재) → `hexa_str_*`/`hexa_array_*`/`hexa_map_*` forward-decl 제거, `#include "rt_string.h"` 생성
3. **Wave 1b** — A-encoding / A-json / A-tensor / A-valstruct / A-struct 포팅 완료 후 drop
4. **Wave 2** — `rt/alloc.hexa` + `@syscall(mmap)` intrinsic (P7-7a pre-req). arena 심볼 drop
5. **Wave 3** — `rt/fmt.hexa` + `rt/io.hexa` + `@syscall(write/open/read/close/unlink/fstat)`. printf/fopen 전체 삭제
6. **Wave 4** — `rt/proc.hexa` + `rt/net.hexa`. popen/fork/socket libc 삭제
7. **Wave 5** — `rt/except.hexa` + `@save_context`/`@restore_context` intrinsic (신규). setjmp/longjmp 삭제
8. **Wave 6** — F-ffi: 정적 링크만 지원 확정 → dlopen/dlsym 경로 drop. `runtime.c` FFI 절 삭제
9. **Wave 7** — libm 대체 (`rt/math.hexa` 18 심볼) → `-lm` 링크 옵션 drop
10. **Terminal** — `runtime.c` 0 LOC, `bootstrap_compiler.c` 0 LOC, `self/native/*.c` 0 LOC. `.hexa` only.

각 Wave 끝마다 smoke test: `hexa tool/rebuild_stage0.hexa` fixpoint 4/4 IDENTICAL + anima t1/t2 physical-ceiling 회귀 없음.

---

## 6. 미결 결정 (decision needed)

1. **libm kernel-dep 허용?** — 메모리 `feedback_c_total_purge_after_fixpoint.md` 는 "예외 없음". → Option α 강제. Wave 7 에서 ~2,000 LOC pure-hexa math 작성.
2. **Dynamic FFI (dlopen/dlsym)** — `hexa_full.hexa` 자체는 안 씀. ML/serving 만 필요. P7-10 에 재검토.
3. **Trampoline RWX mmap** — JIT 코드 생성 자체가 C-ish. 가능하면 compile-time codegen 으로 전체 대체 (B-closure 와 F-ffi 가 겹치지 않음).
4. **setjmp/longjmp → Result-typed return?** — `hexa_throw` 18 ext callsite. 대규모 리팩토링 대신 `@save_context` intrinsic 선호.

## 7. References

- `docs/plans/runtime_c_inventory.md` — 포팅 카탈로그 (각 심볼의 libc 의존 + note)
- `docs/plans/runtime_c_purge_audit.md` — 2026-04-19 1-wave dead-strip audit (16 심볼 삭제)
- `docs/plans/self-hosting-roadmap.md` — P7-7 전체 계획
- `docs/plans/syscall_intrinsic_spec.md` — `@syscall` intrinsic 사양
- 메모리 `feedback_c_total_purge_after_fixpoint.md` — 박멸 정책
- 메모리 `project_self_hosting_track.md` — 현황 90%

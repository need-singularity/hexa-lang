# HEXA Hot Path Policy

**Purpose**: Define the runtime-C ↔ HEXA boundary for anima training performance.

## Why this doc exists

hexa-lang's self-host drive (`HX4: 모든 코드 .hexa`) migrates pure-logic C to
HEXA incrementally. But training speed on anima (LLM, diffusion) is dominated
by per-element C primitives — BLAS sgemm, f32 pointer I/O, tensor alloc.
Moving these to HEXA multiplies per-element overhead (HexaVal wrap/unwrap,
tag dispatch) by 10-100x.

**Resolution**: a tiered model where training-critical kernels explicitly
stay in C (`@hot_kernel`) and the rest migrates to HEXA over time.

## Tier Definition

| Tier | Scope | Location | Migration |
|---|---|---|---|
| **T1** | OS boundary (syscall/dlopen/mmap/malloc) | `self/runtime.c` | **NEVER** — C ABI required |
| **T2** | Value system (`HexaVal` tagged union, tag constants) | `self/runtime.c` | **NEVER** — foundation of C-compile model |
| **T3** | Tensor/BLAS hot kernels | **`self/native/tensor_kernels.c`** | **NEVER** without benchmark gate |
| **T4** | String/array/map helpers | `self/runtime.c` (logic) | → `self/core/*.hexa` (planned) |
| **T5** | Utilities (print, time, exec) | `self/runtime.c` | → HEXA body, thin C wrapper only |

## T3 Function List

Canonical: `$NEXUS/shared/hexa/hot-path.jsonl`.

Currently listed (11 functions):
- `hexa_ptr_read_f32 / write_f32` — per-element tensor I/O
- `hexa_ptr_read_f64` — double-precision read
- `hexa_ptr_read_i32 / write_i32` — int buffer ops
- `hexa_struct_pack_f32 / unpack_f32` — FFI marshaling
- `hexa_tensor_new / randn / data_ptr / from_ptr` — tensor struct ops

Each is marked `@hot_kernel` in `tensor_kernels.c` source.

## Migration Rules

### Rule 1: `@hot_kernel` 차단

PR 이 `self/native/tensor_kernels.c` 의 함수를 HEXA 로 옮기거나, 새로운
함수를 runtime.c 에 추가하면서 "이거 HEXA 로 옮기지 말라" 는 의도라면
**반드시 `@hot_kernel` 주석** + hot-path.jsonl 등록.

### Rule 2: 벤치마크 게이트

T3 등록 함수를 수정하는 PR 은 다음 측정치 첨부 필수:

```
Before: train_alm_14b_micro.hexa 1-epoch — 42.3s ± 0.5
After:  train_alm_14b_micro.hexa 1-epoch — 42.1s ± 0.6 (diff -0.5%, within noise)
```

**Gate**: diff > 5% (절대값) → 자동 block.

### Rule 3: 리뷰 gate

T3 파일 수정 PR 은 `@anima-core` 팀 리뷰 필수.

### Rule 4: T4/T5 이식 중 충돌

T4/T5 migration 도구가 `tensor_kernels.c` 경로를 우연히 건드리면 즉시 실패
(hot-path.jsonl 화이트리스트 교차 체크).

## T3 Phase 2 Plan (future)

현재 `tensor_kernels.c` 는 `#include` 로 runtime.c 에 텍스트 포함. 향후:

1. **Standalone TU**: tensor_kernels.c 를 독립 object file (.o) 로 컴파일
2. **LTO**: runtime.o + tensor_kernels.o → clang -flto 로 인라인 허용
3. **BLAS direct link**: Accelerate (macOS) / OpenBLAS (Linux) 를 사이드카
   library 로 링크 → `cblas_sgemm` 직접 호출 (현재 FFI dlsym 우회)
4. **GPU backend**: tensor_kernels_metal.m (Metal) / tensor_kernels_cuda.cu
   — 플랫폼별 대체 구현

각 단계는 별도 PR + anima 측정 검증.

## Related

- `shared/hexa/hot-path.jsonl` — canonical whitelist
- `self/native/tensor_kernels.c` — T3 source (@hot_kernel markers)
- `self/runtime.c` — T1/T2/T4/T5 (currently mixed; T4/T5 migration ongoing)
- `shared/hexa/build-speed-metrics.jsonl` — transpile baseline log
- `CLAUDE.md` HX4 (모든 코드 .hexa) — 원칙
- `CLAUDE.md` HX7 (self-host 경로) — runtime.c 허용

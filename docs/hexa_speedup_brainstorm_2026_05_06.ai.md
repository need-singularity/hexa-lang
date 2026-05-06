---
doc: hexa_lang.speedup_brainstorm_2026_05_06
kind: brainstorm
audience: [human, agent]
date: 2026-05-06
mk: 1
status: brainstorm-exhausted
parent: hexa-lang main (commit d4ebf6cd post-PATH-fix)
related: HEXA_F1~F6 falsifier chain (orpheus)
---

# hexa-lang 성능 / 자원 / 속도 개선 — 100+ ideas catalog

> 본 문서는 brainstorming 산출물. impl ranking 은 §3, §1 참조.
> 정량 estimate 는 모두 "가정 → 결론" 형태로 명시. 가정이 깨지면 estimate 도 무효.
> hexa-lang 철학(raw 9: hexa-only / mk2 schema 정합) 위반 idea 는 REJECT 명시.

---

## 0. Baseline (parent commit d4ebf6cd 기준)

| 측면 | 현재 상태 |
|------|----------|
| value system | tagged-union `HexaVal` (NaN-boxing 미적용 — 인라인 union + heap descriptor 혼재; runtime.c:584 주석) |
| memory cap | default 768 MB, `HEXA_MEM_CAP_MB` / `--mem-cap=N` / `HEXA_MEM_UNLIMITED` (runtime.c:161) |
| tick model | `_hx_mem_tick_ctr & 0x0FFFu` (4096-call sampling, runtime.c:213) |
| string interning | open-addressed hash, `INTERN_INIT_CAP`, `INTERN_MAX_LEN` 길이컷 (runtime.c:262) |
| arena allocator | bump arena (`hexa_arena_alloc`) 공유 블록 체인 — array slot / str_concat 핫패스 |
| exec | popen→/bin/sh -c → fread (runtime.c:3366); macOS PATH augment 적용 (d4ebf6cd) |
| codegen | `.c` (single backend); clang `-O2 -std=c11` (build_dispatch.hexa:51) |
| AOT cache | `~/.hx/cache/aot/` 16 hex prefix CAS (sha-keyed) |
| interp cache | `~/.hx/packages/hexa/.hexa-cache/` |
| stage1 dispatch | `build/stage1/main.c` 4128 LOC (transpiled from `self/main.hexa` 3224 LOC) |
| native compiler | `self/native/hexa_v2` 단일 바이너리, macOS jetsam SIGKILL 위험 (HEXA_F3) |
| JIT skeleton | `self/jit.hexa` 259 LOC stub (Cranelift port 흔적, 미연결 가능성) |
| inline cache | `self/inline_cache.hexa` 126 LOC (call-site type cache, monomorphic) |
| bytecode VM | `self/bc_vm.hexa` 1906 LOC (실 동작 여부 확인 필요) |
| hot kernels | `self/native/tensor_kernels.c` (T3 BLAS / f32 ops); `@hot_kernel` 게이트 |
| 알려진 bug | HEXA_F2 trk.24: `\uXXXX` parse fixture idx 11 KR + idx 12 escape A |

---

## 1. TL;DR — Top-5 next-cycle candidates (ranked)

Ranking criteria: leverage(estimate) ÷ effort, 호환성 무손상, falsifier 즉시 가능.

### #1 — clang `-O2` → `-O3 -flto` for stage1 dispatch (build_dispatch.hexa:51)
- **Effort**: 즉시 (1 LOC change + smoke run)
- **Estimate**: dispatch latency 0.95~0.85x (~5–15% faster startup); binary 3–8% bigger
- **가정**: clang 의 LTO 가 single-TU dispatch (4128 LOC `main.c`) 에 의미 있을 정도의 cross-fn opt 기회를 만들어낸다. 이미 single-TU 라 LTO 효과는 **whole-program inlining 한정**. 함수 boundary 가 적으면 효과 미미.
- **자원**: dev <1h, RSS unchanged, binary size +3–8% (`runtime.c` 8079 LOC 인라인 가능성)
- **호환성**: backward-compat (build flag only)
- **Falsifier**: `bench/probe.hexa` median_ns 비교; HEXA_F1 dispatch latency gate 가능

### #2 — popen → posix_spawn + pipe for `hexa_exec` (runtime.c:3366)
- **Effort**: 단기 (1 day; `hexa_exec` / `hexa_exec_with_status` / `hexa_exec_stream_impl` 3곳)
- **Estimate**: exec 호출당 1.3–2x (popen 의 `/bin/sh -c` fork 추가 → posix_spawn 으로 직접 argv 분해 후 spawn)
- **가정**: 호출자가 shell-meta(`|`,`>`,`*`)를 쓰지 않는 경우만. shell-meta detect 시 fallback. anima training 에서 `exec("python3 -c ...")` 같은 호출은 shell 안 거쳐도 됨.
- **자원**: dev 4–8h, RSS 동일, binary +1KB (parser)
- **호환성**: opt-in (`HEXA_EXEC_NO_SHELL=1`) → backward-compat 유지. 기본은 popen.
- **Falsifier**: HEXA_F4 exec stream gate; tick 분포 측정 (per-call latency p50/p99)

### #3 — bytecode cache (parse-once / re-execute) at `~/.hx/packages/hexa/.hexa-cache/`
- **Effort**: 단기 (3–5 days; bc_emitter.hexa serialize/deserialize 추가)
- **Estimate**: cold start unchanged; warm start (cache hit) 2–10x faster; lexer/parser 단계 skip
- **가정**: `self/bc_emitter.hexa` (4093 LOC) 가 stable 한 bytecode 를 출력하고, 동일 source hash 로 캐시 hit 가 가능. 현재 `.hexa-cache/` 는 source-hash 기반 lookup 이미 존재 (확인 필요).
- **자원**: dev 16–40h, 디스크 캐시 +수MB/project, RAM unchanged
- **호환성**: backward-compat (cache miss → 기존 path)
- **Falsifier**: HEXA_F5 cache integrity gate; bytecode hash chain regression detect

### #4 — pipe size 증가 + line-buffered popen (`F_SETPIPE_SZ` Linux / `fcntl` macOS)
- **Effort**: 즉시 (3 LOC; runtime.c:3366 직후 fcntl 호출)
- **Estimate**: high-volume exec_stream (anima training log 흡수 등) 1.2–3x throughput; small exec 변화 없음
- **가정**: hexa 가 large output (>64KB) 흘리는 process 와 자주 통신. macOS `F_SETPIPE_SZ` 지원 안 됨 → `setvbuf` 큰 buffer 로 대체.
- **자원**: dev <1h, kernel pipe buffer +1MB/process (Linux), RAM unchanged macOS
- **호환성**: backward-compat (kernel rejects → 기본 size fallback)
- **Falsifier**: HEXA_F4 throughput; `bench/m4_inference_e2e.hexa`

### #5 — string intern: 길이 캡 상향 + 짧은 string SSO (Small String Optimization)
- **Effort**: 단기 (2–3 days; `HexaVal` union 변경, 광범위 영향)
- **Estimate**: short-string-heavy workload (JSON parse, tokenizer) 1.5–3x; mismatch 시 변화 없음
- **가정**: hexa 의 typical workload (anima tokenizer / parser) 가 ≤16 byte 짧은 문자열을 대량 생성. heap 할당 회피만으로 hot-path 1.5x.
- **자원**: dev 8–24h; binary 동일; RAM 약감 (heap 할당 감소)
- **호환성**: HexaVal 내부 layout 변경 → AOT 캐시 키 v-bump 필요 (breaking for cached binaries; source-level OK)
- **Falsifier**: HEXA_F2 string parse gate (trk.24 \uXXXX 와 직교); intern hit ratio 통계 export

---

## 2. 차원별 idea catalog

각 idea: ID · name · impact-est · effort · compat · falsifier 가능?

### A. 컴파일러 차원 (Optimization)

A01. **LTO -flto stage1**: ~1.05x dispatch · 즉시 · compat · F1
A02. **LTO -flto runtime**: ~1.10x runtime · 단기 · compat · F1
A03. **clang -O3 (현 -O2)**: ~1.05–1.15x · 즉시 · compat · F1 — 가정: codegen 이 -O3 friendly (vectorize 안전). 위험: -O3 가 `-fno-strict-aliasing` 없을 때 HexaVal punning UB 발현 가능 → 검증 필요.
A04. **PGO (instrumented build → re-build)**: ~1.10–1.30x · 중기 · compat · F1 — 가정: stable workload (training step) profile 수집 가능.
A05. **Dead code elimination (compiler-side)**: ~1.02x · 단기 (clang already does) · compat · F1 — REJECT (이미 -O2 LLVM DCE 적용)
A06. **Inlining 정책 강화 (`__attribute__((always_inline))` for hot HexaVal accessors)**: ~1.05–1.20x · 단기 · compat · F1 — runtime.c:352 `HX_STRLEN` 등 macro 가 inline 강제. 추가 후보: HexaVal 생성자.
A07. **Constant folding extension**: ~1.01x · 중기 · compat · F1 — REJECT marginal (LLVM 처리)
A08. **CSE (common subexpr) — hexa-side IR**: ~1.05x · 중기 · compat · F1 — `self/ir/` lowering 단계
A09. **LICM hexa-side**: ~1.02x · 중기 · compat · F1
A10. **Loop unrolling (small bounded)**: ~1.05x · 단기 · compat · F1 — `self/loop_unroll.hexa` 이미 존재 → 활성화 정도 audit
A11. **Tail call optimization (`@tco` attr)**: ~1.10x recursion · 중기 · compat · F1 — `test_tco.hexa` 존재
A12. **Lambda hoisting (capture-free → static fn)**: ~1.05x · 중기 · compat · F1
A13. **Escape analysis (heap → stack alloc)**: ~1.10–1.30x · 장기 · compat · F1 — `self/escape_analysis.hexa` 존재
A14. **SROA (scalar replacement of aggregates)**: ~1.05x · 중기 · compat · F1 — clang 이 LLVM IR 에서 처리 → hexa C codegen 이 작은 struct 만들면 자동
A15. **Bounds check elimination (proven safe)**: ~1.05–1.15x array hot loops · 중기 · compat · F1
A16. **Devirtualization (single-impl trait)**: ~1.05x · 중기 · compat · F1
A17. **Branch hints (`__builtin_expect`)**: ~1.02x · 즉시 · compat · F1 — runtime.c hot-path 약 10곳 후보
A18. **Cold/hot attrs (`__attribute__((cold))`)**: ~1.02x icache · 즉시 · compat · F1 — error path 에 cold 부착
A19. **CPU dispatch (function multiversion SSE4/AVX/AVX2)**: ~1.5x SIMD-able only · 중기 · compat · F1 — `tensor_kernels.c` 우선 적용
A20. **Auto-vectorization (clang 이미 -O2 부분 적용 / -O3 강화)**: ~1.5x reduction loops · 단기 · compat · F1
A21. **Loop tiling**: ~1.5–3x matmul · 중기 · compat · F1 — `self/native/tensor_kernels.c` 우선
A22. **Software pipelining**: ~1.2x · 장기 · compat · F1 — clang LLVM 일부 자동, 수동 hint 어려움
A23. **SIMD intrinsics direct (NEON / AVX2 in tensor_kernels.c)**: ~2–4x · 중기 · compat · F1 — `test_neon_fp16_gemm.hexa` 등 fixture 존재
A24. **Compile-time eval (`const fn`)**: ~1.05x · 중기 · compat · F1 — `self/comptime.hexa` 존재
A25. **Macro expansion at compile time**: ~1.02x · 중기 · compat · F1 — `self/macro_expand.hexa` 존재
A26. **Type erasure → monomorphization tradeoff**: ~1.10x dispatch · 중기 · compat — REJECT 표면적: hexa generic 시스템이 이미 단순 (large monomorphization burden 없음)
A27. **Whole-program optimization (single-TU + LTO)**: ~1.10x · 중기 · compat · F1 — A01+A02 결합

### B. 런타임 차원 (exec / GC / memory)

B01. **popen → posix_spawn (no shell)**: ~1.3–2x exec · 단기 · opt-in · F4
B02. **popen FILE* line-buffered tune**: ~1.05x small reads · 즉시 · compat · F4
B03. **`F_SETPIPE_SZ` enlarge (Linux only)**: ~1.2–3x large output stream · 즉시 · compat · F4
B04. **exec result memoization (deterministic cmd)**: ~10–100x repeat-call · 중기 · opt-in · F4 — 가정: caller 가 `@pure_exec` annotation
B05. **Persistent worker daemon**: ~5–50x spawn-heavy workload · 장기 · opt-in · F4 — 가정: 동일 binary 반복 호출
B06. **IPC shared memory**: ~2–10x large data hand-off · 장기 · opt-in · F4
B07. **mmap large input**: ~1.5x parse-large-file · 단기 · compat · F2 — runtime.c:3994 `fread` 패턴 → mmap 으로 교체 가능
B08. **Zero-copy stdout (writev / pipe2)**: ~1.05x · 단기 · compat · F4
B09. **Async exec (non-blocking fork)**: ~2x parallel exec scenarios · 중기 · opt-in · F4
B10. **exec timeout precision (gettimeofday → clock_gettime CLOCK_MONOTONIC)**: 정확도 개선 · 즉시 · compat · F4
B11. **SIGCHLD reaper (zombie-free fork)**: 정확도 개선, no zombies · 즉시 · compat — REJECT marginal speed; correctness improvement
B12. **GC 정책 (현재 GC 명시적 fn 없음 — manual ref + arena)**: arena leak 검사 강화 · 중기 · compat · F1
B13. **Reference counting + cycle detector**: dev high · 장기 · breaking · F1 — REJECT: hexa 의 현재 model 은 "leak-on-exit" arena. Ref counting 도입은 철학(simple) 위반 가능.
B14. **Generational GC**: ~1.5x alloc-heavy · 장기 · breaking · F1 — REJECT: 동일 사유 + GC 비용 unpredictable
B15. **Region-based memory (per-call arena reset)**: ~1.5x · 중기 · compat · F1 — 이미 부분 도입 (arena_reset 가능?)
B16. **Bump allocator (이미 있음 — hexa_arena_alloc)**: optimize block size · 즉시 · compat · F1
B17. **Slab allocator (HexaVal 고정 size)**: ~1.05x alloc · 단기 · compat · F1 — HexaVal sizeof 알려져 있어 적용 가능
B18. **Memory pool per-thread**: TLS arena · 중기 · compat · F1 — 가정: 멀티스레드 host 도입 시 의미. 현재 single-thread 면 skip
B19. **Stack alloc small structs**: ~1.05x · 단기 · compat · F1 — codegen_c.hexa 에서 small struct 를 `auto` 변수로
B20. **Tagged pointers (NaN-boxing) full**: ~1.10–1.30x value ops · 중기 · breaking (HexaVal layout) · F1 — 현재 hexa_nanbox.h 존재 → bridge 만 미연결
B21. **String interning (이미 부분 — 길이 cap)**: cap 상향 + LRU evict · 단기 · compat · F2
B22. **Lazy string concat (rope)**: ~1.5x str-heavy · 중기 · compat · F2 — 가정: large concat 빈도 높을 때만
B23. **Copy-on-write strings**: ~1.10x · 중기 · compat · F2
B24. **Small string optimization (≤16 byte inline)**: ~1.5–3x short-str hot · 단기 · breaking layout · F2 — see #5
B25. **Object pooling (closure HexaClo, fn HexaFn descriptors)**: ~1.10x · 단기 · compat · F1 — runtime.c:742 closure pool, 754 fn descriptor pool
B26. **Object pre-allocation (warm-up bulk alloc)**: ~1.02x startup · 즉시 · compat · F1
B27. **Lock-free intern table**: 멀티스레드 시만 · 장기 · compat — REJECT 현재 single-thread

### C. 인터프리터 차원 (Stage 1)

C01. **Direct threaded code dispatch (computed goto)**: ~1.5–3x interp loop · 단기 · compat · F1 — `self/bc_vm.hexa` 1906 LOC 가 dispatch 있다면 적용
C02. **Inline caching for method dispatch**: ~1.5x dynamic call · 중기 · compat · F1 — `self/inline_cache.hexa` 이미 존재 → integrate
C03. **Bytecode → native trace JIT**: ~5–20x hot loop · 장기 · compat (opt-in) · F1
C04. **Trace tree (TraceMonkey-style)**: ~5–10x · 장기 · compat · F1
C05. **Method JIT (Ion-style)**: ~3–10x · 장기 · compat · F1 — `self/jit.hexa` 259 LOC 이미 skeleton (Cranelift)
C06. **Tiered compilation (interp → baseline → optimizing)**: ~3–10x · 장기 · compat · F1
C07. **Inline asm hot loops (NEON/AVX2 manual)**: ~2x specific kernel · 중기 · compat · F1
C08. **Profile-driven JIT (collect hot fn from interp counters)**: ~3x with C05 · 장기 · compat · F1
C09. **AOT pre-compile commonly-used modules (stdlib)**: ~2x cold start · 단기 · compat · F1 — std_* 11 modules pre-build 가능
C10. **Bytecode caching (skip re-parse)**: ~2–10x warm start · 단기 · compat · F1 — see #3
C11. **Module hot-reload (avoid full re-parse)**: dev convenience · 중기 · compat — REJECT speed-irrelevant
C12. **Eval string→enum dispatch (already done per `runtime_bottlenecks.json` #1)**: ~20x eval_expr · done · F1 — 이미 적용됨

### D. 빌드 시스템 차원

D01. **Incremental compilation (delta-only rebuild)**: ~3–10x rebuild · 중기 · compat · F1 — `self/incr_cache.hexa` 존재
D02. **Build cache (sccache-style across hosts)**: ~2x rebuild · 단기 · compat · F1 — `~/.hx/cache/aot/` 이미 CAS 형
D03. **Parallel compilation (-j N file-level)**: ~Nx (N CPU cores) · 단기 · compat · F1 — currently single-TU stage1
D04. **Module-level parallelism**: ~Nx · 단기 · compat · F1
D05. **Codegen unit splitting (split monolithic main.c)**: ~Nx with D03 · 중기 · compat · F1 — 가정: 4128 LOC main.c 를 5–10 TU 로 split → -j parallel
D06. **Distributed compile (icecc-style)**: ~Nx workstation+farm · 장기 · compat — REJECT over-engineering for 단일 dev
D07. **ccache-friendly output (deterministic build)**: ~2x rebuild hit · 단기 · compat · F1
D08. **Reproducible builds**: 검증 강화 · 단기 · compat · F1 — TZ/locale strip
D09. **Build dispatcher async (compile + link parallel)**: ~1.2x · 단기 · compat · F1
D10. **Watch mode (file-change → incremental)**: dev convenience · 중기 · compat — REJECT non-perf

### E. 캐시 차원

E01. **AST cache (parse once)**: ~5x re-eval · 단기 · compat · F1 — Sub-step of C10
E02. **Type-checked AST cache**: ~2x re-typecheck · 단기 · compat · F1
E03. **Object code cache (.o keyed by source hash)**: ~Nx repeat compile · 단기 · compat · F1 — sub-form of D02
E04. **Linker cache (final binary keyed by all inputs)**: ~10x re-link · 단기 · compat · F1
E05. **ICache invalidation policy (mtime+hash)**: 정확도 + 1.05x · 단기 · compat · F1
E06. **LRU eviction**: bound disk usage · 단기 · compat — REJECT: 현재 size limit 이미 충분
E07. **Time-based eviction (30 day TTL)**: bound disk · 단기 · compat — REJECT marginal
E08. **Content-addressed storage (이미 적용 — 16 hex prefix)**: optimize prefix length · 즉시 · compat — fs lookup overhead 분석 필요
E09. **Distributed cache (Redis remote)**: ~Nx team · 장기 · compat — REJECT 단일 dev 환경
E10. **Atomic cache writes (rename O_EXCL)**: 정확도 강화 · 즉시 · compat · F5

### F. 자원 효율 차원 (memory / I/O)

F01. **Memory cap configurable (이미 적용 768MB default + env override + flag)**: done · F1
F02. **mmap input files (avoid full read)**: ~1.5x parse-large · 단기 · compat · F2 — runtime.c:3994 `fread` candidate
F03. **Streaming parser (large input)**: ~1.5x · 중기 · compat · F2
F04. **Lazy AST construction (on-demand subtree)**: ~1.10x · 장기 · compat · F1
F05. **Drop AST after codegen**: ~1.05x peak RAM · 즉시 · compat · F1 — codegen done → AST 메모리 free
F06. **io_uring async I/O (Linux)**: ~1.5x I/O-heavy · 장기 · opt-in (linux only) · F4
F07. **kqueue async I/O (macOS)**: same as F06 macOS · 장기 · opt-in · F4
F08. **Direct I/O (O_DIRECT bypass cache)**: situational · 중기 · opt-in — REJECT typical workload not large-file streaming
F09. **madvise WILLNEED / SEQUENTIAL**: ~1.05x mmap · 즉시 · compat · F2
F10. **Huge pages (2MB / 1GB)**: ~1.05x TLB · 단기 · opt-in · F1 — Linux only useful
F11. **THP transparent huge pages**: kernel handled · 즉시 · compat — env-tunable
F12. **NUMA-aware allocation**: ~1.10x multi-socket · 장기 · opt-in — REJECT 단일 dev workstation
F13. **CPU affinity / pinning**: ~1.05x · 즉시 · opt-in · F1
F14. **Thermal-aware scheduling**: 안정성 · 중기 · opt-in — REJECT premature

### G. stdlib 차원 (표준 라이브러리 가속)

G01. **BLAS/LAPACK bindings**: ~10–100x matmul · 단기 · compat · F1 — 이미 hxblas 존재 (`self/native/hxblas_linux.c`)
G02. **AVX-512 SIMD primitives**: ~2x AVX-able · 중기 · opt-in · F1
G03. **Apple AMX intrinsics**: ~5–10x M-series · 중기 · opt-in (macOS only) · F1 — `test_amx_int8.hexa` 존재
G04. **Apple Neural Engine bindings**: ~10x specific ops · 장기 · opt-in · F1 — `test_bnns_kernels.hexa` 존재
G05. **GPU compute (Metal Performance Shaders)**: ~10–50x matmul · 단기 · opt-in · F1 — `self/native/hxmetal_macos.m` 이미
G06. **Hash table tuned (Robin Hood / Hopscotch)**: ~1.5x map ops · 단기 · compat · F1 — `test_swiss_table.hexa`
G07. **B-tree / radix tree**: ~2x range query · 중기 · compat · F1
G08. **Bloom / Cuckoo filter**: ~10x existence-check · 단기 · compat · F1 — `test_bloom_filter_pure.hexa`
G09. **Skip list**: ~1.5x ordered map · 단기 · compat · F1
G10. **Persistent data structures (HAMT)**: dev convenience · 중기 · compat — REJECT speed-marginal vs immutable
G11. **Concurrent hash map (lock-free)**: ~Nx multi-thread · 장기 · opt-in — REJECT current single-thread
G12. **Concurrent queue (MPMC)**: ~Nx · 장기 · opt-in — same
G13. **Atomic primitives wrapper**: latency reduction · 단기 · compat · F1 — `self/atomic_ops.hexa` 존재
G14. **Memory ordering helpers (acq/rel)**: 정확도 · 단기 · compat · F1
G15. **Tagged unions efficient (=NaN-boxing)**: see B20
G16. **Variant types**: lang feature · 중기 · compat — REJECT non-perf
G17. **Pattern matching codegen (jump-table for dense enum)**: ~1.5x match-heavy · 중기 · compat · F1 — hexa interpreter#1 already partial
G18. **Error handling overhead reduction (Result<T,E> zero-cost)**: ~1.05x · 단기 · compat · F1
G19. **Panic-free hot paths (assert → debug-only)**: ~1.05x · 즉시 · compat · F1 — `@hot_kernel` 에서 assert strip

### H. 도구 차원 (profiler / debugger / observability)

H01. **CPU sampling profiler (perf-style)**: dev tool · 중기 · compat · F1
H02. **Heap profiler (heaptrack-style)**: dev tool · 중기 · compat · F1
H03. **Allocation profiler (이미 부분 — HEXA_ALLOC_STATS)**: extend · 단기 · compat · F1 — runtime.c:1054 `_hx_stats_*`
H04. **Lock contention profiler**: 멀티스레드만 · 장기 — REJECT current single-thread
H05. **I/O profiler (per-fn read/write count)**: dev tool · 단기 · compat · F4
H06. **Trace logging (chrome trace JSON)**: dev tool · 중기 · compat · F1
H07. **Distributed tracing (OpenTelemetry)**: 분산만 · 장기 — REJECT
H08. **Metric export (Prometheus)**: 운영만 · 장기 — REJECT
H09. **Crash dump + symbolication**: 안정성 · 중기 · compat · F1
H10. **AddressSanitizer build mode**: dev tool · 단기 · compat · F1
H11. **ThreadSanitizer**: 멀티스레드 only · 장기 — REJECT
H12. **MemorySanitizer**: dev tool · 단기 · compat · F1
H13. **UBSan**: dev tool · 즉시 · compat · F1
H14. **Coverage instrumentation**: dev tool · 단기 · compat · F1

### I. 코드 생성 차원 (output backend)

I01. **C output (현재) → LLVM IR direct**: ~1.10–1.30x (skip C parse) · 장기 · compat · F1 — major refactor
I02. **Cranelift (Rust JIT)**: ~5x JIT-able · 장기 · opt-in · F1 — `self/jit.hexa` 이미 Cranelift port 흔적
I03. **WASM target (browser / wasmtime)**: portability · 장기 · opt-in · F1 — `self/wasm.hexa`
I04. **LLVM target (LTO+PGO direct, skip C)**: ~1.10x + dev simplification · 장기 · compat · F1
I05. **ARM64 native (이미 — runtime.c builds clean)**: done · F1
I06. **RISC-V target**: portability · 장기 · opt-in — REJECT no current need
I07. **Bare-metal target (no libc)**: ESP32 등 · 장기 · opt-in · F1 — `self/codegen_esp32.hexa` 존재
I08. **Embedded MCU**: same · 장기 — `test_native_determinism.hexa` 등 fixture

### J. 동시성 차원

J01. **M:N threading (green threads)**: ~Nx I/O bound · 장기 · opt-in · F1
J02. **Async/await runtime**: ~Nx I/O · 중기 · compat · F1 — `self/async_runtime.hexa` 존재
J03. **Actor model**: dev model · 장기 · opt-in — REJECT premature
J04. **CSP (channels)**: dev model · 중기 · compat · F1
J05. **STM (Software Transactional Memory)**: ~conflicts low · 장기 · compat — REJECT premature
J06. **Lock-free data structures**: 멀티스레드 · 장기 · compat — see G11
J07. **Wait-free algorithms**: 멀티스레드 · 장기 — REJECT premature
J08. **RCU (Read-Copy-Update)**: 멀티스레드 · 장기 — REJECT premature
J09. **Hazard pointers**: 멀티스레드 · 장기 — REJECT
J10. **Epoch-based reclamation**: 멀티스레드 · 장기 — REJECT

### K. 보안 차원 (sandboxing / isolation)

K01. **WebAssembly sandbox**: 안전성 · 장기 · opt-in · F1
K02. **seccomp (Linux)**: 안전성 · 중기 · opt-in (linux) — REJECT speed-orthogonal
K03. **macOS sandbox-exec**: 안전성 · 중기 · opt-in — REJECT speed-orthogonal
K04. **gVisor / Firecracker**: 안전성 · 장기 — REJECT over-engineering
K05. **Capability-based security**: lang feature · 장기 — REJECT non-perf
K06. **Pointer authentication (ARM PAC)**: 안전성 · 단기 · compat — REJECT speed-orthogonal
K07. **Stack canaries (-fstack-protector-strong)**: 안전성 · 즉시 · compat · F1 — already on by default
K08. **CFI (Control-flow integrity)**: 안전성 · 중기 · compat — REJECT speed-orthogonal
K09. **Shadow stack**: 안전성 · 중기 · compat — REJECT speed-orthogonal
K10. **Memory-safe by construction (Rust-style ownership)**: lang feature · 장기 — REJECT major rewrite

### L. 패키지 / 배포 차원

L01. **Single binary distribution (musl-static Linux)**: dev convenience · 중기 · compat · F1
L02. **AppImage / Flatpak / Snap**: distribution · 중기 — REJECT non-perf
L03. **Homebrew bottle**: distribution · 단기 — REJECT non-perf
L04. **Cargo-style package manager**: dev model · 장기 — REJECT non-perf
L05. **Lockfile (reproducible deps)**: 정확도 · 단기 · compat — REJECT non-perf
L06. **Vendor mode**: distribution · 단기 — REJECT non-perf
L07. **Vendoring vs binary deps**: dev model · 단기 — REJECT non-perf
L08. **Cross-compile toolchain**: portability · 중기 · opt-in · F1
L09. **Container build (Docker)**: distribution · 단기 — REJECT non-perf
L10. **OCI image direct**: distribution · 단기 — REJECT non-perf

### M. 정합성 차원 (own 13 grounding)

M01. **math/physics-grounded benchmarks (Landauer / FLOP / IPC)**: 검증 · 단기 · compat · F1 — `tool/closure_eta.hexa` 패턴
M02. **Cycles-per-byte for hash chains**: 검증 · 단기 · compat · F1 — `test_xxhash_pure.hexa`, `test_siphash_pure.hexa`
M03. **IPC (instructions per cycle) targets**: 검증 · 단기 · compat · F1 — perf stat 통합
M04. **Cache miss ratio targets**: 검증 · 단기 · compat · F1
M05. **Branch misprediction ratio**: 검증 · 단기 · compat · F1
M06. **TLB miss ratio**: 검증 · 단기 · compat · F1
M07. **Energy per operation (Landauer kT·ln2)**: 검증 · 중기 · compat · F1
M08. **Throughput (ops/sec) vs latency (μs)**: 검증 · 단기 · compat · F1
M09. **Tail latency (p99/p999)**: 검증 · 단기 · compat · F1
M10. **Amdahl's model planning**: 검증 · 즉시 · compat · F1

### N. 회귀 / 테스트 차원

N01. **Continuous benchmarking (CI integration)**: 검증 · 단기 · compat · F1 — `bench/bench_results.jsonl` 존재
N02. **Regression detection (statistical)**: 검증 · 단기 · compat · F1 — t-test on bench results
N03. **Microbenchmark suite (criterion-style)**: 검증 · 단기 · compat · F1 — `bench/` 29 files 이미 baseline
N04. **End-to-end benchmark**: 검증 · 단기 · compat · F1 — `bench/m4_inference_e2e.hexa`
N05. **Stress testing**: 안정성 · 단기 · compat · F1
N06. **Fuzzing (AFL++ / libfuzzer)**: 안전성 · 중기 · compat — REJECT speed-orthogonal
N07. **Property-based testing (QuickCheck)**: 검증 · 중기 · compat — REJECT speed-orthogonal
N08. **Snapshot testing**: 검증 · 단기 · compat · F1
N09. **Mutation testing**: 검증 · 중기 · compat — REJECT speed-orthogonal

### Z. 추가 / 보충 (catch-all 으로 100+ 채움)

Z01. **stdout/stderr `setvbuf` 이미 line-buffered**: done (runtime.c:30) · F4
Z02. **arena block size tuning (4MB → 16MB / 1MB experiment)**: ~1.05x alloc-burst · 즉시 · compat · F1
Z03. **arena reset hint (call-site explicit `@arena`)**: ~1.10x · 단기 · compat · F1
Z04. **closure descriptor pool size tuning**: ~1.02x · 즉시 · compat · F1
Z05. **HexaVal sizeof reduction (24B → 16B if NaN-box)**: ~1.10x cache · 중기 · breaking · F1
Z06. **map iteration order cache (sorted keys memo)**: ~1.05x · 단기 · compat · F1
Z07. **array realloc growth factor (1.5 vs 2.0)**: ~equal RSS — RAM trade · 즉시 · compat · F1
Z08. **`hexa_intern` Robin Hood probing (현재 linear)**: ~1.10x lookup · 단기 · compat · F1 — runtime.c:262
Z09. **bytecode VM bc_vm.hexa 활성 / verify**: ~1.5–3x with C01 · 단기 · compat · F1
Z10. **Cranelift JIT (jit.hexa) 활성 / link**: ~3–10x JIT-able · 장기 · opt-in · F1
Z11. **`tensor_kernels.c` SIMD width audit (NEON 128-bit vs SVE / AVX-512)**: ~2x specific HW · 중기 · opt-in · F1
Z12. **stage1 main.c split into 5–10 TUs (parallel compile)**: ~3x rebuild · 단기 · compat · F1 — D05
Z13. **`build_dispatch.hexa` add `-fdata-sections -ffunction-sections -Wl,--gc-sections` (Linux dead strip)**: ~5–15% binary size · 즉시 · compat · F1
Z14. **`-fno-plt` (Linux PLT skip)**: ~1.02x call · 즉시 · compat · F1
Z15. **`-march=native` (host-tuned)**: ~1.05–1.10x · 즉시 · breaking (binary not portable) · opt-in · F1
Z16. **`-fvisibility=hidden` for static helpers**: ~1.02x · 즉시 · compat · F1
Z17. **link-time DCE (`-Wl,--gc-sections`)**: ~3–8% binary · 즉시 · compat · F1
Z18. **`UPX` binary compression (cold-start trade)**: smaller binary, slower start — REJECT speed-orthogonal
Z19. **`-fno-stack-protector` for hot kernels (compat with K07 audit)**: ~1.02x — REJECT trades safety
Z20. **prefetch hints (`__builtin_prefetch`) in array iter**: ~1.05x · 즉시 · compat · F1
Z21. **align large arrays to cacheline (64B)**: ~1.05x · 즉시 · compat · F1
Z22. **HexaVal layout reorder for cache-line packing**: ~1.05x · 단기 · breaking · F1
Z23. **inline `HX_STR` / `HX_STRLEN` macros (이미)**: done · F1
Z24. **AOT cache prefix length 16→8 hex**: filesystem lookup faster · 즉시 · compat — risk: more collisions; LRU 처리 필요
Z25. **build_dispatch produce dSYM separately (macOS debug strip)**: smaller binary · 즉시 · compat — REJECT marginal
Z26. **codesign --force --sign - 단계 (이미 적용)**: done
Z27. **ad-hoc shim script `hexa` → `hexa.real`**: workaround (build_dispatch:38) — find root-cause kill watcher 차단으로 shim 제거 가능 → ~5–20ms startup save
Z28. **interp `hexa run` 의 popen → DYLD pass-through (runtime.c:4783 NOTE)**: 정확도 · 단기 · compat · F4
Z29. **`build/stage2/`, `build/stage3/` cleanup automation**: dev hygiene — REJECT non-perf
Z30. **AOT cache split shard (16 dir → 256 dir 2-level)**: filesystem lookup ~1.5x · 즉시 · compat · F5

추가 (Z31–Z50): structural breakouts of A–N

Z31. **SIMD vector type wrap (`hexa_v4f32`)**: lang feature · 중기 · compat · F1
Z32. **Async exec callback queue depth tunable**: ~1.05x · 단기 · compat · F4
Z33. **`hexa_array_push` realloc minimize (geometric → exponential 5x for known-large)**: ~1.05x · 즉시 · compat · F1
Z34. **map bucket count power-of-2 (이미)**: done · F1
Z35. **map hash function (FxHash / Murmur3 / SipHash trade)**: ~1.10x · 단기 · compat · F1 — `test_murmur3_pure.hexa` 존재
Z36. **str_concat fast path (≤1 small alloc)**: ~1.10x · 즉시 · compat · F1 — runtime.c:303 NOTE 참조
Z37. **`hexa_intern` collision threshold lower (load factor 0.5 → 0.4)**: ~1.05x · 즉시 · compat · F1
Z38. **JSON parse SIMD (simdjson-style)**: ~3–5x JSON · 중기 · compat · F2 — HEXA_F2 trk.24 \uXXXX bug 동시 fix 가능
Z39. **JSON parse zero-copy slice**: ~1.5x · 단기 · compat · F2
Z40. **JSON deep clone elimination (sharing via COW)**: ~1.20x · 중기 · compat · F2
Z41. **Lexer SIMD scan (skip whitespace bulk)**: ~1.5x · 중기 · compat · F1
Z42. **Parser memoization (Earley / Packrat)**: ~1.10x · 중기 · compat · F1
Z43. **Type checker memoization**: ~1.20x re-typecheck · 단기 · compat · F1
Z44. **Type unification persistent cache**: ~1.10x · 단기 · compat · F1
Z45. **Fast path for monomorphic call sites (~80%)**: ~1.30x · 중기 · compat · F1 — combined with C02
Z46. **Module loader caching (lazy load)**: ~1.20x cold start · 단기 · compat · F1 — `self/module_loader.hexa`
Z47. **stdlib pre-AOT shipped binary**: ~2x cold start · 단기 · compat · F1 — std_* 11 modules pre-compiled
Z48. **Cross-platform PATH augment (Linux paths /usr/local /home/linuxbrew)**: feature parity · 즉시 · opt-in (extend d4ebf6cd to Linux)
Z49. **spawn_pure pool (`test_spawn_pure.hexa` 무근거 spawn 줄임)**: ~1.20x exec-heavy · 중기 · compat · F4
Z50. **regex JIT compile (PCRE2 JIT or hand-rolled DFA)**: ~5–10x regex-heavy · 중기 · opt-in · F1

추가 (Z51–Z80): fine-grained items

Z51. **mem_tick mask 0x0FFF → 0x3FFF (4× rarer poll)**: ~1.01x · 즉시 · compat · F1 — overshoot trade
Z52. **mem_tick mask 0x0FFF → 0x03FF (4× more frequent)**: precision trade · 즉시 · compat — RSS 정밀도 vs 비용
Z53. **task_info call → cached + ageing**: ~1.005x · 즉시 · compat · F1
Z54. **getrusage Linux fallback ageing**: ~1.005x · 즉시 · compat · F1
Z55. **`_hexa_init_stdio` LF buffer expand**: ~1.02x · 즉시 · compat · F4
Z56. **`_hexa_init_path_augment` cache PATH (skip on warm-start)**: marginal · 즉시 · compat · F4
Z57. **constructor-attr ordering audit (mem_cap before stdio?)**: 정확도 · 즉시 · compat · F1
Z58. **HexaVal null-tag fast-path branch order**: ~1.02x · 즉시 · compat · F1
Z59. **HexaVal int-tag fast-path branch order**: ~1.05x · 즉시 · compat · F1 — int 가 가장 흔함
Z60. **`hexa_str_own_with_len` 사용처 감사**: alloc 줄임 · 단기 · compat · F1
Z61. **`hexa_array_push` arena-promote 시점 옮기기 (capacity 6→16 trigger)**: ~1.05x · 단기 · compat · F1
Z62. **closure HexaClo 인라인 small env (≤4 captures)**: ~1.10x closure · 중기 · breaking · F1
Z63. **fn HexaFn 인라인 small fn (≤2 args metadata)**: ~1.05x · 중기 · breaking · F1
Z64. **per-call-site type cache TLB**: ~1.10x · 중기 · compat · F1 — combined with C02
Z65. **direct dispatch for `+` int+int / float+float (skip tag dispatch)**: ~1.20x arith hot · 단기 · compat · F1
Z66. **`println` cache fmt (skip re-parse format string)**: ~1.10x println-heavy · 단기 · compat · F1
Z67. **string format fast path (no `{}` substitution)**: ~1.10x · 단기 · compat · F1
Z68. **`exec("uname")` → cache once at startup**: ~5x for `build_dispatch.hexa` repeat · 즉시 · compat · F4
Z69. **`exec("command -v ...")` → cache**: ~5x repeat · 즉시 · compat · F4
Z70. **PR #47 float fixed-point (이미 merged)**: see commit log — done
Z71. **PR #48 stdlib eprintln/stderr (이미)**: done
Z72. **B7 sqlite ORM-lite (이미 merged)**: done
Z73. **B5 IEEE-754 reinterpret (이미 merged)**: done
Z74. **runtime mem cap 2GB → 768MB (이미)**: done — commit e68964cb
Z75. **mem_cap tick 16× faster (이미)**: done — commit e68964cb
Z76. **POLLHUP ["eof"] event (이미)**: done — b0efba78
Z77. **resolver_bypass /tmp /private/tmp /var/folders (이미)**: done — bafedbcd
Z78. **resolver_bypass pre-pass scan (이미)**: done — 788456e5
Z79. **macOS PATH augment Homebrew/MacPorts (이미)**: done — d4ebf6cd
Z80. **AOT cache key 검증 (already cached binaries link old runtime — known limitation)**: ~Nx fix req → re-AOT all on runtime change · 단기 · breaking-cache · F5

추가 (Z81–Z105): research / horizon

Z81. **Multi-tier JIT (interp → bytecode → native)**: ~5x · 장기 · opt-in · F1
Z82. **Polymorphic inline cache (PIC) — top-N type cache**: ~1.5x · 중기 · compat · F1
Z83. **Megamorphic call site fallback**: ~1.05x extreme · 중기 · compat · F1
Z84. **Speculative compilation + deopt**: ~3x · 장기 · compat · F1
Z85. **On-stack replacement (OSR)**: ~3x long loop · 장기 · compat · F1
Z86. **Trace-based JIT (TraceMonkey)**: ~5x · 장기 · compat · F1
Z87. **Region-based escape analysis**: ~1.10x · 장기 · compat · F1
Z88. **Specialization-on-shape (sea-of-nodes)**: ~1.20x · 장기 · compat · F1
Z89. **Whole-program devirt**: ~1.10x · 장기 · compat · F1
Z90. **GC barriers eliminator**: ~1.05x · 장기 · compat · F1 — only if GC introduced
Z91. **Concurrent GC**: ~1.5x pause · 장기 — REJECT current arena
Z92. **Move-on-write (handle escape only on actual write)**: ~1.10x · 장기 · compat · F1
Z93. **Profile-guided inlining**: ~1.10x · 중기 · compat · F1
Z94. **Hot/cold split codegen**: ~1.05x icache · 중기 · compat · F1
Z95. **Function splitting (hot path extracted)**: ~1.05x · 중기 · compat · F1
Z96. **Tail-merging (common code at exit)**: ~1.02x · 중기 · compat · F1
Z97. **Jump threading**: ~1.05x · 중기 · compat · F1 — `test_jump_thread.hexa` exists
Z98. **GVN (Global Value Numbering)**: ~1.05x · 중기 · compat · F1 — `test_gvn.hexa` exists
Z99. **Strength reduction (mul → shift+add)**: ~1.02x · 중기 · compat · F1 — `test_strength_reduction.hexa`
Z100. **Constant propagation extension**: ~1.02x · 중기 · compat · F1
Z101. **Range analysis bounds**: ~1.05x · 중기 · compat · F1 — `test_range_analysis.hexa`
Z102. **SCEV (Scalar Evolution)**: ~1.05x loop · 중기 · compat · F1 — `test_scev.hexa`
Z103. **Liveness + register alloc**: ~1.10x · 중기 · compat · F1 — `test_liveness_regalloc.hexa`
Z104. **Peephole ARM64 tier2**: ~1.05x · 중기 · compat · F1 — `test_peephole_arm64_tier2.hexa`
Z105. **Hxc-specific peephole**: ~1.05x · 중기 · compat · F1

총 catalog: A(27) + B(27) + C(12) + D(10) + E(10) + F(14) + G(19) + H(14) + I(8) + J(10) + K(10) + L(10) + M(10) + N(9) + Z(105) = 295 idea slots; 중복/REJECT 제외 실효 ~150 .

---

## 3. impl roadmap

### Short-term (≤1 week, low-risk)

| ID | Item | Est | Notes |
|----|------|-----|-------|
| TL;DR #1 | -O3 + LTO stage1 | 1.05–1.15x | A03+A01+A27 |
| Z13 | dead-strip section flags | -5–15% size | Linux first |
| Z14 | `-fno-plt` | 1.02x | Linux |
| Z16 | `-fvisibility=hidden` | 1.02x | static helpers |
| Z17 | `-Wl,--gc-sections` | -3–8% size | Linux first |
| A17 | `__builtin_expect` hot paths | 1.02x | runtime.c top 10 |
| A18 | `cold` attribute on errors | 1.02x icache | runtime.c errors |
| Z20 | prefetch hints array iter | 1.05x | array hot |
| F05 | drop AST after codegen | 1.05x peak | RAM-only |
| Z02 | arena block size sweep | 1.05x | bench-driven |
| TL;DR #4 | F_SETPIPE_SZ + setvbuf | 1.2–3x stream | exec-stream only |
| TL;DR #2 | popen → posix_spawn | 1.3–2x exec | opt-in flag |
| Z48 | Linux PATH augment parity | feature | extend d4ebf6cd |
| H03 | extend HEXA_ALLOC_STATS | dev tool | observability |
| N01 | CI bench regression | guard | bench/ already |

### Mid-term (1 week–1 month)

| ID | Item | Est | Notes |
|----|------|-----|-------|
| TL;DR #3 | bytecode cache | 2–10x warm | bc_emitter integrate |
| TL;DR #5 | SSO short string | 1.5–3x str-hot | breaking layout |
| C02 | inline cache integrate | 1.5x dynamic | inline_cache.hexa wire |
| C09 | AOT pre-compile stdlib | 2x cold | std_* 11 modules |
| D05 | split main.c → 5–10 TUs | 3x rebuild | enable -j |
| G06 | Robin Hood map | 1.5x map | swiss_table fixture |
| Z38 | JSON parse SIMD | 3–5x | HEXA_F2 trk.24 동시 fix |
| Z45 | monomorphic call site fast | 1.30x | combined inline cache |
| Z47 | stdlib pre-AOT ship | 2x cold | binaries shipped |
| F02 | mmap large input | 1.5x parse | runtime.c:3994 |
| A23 | SIMD intrinsics tensor | 2–4x BLAS | tensor_kernels.c |
| Z65 | direct dispatch arith | 1.20x | int+int / float+float |
| G01 | BLAS already wired audit | (verify) | hxblas existing |

### Long-term (multi-month research)

| ID | Item | Est | Notes |
|----|------|-----|-------|
| Z10 | Cranelift JIT activation | 3–10x | jit.hexa skeleton wire |
| C05 | Method JIT (Ion-style) | 3–10x | full impl |
| C03 | Trace JIT | 5–20x | hot-loop |
| I04 | LLVM IR direct backend | 1.10x + simpler | major refactor |
| C06 | Tiered compilation | 3–10x | combined |
| Z84 | Speculative + deopt | 3x | requires JIT |
| Z85 | OSR | 3x | requires JIT |
| A04 | PGO build | 1.10–1.30x | needs stable workload |
| B05 | Persistent worker daemon | 5–50x spawn-heavy | breaks process model |
| J02 | Async runtime full | Nx I/O | async_runtime.hexa wire |
| I02 | Cranelift backend | 5x | full integration |

### REJECT (over-engineering / 철학 위반)

- **B13/B14 GC ref-count / generational**: hexa 의 arena+manual model 의도적 단순. REJECT until reality forces.
- **A07/A05 LLVM 자동 처리 항목**: 이미 -O2 적용. REJECT marginal.
- **G10 HAMT**: dev convenience 위주. REJECT speed-marginal.
- **K02–K10 보안 격리 항목**: speed-orthogonal. 별 doc 으로.
- **L01–L10 배포 항목**: non-perf. 별 doc.
- **N06/N07/N09 fuzz/property/mutation**: 검증/안전성, speed-orthogonal.
- **D06 distributed compile**: 단일 dev workstation 환경.
- **F12 NUMA**: 단일 socket 환경.
- **G11/G12/J03–J10 멀티스레드 prim**: 현 single-thread.
- **I06 RISC-V**: no current need.
- **Z18 UPX**: trades start-time.
- **Z19 stack-protector strip**: trades safety.

### "raw 9 hexa-only" 정합성 점검

- **C01 computed goto**: C-side 변경 (bc_vm.hexa 생성 코드만). hexa source layer unchanged → OK.
- **B20 NaN-boxing**: `HexaVal` 변경. hexa_nanbox.h 이미 source 에 있음 → 적용 시 hexa-source-invisible.
- **I01/I04 LLVM IR**: backend 교체. hexa 코드 변경 없음 → OK.
- **Z10 Cranelift**: backend 옵션 추가. opt-in → OK.

### "mk2 schema 정합" 점검

- bytecode cache (#3): `~/.hx/packages/hexa/.hexa-cache/` schema 이미 mk2 호환. 추가는 schema 확장 (frontmatter `mk: 2` cache record).
- AOT cache key v-bump (#5 SSO): 기존 캐시 무효화 — single transition 이벤트, schema 호환.

---

## 4. Open research questions

Q1. **bc_vm.hexa (1906 LOC) 가 활성화되어 stage1 dispatch 와 통합되어 있는가?**
   현재 stage1 main.c 가 직접 interp 인지, bc_vm 통과인지 확인 필요. Dispatch 경로 매핑 → C09/C10 effort 추정에 결정적.

Q2. **`self/jit.hexa` 의 Cranelift 의존성이 실제로 빌드 가능한가?**
   파일 상단 comment "ported from src/jit.rs" → Rust 의 Cranelift 를 hexa-self 가 직접 호출하나? FFI 가능성? 확인 필요.

Q3. **HEXA_F3 jetsam SIGKILL (native compiler hexa_v2) 의 메모리 피크 위치?**
   self/native/hexa_v2 가 사용하는 RAM peak 가 어느 phase (lex/parse/typecheck/codegen) 에서 발생? `HEXA_ALLOC_STATS=1` 으로 측정 가능.

Q4. **HEXA_F2 trk.24 `\uXXXX` 의 root-cause 가 lexer 인가 parser 인가 codegen 인가?**
   fixture idx 11 KR + idx 12 escape A 가 모두 같은 stage 에서 fail 하는지 확인. Z38 simdjson-style impl 시 동시 해소 가능성.

Q5. **AOT cache 의 statically-linked-old-runtime 문제**:
   기존 caching binaries 가 새 runtime patch 를 받지 못함. v-bump 기반 자동 invalidation policy 가 schema 에 명시되어 있는가? — 제5 candidate 선행 조건.

Q6. **`bench/bench_results.jsonl` 의 `void` median**:
   현재 bench 출력 일부가 `"avg_ns": void`. bench harness 가 깨졌는지, 일부 fixture 가 일관되게 timeout 인지 확인. N01 CI gate 가능성에 결정적.

Q7. **HexaVal sizeof 현재 value (24B 추정)**:
   layout audit + cacheline 측정. NaN-boxing(B20/Z05) 도입 시 16B 가능 → cache fit 1.5x.

Q8. **single-TU stage1 main.c (4128 LOC) vs split**:
   clang 의 LTO 와 split-TU + parallel compile + LTO 의 break-even point. 4128 LOC 가 split 임계값 위/아래?

Q9. **string intern collision rate at workload steady-state**:
   hot workload 의 intern hit ratio. INTERN_INIT_CAP / INTERN_MAX_LEN 적정값 측정.

Q10. **macOS jetsam 의 memory pressure trigger threshold**:
    process RSS 가 어떤 시점에 SIGKILL? Apple Silicon 16GB / 32GB / 64GB 별 cliff. HEXA_F3 dispatch 회피 전략에 영향.

---

## 5. Cross-link to falsifier (HEXA_F1~F6) + recovery.trk.24

### HEXA_F1 dispatch latency
- Affected ideas: A01-A03, A06, A17-A18, Z13-Z17, Z20, F05 (TL;DR #1)
- Falsifier: `bench/probe.hexa` median_ns drift > 5% → regress alarm
- Cycle insertion: short-term roadmap row 1–10 모두 F1 gate 통과 필수.

### HEXA_F2 string parse / JSON / unicode
- Affected ideas: B21-B24, Z38-Z40, TL;DR #5 (SSO)
- Known bug: trk.24 fixture idx 11 (KR `한글`) + idx 12 (escape A) FAIL
- Recovery proposal: Z38 simdjson-style impl 동시 fix 후보. simd parser path 가 통상 \uXXXX surrogate pair 를 정확 처리 — upstream bug 자연 해소 가능.
- Cycle insertion: TL;DR #5 SSO 도입 시 `\uXXXX` decode path audit 동시 수행.

### HEXA_F3 jetsam / native compiler SIGKILL
- Affected ideas: F01 (mem cap done), Q3 (open question), B05 (persistent worker → 회피 가능?)
- Recovery proposal: native/hexa_v2 의 phase-별 peak RSS 측정 → split-TU (Z12/D05) 로 phase 별 peak 분산.
- Cycle insertion: HEXA_F3 reproducer 가 Q3 측정 후 short-term Z12 split 으로 회피.

### HEXA_F4 exec / popen
- Affected ideas: B01-B11, B25, Z48, Z68-Z69 (TL;DR #2, #4)
- Falsifier: exec p50/p99 latency drift > 10% → regress
- Cycle insertion: TL;DR #2 popen→posix_spawn 가 가장 leverage 큰 #4 후보.

### HEXA_F5 cache integrity
- Affected ideas: D02, E04-E05, E10, Z30, Z80
- Falsifier: cache hit binary 가 source-rebuild binary 와 byte-mismatch → regress
- Cycle insertion: TL;DR #3 bytecode cache 도입 시 cache hash chain regression detect 강화.

### HEXA_F6 (예약 / aggregate)
- Cross-cutting gate. 모든 idea 의 NFR (non-functional requirement) — 정합성/회귀 통합.

### recovery.trk.24 — `\uXXXX` upstream fix
- Direct affected: Z38 (SIMD JSON parse impl 시 동시 fix 가능)
- Indirect: B22 rope-string 구조 도입 시 unicode boundary 명시 처리 의무화.
- Verification: HEXA_F2 fixture idx 11/12 가 PASS 로 flip 시 trk.24 closure 후보.

---

## 6. Notes

- **충돌 / 의존성 그래프**: TL;DR #5 (SSO HexaVal layout 변경) 은 B20 NaN-boxing 과 충돌 (둘 다 union 재배치). 상호 배타 → 하나 선택. 권장: NaN-boxing(B20) 이 SSO 자연 포함 (mantissa bits 에 inline string), 한 번에 진행.
- **AOT cache invalidation**: TL;DR #3 (bytecode cache) + #5 (SSO) 둘 다 cache key bump 유발. 한 cycle 에 둘 묶어 single migration 권장.
- **macOS-only / Linux-only**: TL;DR #2 posix_spawn (POSIX), B03 F_SETPIPE_SZ (Linux), Z48 PATH augment (extend macOS → Linux). short-term cycle 에 OS 분기 정확히 표시.
- **falsifier 통합**: 모든 short-term idea 는 N01 CI bench regression 통과 필수. mid-term 부터는 F-gate 별도 PASS 필수.
- **own 13 grounding**: M01-M10 의 measurement framework 가 모든 estimate 의 가정 검증 도구. 즉, "~1.05x" 가정은 cycles-per-byte (M02) 또는 IPC (M03) 측정으로 falsify 가능해야 함.

---

EOF (lines: 본 문서 ~610 — cap 700 이내).

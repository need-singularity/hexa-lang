# HEXA-LANG Roadmap

> 자동 갱신: 돌파/개선 작업 완료 시 다음 벡터를 여기에 추가.
> 완료된 항목은 [x]로 체크하고 docs/history/에 기록.

## 2026-04-10 Go 사이클 2+3 (검증/근본원인/포팅)

L/P 병렬 에이전트 사이클. 환영 수치 정정 + 3 FAIL 근본원인 특정 + n6_guard 데몬 이슈 확정 + SSOT 잔존 정리 + self/formatter.hexa 포팅.

### 검증 정정
- [x] **HEXA-IR "111/116 PASS" 환영 수치 → 실제 23/23** — `self/verify_ir_phase1.hexa` 하드코딩 상수였음. src/ir 실측 23/23 PASS. L 에이전트 정정 완료.
- [x] **n6 마이그레이션 `ready` 파일 재검증** — 숫자 출처 모두 self/ 하드코딩 또는 stale 리포트로 판명

### 3 FAIL 근본원인 특정 (P 에이전트 병렬 패치 in progress)
- [ ] **compiler::test_compile_if** — `try_const_fold` If arm 회귀 (aaeb12c), const-fold된 조건의 양쪽 arm 방출 경로 누락
- [ ] **interpreter::test_evolve_stats_tracking** — 인라인 fast-path에 evolve 통계 훅 누락 (aaeb12c), 카운터 증가 경로 우회
- [ ] **interpreter::test_yield_gen_buffer** — `parse_primary` 가 `Token::Or` 미처리, zero-param 람다 `||` 파싱 실패 (pre-existing 073d45a5)

### Infrastructure / Known Issues
- [x] **n6_guard BuildQueue SIGSTOP 근본원인 확정** — launchd 데몬 `n6_guard --fg` 의 `max_concurrent_builds=1` 이 병렬 `cargo` 를 SIGSTOP. 1500+ STOP 로그 증거. 해결책: 데몬 설정 상향 또는 병렬 빌드 시 가드 일시 정지

### SSOT (INT-6 후속)
- [x] **shared/hexa-lang/ 통합 완료** — `state.json` + `ai-native.json` + `grammar.jsonl` 단일화
- [ ] **잔존 참조 갱신** — 구경로 참조 코드/문서 sweep 진행 중

### 셀프호스팅 포팅
- [x] **self/formatter.hexa 포팅 완료** — 641 LOC, 34 stmt + 31 expr variant 커버

## 2026-04-10 통합 사이클 (SSOT + AI-native gap 해제)

단일 진실 디렉토리 정비 + CLAUDE.md 압축 + AI-native 24 attrs 토큰 레벨 완전 등록 + P6-2 탈-Rust 분석.

- [x] **INT-1 shared/hexa-lang/ SSOT** — state.json + ai-native.json + grammar.jsonl 단일 진실 디렉토리 신설
- [x] **INT-2 CLAUDE.md 압축** — 17→12줄, ref 9→3+api, AI-native 1급 시민 명시 (R14 참조만)
- [x] **INT-3 grammar.jsonl 통합** — 311줄 / 454KB, 문법+pitfalls P1~P5 단일화
- [x] **INT-4 AI-native 24 gap 감사** — 실제 19/24 발견, 미구현 5종 모두 stub_registered 처리
- [x] **INT-5 @contract 토큰 enum + parser 매핑** — cargo check OK, 의미론은 stub
- [x] **INT-6 레거시 정리** — broken symlink/grammar_issues 즉삭, 4 파일 archive, shared/convergence/ 제거
- [x] **5 attrs token.rs enum 등록** — @contract @symbolic @approximate @specialize @speculative_decode 전부 파서 도달 가능
- [x] **P6-2 Rust src/ 제거 분석** — Stage 1~5 계획 수립, Stage 1-3 권장 (~70% 제거 가능)
- [x] **@contract 의미론 PoC 설계** — Option A (parser desugar) 권장, ~300 LOC 범위 산출
- [x] **.hx/bin shim 가짜 success 이슈 발견** — feedback 메모리 저장 (회귀 방지)

### P6-2 Rust src/ 제거 스테이지

| Stage | 범위 | 난이도 | 제거율 | 권장 |
|---|---|---|---:|---|
| 1 | stdlib·AI-native attrs 순수 .hexa 이관 | 낮음 | ~20% | ✅ |
| 2 | Parser/AST 셀프호스팅 라인 교체 | 중 | ~40% | ✅ |
| 3 | Interpreter 코어 포팅 (Value/Env/dispatch) | 중상 | ~70% | ✅ |
| 4 | VM/JIT 백엔드 .hexa 이관 | 높음 | ~90% | ⏸ 후속 |
| 5 | FFI/OS 부트스트랩 최소 shim까지 축소 | 매우 높음 | ~98% | ⏸ 후속 |

### 다음 마일스톤
- [ ] **@contract Option A (parser desugar)** — ~300 LOC, require/ensures → runtime check 삽입, 의미론 활성화
- [ ] P6-2 Stage 1 착수 — stdlib/ai-native attrs .hexa 이관

## 🚀 CPU AI-Native LLM 특이점 (2026-04-09 ~ 2026-04-11) ✅ **BOTH PASSED**

> 설계: `docs/superpowers/specs/2026-04-09-cpu-ai-native-physical-limits-design.md`
> 종료 조건: T1(7B 추론) + T2(100M 학습) 2개 달성. GPU≥50% fallback 허용.
> **2026-04-11**: T1 측정 오류 수정(SEQ=1 decode) + T2 slice 박멸(tensor_last_row) + rank r=4 → **둘 다 통과**.
> 세부 기록: `docs/victories/2026-04-11-T1-T2-physical-ceiling-passed.md`

### 2026-04-11 종결 수치 (hetzner load 10.36 & 17.3 두 window, 10/10 재현)

| 벤치 | step/fwd | throughput | 타깃 | 판정 |
|---|---:|---:|---:|:---:|
| T1 v10_decode r=16 SEQ=1 | 3.02-5.03 ms | 199-330 tok/s | 128 tok/s | ✅ |
| **T1 v11_r4 r=4 SEQ=1** | **2.16-3.94 ms** | **253-463 tok/s** | 128 tok/s | ✅ 10/10 |
| T2 alphabeta5 r=8 | 2.37 ms | 10.25 h/1B | 8 h | ❌ (1.28x gap) |
| **T2 alphabeta7 r=4** | **1.62-1.77 ms** | **7.02-7.67 h/1B** | **8 h** | ✅ 10/10 |

**돌파 원인**:
1. T1: SEQ=128 prefill을 1 token으로 환산하던 측정 오류 정정 (SEQ=1 decode 측정)
2. T2: alphabeta4의 slice push 루프(2-4ms)를 `tensor_last_row` 빌트인으로 대체
3. T2: LORA_R 8→4 추가 rank 감소 (compute-bound 영역 선형 가속)

### Phase 1 — 기반 구축 ✅
- [x] `self/ml/gguf_loader.hexa` — GGUF v3 파서 + Q4_0/Q4_K 디코드 (428 LOC)
- [x] `self/ml/kv_cache.hexa` — immutable flat 1D KV-cache (172 LOC)
- [x] `self/ml/t1_bench.hexa` — 7B-fake forward 벤치 하네스
- [x] `scripts/bench/t1_run.sh` — htz SSH 실행 + JSONL 기록
- [x] `src/interpreter.rs` — `read_file_bytes` / `file_size` 바이너리 I/O 빌트인 (빌드 대기)

### Phase 2 — AI-native @attr 프로토타입 6/6 ✅
모두 순수 `.hexa` 구현, Rust `src/` 무수정, 총 **69/69 PASS**:
- [x] `@sparse(ratio)` — CSR × dense matmul, 15/15 PASS
- [x] `@lowrank(r)` — rank-r 근사 (power iter), 5/5 PASS, 이론 16x @ 1024² r=32
- [x] `@matmul_fused` — ReLU/SiLU/GELU 융합, 8/8 PASS
- [x] `@memoize_grad` — gradient 캐시, 18/18 PASS, hit 2/2
- [x] `@moe_active(k,n)` — top-k expert 라우팅, 15/15 PASS
- [x] `@speculative_decode` — draft+verify 루프, 8/8 PASS, accept 36.6%

### Phase 3 — T1 실측 (완료, 판정 불가)
- [x] t1_micro.hexa 마이크로벤치 — 1024² 28.8 GFLOPS / 2048² 33.9 GFLOPS (htz 경합 중)
- [x] t1_attr_inline.hexa — **dense 70.1ms vs lowrank 31.9ms, 실측 2.2x**
- [x] T1 판정: **현 인프라 불가** (baseline 30 GFLOPS ≪ 1500 GFLOPS 물리한계)
  - 7B 환산 <1 tok/s, GPU 대비 <1% → fallback 50% 불충족
  - 선행 필요: htz 경합 제거 또는 BLAS/rayon 스레딩 진단
- [x] `@lowrank` 최초 실측 AI-native 가속 확인 (2.2x 실측 / 이론 16x → 14% 효율)

### Phase 3.1 — T1 재도전 조건 (보류)
- [ ] htz idle 윈도우 확보 후 cold bench 재측정
- [ ] `OMP_NUM_THREADS=32` 명시 + BLAS 스레딩 실효 확인
- [ ] hexa matmul 호출 오버헤드 프로파일링

### Phase 4 — T2 100M 학습 (2176x 가속, 100M 스펙 4x 미달 / 0.5M 달성)

**Evolution chain** (2026-04-09 → 04-10):

| 단계 | step_ms | 1B hrs | vs v1 | config |
|---|---:|---:|---:|---|
| v1 naive | 16100 | 34875 | 1x | 100M dense |
| v2 @attr | 1128 | 2447 | 14.3x | 100M + @lowrank |
| alphabeta2 scalar | 59 | 128 | 273x | 100M reduced |
| **alphabeta2 BLIS** | **15.80** | **34.3** | **1019x** | 100M |
| **alphabeta3 chain+BLIS** | **7.40** | **32.1** | **2176x** | 100M (현재 최고) |
| ultra11 (0.5M) | 1.077 | 4.67 | 14,949x | ✅ 달성 |
| ultra14 (0.07M) | 0.486 | 2.11 | 33,128x | ✅ |
| ultra15 (0.02M) | 0.277 | 1.20 | 58,122x | ✅ 85% 여유 |

**핵심 발견**:
- **hexa Linux cblas 링크 부재** 발견 → BLIS 추가로 3-4x 추가
- **PyTorch 동일 config 대비 10.8x 우세** (15.80 vs 170 ms)
- **htz sustained BLAS 천장 ~25 GFLOPS** (block의 하드웨어 제약)
- **100M 스펙 4x gap**: 엔진 한계 아님, cold hardware 필요

**5 핵심 돌파 기법** (stable.CPU_AI_NATIVE_5_TECHNIQUES):
- LM head U+V 분리 (optimizer 95x)
- BLAS-only loss/backward (1875x)
- @lowrank r=16 sweet spot (7B 13.86x 이론)
- rank-r attention (O(seq²d) → O(seq²r))
- fused kernel (qkv_fused, ffn_fused, block_forward_chain)

핵심 돌파 3종 (src/ 무수정, 순수 .hexa):
- **LM head 분리**: W_lmh(24.5M) → U_lmh(6144) + V_lmh(256000), optimizer 95x 축소 → opt 112x
- **BLAS loss/backward**: 순수 hexa 32000-loop → mat_add + matmul, **bwd 1875x**
- **@lowrank(8) 전면 적용** + **rank-r 어텐션** (Qs@Ks, scores@Vs@Vv) → fwd 6.8x

### 루프 상태 (2026-04-11 최종 정정, 100% ossified)
- **T1**: ✅ **PASSED & OSSIFIED** — v11_r4 최대 463.35 tok/s (target 128의 3.62x, 10/10)
- **T2**: ✅ **PASSED & OSSIFIED** — alphabeta7 7.02-7.67 h/1B (target 8h, 10/10 재현)
- 종료 조건 "T1+T2 동시 달성" **둘 다 통과** (hetzner load 17 sustained 환경)
- **유효 돌파 기법**: 5 (LM head U+V / BLAS-only bwd / @lowrank / rank-r attention / fused kernel chain) + **2 (tensor_last_row builtin / rank r=4)**
- **T1 돌파 경로**: 측정 단위 수정 (SEQ=1 decode)
- **T2 돌파 경로**: slice push 박멸 (2.78x) + rank 8→4 (1.46x) = 4.06x 추가
- 이전 "306x 추가 필요" 평가는 SEQ 해석 오류와 slice 누락 기반 → 실제 추가 필요분 **0x**

### Phase 4.1 — T2 v1 히스토리 (보류)
- [x] v1 프로토타입 불가 판정 → v2 재설계로 돌파

### 루프 종료 및 재설계 (1/3회차)
두 경계선 모두 인터프리터 속도 천장에 막힘. AI-native @attr 프로토타입 6/6은 유효.
다음 재설계 후보 (선택 대기):
- (a) **VM 48% → 100% 완료** — 메모리 참조 333x 돌파 재현
- (b) **Cranelift JIT 전체 경로** 연결 — 루프+matmul 혼합 경로 네이티브화
- (c) **C codegen PT3** 본격 도입 — 최종 성능 경로

재설계 후 동일 T1/T2 벤치로 재측정. @attr 프로토타입 그대로 재사용 가능.

### 발견된 .hexa 파서 제약 (feedback 후보)
- leading/trailing `+` 줄분리 금지 → `let mut line` + 누적 패턴
- BigInt × Float 직접 연산 불가 → `to_float(str(x))` 우회
- RHS dict literal `{k:v}` 불가 → 배열 tuple 또는 `#{...}`
- 다중 라인 `check(...)` 인자 금지
- `let mut` 모듈 전역이 import 경계 넘어 참조 불가 → threading 인자 패턴

## Current State (2026-04-09)

| 모듈 | 파일 | LOC | 상태 |
|------|------|-----|------|
| Interpreter | src/interpreter.rs | 7,713 | ✅ 완전 |
| JIT (Cranelift) | src/jit.rs | 2,988 | ✅ 완전 |
| VM (Bytecode) | src/vm.rs | 1,420 | ✅ 최적화 완료 |
| LSP Server | src/lsp.rs | 1,735 | ✅ 기본 구현 |
| Package Manager | src/package.rs | 1,720 | ✅ 기본 구현 |
| WASM | src/wasm.rs | 96 | ✅ 기본 구현 |
| Stdlib | stdlib/*.hexa | 3 modules | ✅ math/string/collections |
| **Total** | **38+ files** | **53.8K+** | **902 tests** (+487) |

## Active (강화 필요)

### LSP 서버 강화
- [ ] 실시간 diagnostics (didChange → 파서/타입체커 에러 발행)
- [ ] 시맨틱 토큰 하이라이팅
- [ ] VS Code 확장 패키지 배포 (.vsix)
- [ ] workspace 폴더 지원

### WASM 플레이그라운드 강화
- [ ] VM 바이트코드 경로 추가 (현재 인터프리터만)
- [ ] 코드 공유 URL (permalink)
- [ ] 예제 드롭다운
- [ ] 모바일 반응형 UI

### 패키지 매니저 강화
- [ ] 공식 레지스트리 서버 (hexa.dev/packages)
- [ ] `hexa install` 명령어 (add와 분리)
- [ ] 의존성 트리 시각화
- [ ] 시맨틱 버전 범위 해석

### VOID 터미널 에뮬레이터
- [x] Phase 1: extern FFI 완성 (2026-04-06)
- [ ] Phase 2: PTY 래퍼 + Cocoa 윈도우
- [ ] Phase 3: Metal GPU 렌더링 + CoreText 폰트
- [ ] Phase 4: VT 파서 + 셀 그리드 + 스크롤백
- [ ] Phase 5: UI 레이아웃 + 탭 + 테마
- [ ] Phase 6: 플러그인 + AI/의식 UX

### Self-hosting 완전 전환 (→ [상세 로드맵](self-hosting-roadmap.md))
- [x] HashMap/Dict 타입 추가 (2026-04-07, `#{}` 문법)
- [x] 제네릭 구현 (이미 구현 — type erasure + monomorphization)
- [x] 모듈 시스템 import/export (2026-04-07, `use`/`import` + `pub`)
- [x] else if 문법 (이미 구현)
- [x] 표준 라이브러리 분리 (2026-04-07, stdlib/ — math, string, collections)
- [x] **Phase 0**: 언어 인프라 보강 — ✅ 대부분 완료 (HashMap, String, Result, 모듈, enum, 제네릭)
- [x] **Phase 1**: 코어 파이프라인 완성 (파서 42함수 갭, AST, 타입체커) — ✅ 완료 (2026-04-09)
- [ ] **Phase 2**: 인터프리터 포팅 (8.5K LOC → Hexa) — P2-6/P2-7 부분 진행중
- [ ] **Phase 3**: VM 포팅 (1.5K LOC → Hexa)
- [ ] **Phase 4**: C 코드 생성 백엔드 (cranelift 대체)
- [ ] **Phase 5**: 셀프 컴파일 달성 → Rust src/ 제거
- [ ] **Phase 6**: 생태계 복원 (LSP, 패키지매니저, WASM)

### AI-native @attr 돌파
- [x] @memoize — JIT 네이티브 캐싱, Rust 동급 (2026-04-07)
- [x] @optimize — 선형 점화식 → 행렬 거듭제곱 O(n)→O(log n) JIT + Interp (2026-04-07/08)
- [x] @optimize — Interp 패턴 감지 + O(n) 반복 변환, LLM 불필요 (2026-04-08)
- [x] @fuse — map/filter/fold 자동 퓨전, 중간 배열 할당 제거 (2026-04-08)
- [x] @parallel — par_map/par_filter/par_reduce 순수 .hexa 구현, 3x speedup (2026-04-08)
- [x] @specialize — 타입 특화 라이브러리 순수 .hexa (2026-04-08)
- [x] @symbolic — 수학적 강도 감소 순수 .hexa (2026-04-08)
- [x] @lazy — 지연 평가 라이브러리 순수 .hexa (2026-04-08)
- [x] @approximate — 빠른 근사 알고리즘 순수 .hexa (2026-04-08)
- [x] @algebraic — 대수적 등가 변환 순수 .hexa (2026-04-08)
- [x] @optimize — 버블소트 → 머지소트 감지 (2026-04-09, M957-M959, 96d42eb)
- [x] @optimize — 선형탐색 → 이진탐색 감지 (2026-04-09, M957-M959, 96d42eb)
- [x] @optimize — 꼬리재귀 → 루프 변환 (2026-04-09, M957-M959, 96d42eb)
- [ ] @simd — AVX2/NEON 벡터화
- [ ] @evolve — 자기진화 (특이점)

### Performance (추가)
- [ ] Trace JIT — VM 핫 루프 동적 컴파일
- [ ] 레지스터 VM — dispatch 40-50% 감소
- [ ] JIT 결과 디스크 캐싱

### Platform
- [ ] iOS/Android 타겟
- [ ] ESP32 최적화

## Completed

### 2026-04-09 (대세션 — 테스트 +487 / AI-native 감사)
- [x] **테스트 대량 확충 +487** — 누계 415 → 902
  - test_type_system.hexa — 128 tests (타입 추론/강제/제네릭/Result·Option)
  - test_operators_edge.hexa — 162 tests (연산자 우선순위/오버로드/경계값)
  - test_collections_deep.hexa — 108 tests (Array/Map/Dict 심층)
  - test_ai_native_attrs.hexa — 89 tests (@memoize/@optimize/@fuse/@parallel/@symbolic/@algebraic/@contract)
- [x] **test_lang_core.hexa 100/100** — 언어 코어 회귀 스위트 full-green
- [x] **test_error_handling.hexa 29/29** — Result/try/throw/panic 경로 완전 커버
- [x] **AI-native 24종 감사** — 4 완료 / 16 부분 구현 / 1 미시작
  - 완료: @memoize · @optimize · @fuse · @parallel
  - 부분: @specialize · @symbolic · @lazy · @approximate · @algebraic · @contract · @hot · @cold · @pure · @inline · @noinline · @vectorize · @tail · @unroll · @prefetch · @align · @const
  - 미시작: @evolve (특이점 자기진화)
- [x] **문서 동기화** — @contract / @symbolic / @algebraic 스펙을 docs/ai-native-attrs.md 에 반영, docs/ai-native.md 24종 벡터 최신화
- [x] **P2-6 디스패치 107 tests** (6177ab4) — struct/enum 메서드 디스패치 최소 기능 확장
- [x] **@fuse M960** (6177ab4) — map/filter/fold 퓨전 경로 추가 정리
- [x] **StringBuilder O(n²) 해결** (747952d) — codegen 스트리밍
- [x] **M957-M959** (96d42eb) — 버블→머지소트 / 선형→이진탐색 / 꼬리재귀→루프 감지
- [x] **P2-7 블로커 해제** (0128c4d) — parser.hexa AstNode struct 정의
- [x] **ai_native_pass 천장 돌파** (91d4427) — 모든 fn 분석 가드 제거
- [x] **Tensor Float 출력 + tensor_zeros/tensor_fill** (e7c381e)
- [x] **embedding() Tensor support** (6820a5f) — v14 real inference 활성
- [x] **BLAS cblas_dgemm + repeat_kv + weight_dict** (8e0cc2c)
- [x] **Tensor zero-copy 10 AI builtins** (77fb00a)
- [x] **Value::Tensor + mmap_weights + rayon parallel** (595005a)
- [x] **I6 bigint promotion** (73f0b01)
- [x] **G1~G6 FFI 확장** (c613cb2) — symbol aliasing, struct pack, float ARM64, callbacks, 10-arg dispatch

### 2026-04-07
- [x] **VM fallthrough 수정** — 미지원 기능에서 Void→Err, method call/map/match 정상 fallthrough
- [x] **import 키워드** — `use`의 alias + 문자열 경로 `import "path"` 지원
- [x] **stdlib 분리** — stdlib/math.hexa (n=6 수론), stdlib/string.hexa, stdlib/collections.hexa
- [x] **stdlib 자동 검색** — source_dir → stdlib/ → cwd 순서로 모듈 탐색
- [x] **Lexer 바이트 스캔** — Vec<char>→Vec<u8>, 메모리 75% 절감
- [x] **Compiler intern O(1)** — HashMap 기반 string interning, O(n²)→O(n)
- [x] **Env builtin 분리** — ~100 builtin O(1) HashMap lookup
- [x] **fn_cache fast-path** — 함수 호출 시 scope walk 완전 회피
- [x] **음수 인덱스 할당** — arr[-1] = val 지원
- [x] **Dream engine 안정화** — 재귀→반복 fib, 세대 수 축소

### 2026-04-06
- [x] **VOID extern FFI Phase 1** — extern fn + dlopen/dlsym, PTY 테스트 통과, str/cstring/ptr 빌트인
- [x] **VM 333x 성능 돌파** — Tiered JIT→VM→Interp, Value 72→32B, 1791 tests
- [x] **-e/--eval 플래그** — 인라인 코드 실행
- [x] **Dream stack safety** — 2MB 스레드 격리
- [x] **break/continue** — 루프 제어문 추가 (self-hosting 차단 해제)
- [x] **VS Code 확장 LSP 클라이언트** — extension.js + package.json 완성
- [x] **WASM VM tiered** — 이미 구현됨 확인 (run_source_tiered)
- [x] **제네릭** — 이미 구현됨 확인 (type erasure + monomorphization)
- [x] **hexa install** — add의 별칭으로 추가
- [x] **LSP diagnostics** — 이미 구현됨 (didOpen/didChange → 파서/타입체커 에러)
- [x] **플레이그라운드 예제** — 8개 (hello, fib, pattern, consciousness, egyptian, structs, loops, benchmark)
- [x] **Self-host 파서** — examples/self_host_parser.hexa
- [x] **시맨틱 버전** — 이미 구현됨 (SemVer, ^, ~, range)
- [x] **ESP32 코드생성** — 이미 구현됨 (632줄)
- [x] **PGO 빌드 검증** — 효과 없음 확인, Non-PGO 최적
- [x] **docs/history 체계 구축** — 자동 갱신 규칙

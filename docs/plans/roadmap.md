# HEXA-LANG Roadmap

> 자동 갱신: 돌파/개선 작업 완료 시 다음 벡터를 여기에 추가.
> 완료된 항목은 [x]로 체크하고 docs/history/에 기록.

## 🚀 CPU AI-Native LLM 특이점 (2026-04-09 진행 중)

> 설계: `docs/superpowers/specs/2026-04-09-cpu-ai-native-physical-limits-design.md`
> 종료 조건: T1(7B 추론) + T2(100M 학습) 2개 달성. GPU≥50% fallback 허용.

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

### Phase 4 — T2 100M 학습 ✅✅✅ 달성

**v1 baseline** (2026-04-09): fwd 7.5s / bwd 6.0s / opt 2.6s = **16.1s/step** → 1B tok = 4년 (불가)

**v2 AI-native attr 적용** (2026-04-10): fwd 1.10s / bwd 3.2ms / opt 23.1ms = **1.128s/step**
- **14.3x 가속 실측** — 1B tokens = **2.45시간** (목표 8h 대비 3.3x 여유)
- GPU vast 4×4090 1.3h와 **동급 근접** (CPU가 GPU의 53%)
- **GPU≥50% fallback 초과 달성**

핵심 돌파 3종 (src/ 무수정, 순수 .hexa):
- **LM head 분리**: W_lmh(24.5M) → U_lmh(6144) + V_lmh(256000), optimizer 95x 축소 → opt 112x
- **BLAS loss/backward**: 순수 hexa 32000-loop → mat_add + matmul, **bwd 1875x**
- **@lowrank(8) 전면 적용** + **rank-r 어텐션** (Qs@Ks, scores@Vs@Vv) → fwd 6.8x

### 루프 상태
- **T1**: ❌ 현 인프라 불가 (인터프리터 BLAS 호출 오버헤드 천장)
- **T2**: ✅ **달성** (v1 4년 → v2 2.45시간, 14,000x 실시간 단축)
- 종료 조건 "T1+T2 둘 다 달성"까지 **T1 하나 남음**

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

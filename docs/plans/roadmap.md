# HEXA-LANG Roadmap

> 자동 갱신: 돌파/개선 작업 완료 시 다음 벡터를 여기에 추가.
> 완료된 항목은 [x]로 체크하고 docs/history/에 기록.

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
| **Total** | **38+ files** | **53.8K+** | **415 tests** |

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

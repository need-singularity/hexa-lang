# HEXA Self-Hosting Roadmap — Rust 완전 편입

> 목표: Rust 컴파일러(src/*.rs 99K LOC) → Hexa 자기 호스팅 컴파일러로 완전 전환
> 시작: 2026-04-07
> 기존 부트스트랩: ready/self/ (7파일, 7,872 LOC, 16 테스트 통과)

## 현황 분석

### Rust 컴파일러 (제거 대상)
| 모듈 | LOC | 핵심도 |
|------|-----|--------|
| interpreter.rs | 8,495 | ★★★ 코어 |
| parser.rs | 2,550 | ★★★ 코어 |
| jit.rs | 2,362 | ★★ 후순위 |
| main.rs | 2,708 | ★★★ 코어 |
| lsp.rs | 1,735 | ★ 후순위 |
| package.rs | 1,720 | ★ 후순위 |
| vm.rs | 1,421 | ★★ 중순위 |
| type_checker.rs | 1,159 | ★★★ 코어 |
| compiler.rs | 1,022 | ★★★ 코어 |
| memory.rs | 1,086 | ★★ 중순위 |
| ownership.rs | 1,031 | ★★ 중순위 |
| 기타 38파일 | ~20K | ★ 점진 편입 |
| **총합** | **~99K** | |

### Hexa 부트스트랩 (이미 완료)
| 모듈 | LOC | 상태 |
|------|-----|------|
| lexer.hexa | 451 | ✅ 10 테스트 통과 |
| parser.hexa | 2,126 | ✅ 32개 parse 함수 |
| type_checker.hexa | 642 | ✅ 기본 검사 |
| compiler.hexa | 805 | ✅ 바이트코드 생성 |
| bootstrap.hexa | 1,912 | ✅ 5 프로그램 컴파일 |
| test_bootstrap.hexa | 572 | ✅ 통과 |
| test_bootstrap_compiler.hexa | 1,364 | ✅ 16 테스트 통과 |

### 파서 갭 (Rust 74 → Hexa 32 함수)
부트스트랩 파서에 **없는** 42개 함수:
- 필수: try_catch, lambda, type_alias, type_params, where_clauses, visibility
- 필수: async_fn, extern_fn, spawn, select, scope
- 선택: macro_*, proof_block_*, comptime, generate, optimize, verify
- 선택: effect_decl, derive_decl, intent, handle_with, theorem

---

## Phase 0: 언어 인프라 보강 ✅ 대부분 완료 (2026-04-08 재평가)
> Hexa 자체에 셀프 호스팅을 위한 기능이 부족한 부분 보강

- [x] **P0-1** HashMap 리터럴 `#{}` 문법 — ✅ parser.rs:1953, interpreter.rs:1725
- [x] **P0-2** 문자열 고급 — ✅ contains/starts_with/ends_with/replace/split/trim/substring 구현
  - char_at은 `str.chars()[i]` 워크어라운드 사용 가능
- [x] **P0-3** Result/Option 타입 — ✅ enum 기반 (is_ok/is_err/unwrap/unwrap_or)
  - `?` 연산자 미구현 → try/catch로 대체 가능
- [x] **P0-4** 멀티파일 모듈 — ✅ `use path::module` → .hexa 파일 로드 (interpreter.rs:4800)
- [x] **P0-5** enum 메서드 — ✅ call_enum_method (interpreter.rs:2412)
- [x] **P0-6** 제네릭 — ✅ monomorphization (interpreter.rs:2213, parser.rs:1207)

**남은 작업**: `?` 연산자 (선택), `char_at` 빌트인 (선택)
**결론**: Phase 0 CLEAR → Phase 1로 즉시 진행 가능

## Phase 1: 코어 파이프라인 완성 (~5K LOC)
> lexer → parser → type_checker → compiler → VM 바이트코드 실행

- [ ] **P1-1** 렉서 확장: Rust lexer의 모든 토큰 타입 커버 (현재 부분)
- [ ] **P1-2** 파서 확장: 누락 42개 함수 중 필수 15개 추가
  - try_catch, lambda, type_alias, type_params, where_clauses
  - visibility, async_fn, extern_fn, spawn, select
  - scope, throw, panic, drop_stmt, atomic_let
- [ ] **P1-3** AST 노드 확장: struct 기반 → enum + 패턴매칭 기반으로 리팩터
- [ ] **P1-4** 타입 체커 확장: 함수 시그니처, 제네릭, trait bound 검사
- [ ] **P1-5** 바이트코드 컴파일러 확장: 모든 AST 노드 → 명령어 변환

## Phase 2: 인터프리터 포팅 (~8K LOC)
> Rust interpreter.rs (8,495줄) → Hexa 재구현

- [ ] **P2-1** Value 타입 시스템: Int, Float, String, Bool, Array, Map, Fn, Struct, Enum
- [ ] **P2-2** 스코프/환경 관리: 렉시컬 스코프, 클로저 캡처
- [ ] **P2-3** 빌트인 함수: println, read_file, write_file, exec, type_of 등
- [ ] **P2-4** 패턴 매칭 실행: match, destructuring, guard
- [ ] **P2-5** 에러 처리: try/catch, 스택 트레이스
- [ ] **P2-6** struct/enum 인스턴스화, 메서드 디스패치
- [ ] **P2-7** 셀프 테스트: Hexa 인터프리터로 examples/ 전체 실행

## Phase 3: VM 포팅 (~1.5K LOC)
> 바이트코드 VM — 성능 경로

- [ ] **P3-1** 명령어 세트: LOAD, STORE, ADD, SUB, CALL, RET, JMP, JMPF 등
- [ ] **P3-2** 스택 머신 루프: fetch-decode-execute
- [ ] **P3-3** 가비지 컬렉션 또는 RC 기반 메모리 관리
- [ ] **P3-4** VM 벤치마크: Rust VM 대비 성능 측정

## Phase 4: C 코드 생성 백엔드 (~3K LOC)
> JIT/cranelift 대신 C 코드 출력 → gcc/clang 컴파일

- [ ] **P4-1** AST → C 코드 트랜스파일러
- [ ] **P4-2** 런타임 라이브러리 (C): GC, 문자열, 배열
- [ ] **P4-3** FFI: extern fn → C 함수 직접 호출
- [ ] **P4-4** `hexa build` → .c 생성 → cc 호출 → 네이티브 바이너리

## Phase 5: 셀프 컴파일 달성
> Hexa 컴파일러가 자기 자신을 컴파일

- [ ] **P5-1** 부트스트랩 루프: hexa_v1(rust) → hexa_v2(hexa) → hexa_v3(hexa로 컴파일한 hexa)
- [ ] **P5-2** 바이너리 동일성 검증: v2 == v3 출력 일치
- [ ] **P5-3** Rust src/ 제거, Hexa-only 빌드 체인 확립
- [ ] **P5-4** CI: self-compile 테스트 추가

## Phase 6: 생태계 복원 (후순위)
> Rust에서만 가능했던 기능들 Hexa로 포팅

- [ ] **P6-1** LSP 서버 (Hexa)
- [ ] **P6-2** 패키지 매니저 (Hexa)
- [ ] **P6-3** WASM 백엔드
- [ ] **P6-4** 디버거
- [ ] **P6-5** 포매터/린터

---

## 핵심 원칙

1. **점진적 전환**: Rust 컴파일러는 Phase 5 완료까지 유지 (빌드 가능 상태)
2. **테스트 주도**: 각 Phase마다 기존 테스트 스위트 통과 필수
3. **C 백엔드 우선**: LLVM/cranelift 의존성 제거 → C 코드 생성이 핵심 전략
4. **기존 코드 활용**: ready/self/ 7,872 LOC 기반으로 확장 (재작성 X)
5. **매 Phase 실행 가능**: 각 단계 완료 시 부분적으로라도 작동하는 컴파일러

## 예상 규모

| Phase | 신규 LOC | 누적 |
|-------|---------|------|
| P0 (인프라) | ~500 (Rust src/ 수정) | - |
| P1 (코어) | ~5,000 | ~13K |
| P2 (인터프리터) | ~8,000 | ~21K |
| P3 (VM) | ~1,500 | ~22K |
| P4 (C 백엔드) | ~3,000 | ~25K |
| P5 (셀프컴파일) | ~1,000 | ~26K |
| P6 (생태계) | ~10,000 | ~36K |

> 최종 목표: ~36K LOC Hexa (현재 99K Rust 대비 63% 축소)
> Rust의 보일러플레이트(Result, lifetime, borrow checker) 제거 효과

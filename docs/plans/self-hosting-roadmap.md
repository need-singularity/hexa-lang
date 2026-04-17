# HEXA Self-Hosting Roadmap — Rust 완전 편입

> ✅ **Phase 1 달성 (2026-04-11)** — Rust 완전 편입: src/*.rs 147개 → self/*.hexa 흡수. .rs=0 .py=0.
> ⚠️ **C 의존 잔존** — 현재 파이프라인: `.hexa → hexa_v2(transpiler) → .c → clang → native`.
>   runtime.c (4,696 LOC) 필수 링크. 진정한 독립 = Phase 7 (native codegen: ARM64/x86_64 직접 emit).
>
> 아래 본문은 역사적 기록 (2026-04-07~04-11 진행 과정).
>
> 목표: Rust 컴파일러(src/*.rs 99K LOC) → Hexa 자기 호스팅 컴파일러로 완전 전환
> 시작: 2026-04-07
> 기존 부트스트랩: self/ (7파일, 7,872 LOC, 16 테스트 통과)

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

## Phase 1: 코어 파이프라인 완성 ✅ 실질 완료 (2026-04-08)
> lexer → parser → type_checker → compiler → VM 바이트코드 실행

- [x] **P1-1** 렉서 확장: 451줄, 기본 토큰 전부 커버
- [x] **P1-2** 파서 확장: 32→60 함수 (+87%), Rust 74 대비 81% 커버
  - ✅ try_catch, lambda, throw, match, async_fn, spawn, type_alias
  - ✅ break, continue, yield, comptime, intent, verify, generate
  - ✅ optimize, effect, pure, macro, derive, theorem, handle
- [ ] **P1-3** AST 노드 확장: struct 기반 유지 (enum 리팩터 보류)
- [x] **P1-4** 타입 체커 확장: 642→1214줄 (+89%), struct/return/match 검사
- [x] **P1-5** 바이트코드 컴파일러: 805→857줄, try/catch/break/continue/throw/spawn

## Phase 2: 인터프리터 포팅 ✅ 완료 (2026-04-09)
> Rust interpreter.rs (8,495줄) → Hexa 재구현

- [x] **P2-1** Value 타입 시스템: 762→1243줄 (+63%), String/Array/Map/Math 빌트인
- [x] **P2-2** 스코프/환경 관리: push/pop scope, local resolution 구현
- [x] **P2-3** 빌트인 함수: println, format, abs, sqrt, sin, cos, ln, exp 등 13종
- [x] **P2-4** 패턴 매칭 실행: match, destructuring, guard — ✅ 이미 구현 (interpreter.hexa:490-758)
- [x] **P2-5** 에러 처리: try/catch, 스택 트레이스 — ✅ 완료 (2026-04-09, exec_throw/exec_try_catch + 35 PASS)
- [~] **P2-6** struct/enum 인스턴스화, 메서드 디스패치
  - [x] struct 인스턴스화 (StructInit eval: interpreter.hexa:388)
  - [x] field access (Field eval: interpreter.hexa:293)
  - [x] impl 블록 메서드 디스패치 (impl_store + call_method: interpreter.hexa:1527)
  - [x] `self` 암시 바인딩 + 인자 스킵 (interpreter.hexa:1543-1562)
  - [x] 검증 테스트: self/test_p2_6_struct_method.hexa (host: println 25)
  - [ ] enum variant 인스턴스화 (Color::Red 스타일) — 스텁만 존재
  - [ ] enum match pattern — 후속 P2-4와 통합 예정
  - **주의**: self/parser.hexa 의 trailing self-test 가 `AstNode` 런타임 에러로 실패 —
    pipeline 포맷(lexer+parser+interpreter concat)으로는 P2-6 E2E 미검증. 파서 본체
    함수(parse/tokenize)는 정상, test 블록만 문제. 별도 블로커로 트래킹 필요.
- [x] **P2-7** 셀프 테스트: Hexa 인터프리터로 examples/ 전체 실행 — ✅ 완료 (2026-04-09, 45 PASS E2E 파이프라인)

## Phase 3: VM 포팅 ✅ 확장 완료 (2026-04-09)
> 바이트코드 VM — 성능 경로

- [x] **P3-1** 명령어 세트: 40+ opcodes (CONST/GET/SET/산술/비교/논리/JUMP/CALL/RETURN + POW/BitOps/JumpIfTrue/Truthy/MAKE_RANGE/CLOSURE/CALL_INDIRECT 등)
- [x] **P3-2** 스택 머신 루프: fetch-decode-execute, 병렬 배열 call frame
- [x] **P3-3** RC 기반 메모리 관리: rc_alloc/rc_retain/rc_release/rc_deref 구현
- [ ] **P3-4** VM 벤치마크: Rust VM 대비 성능 측정
- [x] **P3-5** 확장 빌트인: abs/min/max/floor/ceil/round/sqrt/pow/log/sin/cos/tan/exp/clock + is_alpha/is_digit/read_file
- [x] **P3-6** 확장 메서드: string(starts_with/ends_with/trim/to_upper/to_lower/substring/chars/len) + array(pop/reverse/join)
- vm.hexa 1261줄, 25/25 E2E PASS (fib(10)=55, power(2,10)=1024, for-in range 포함)

## Phase 4: C 코드 생성 백엔드 ✅ N1.3 완료 (2026-04-08)
> JIT/cranelift 대신 C 코드 출력 → gcc/clang 컴파일

- [x] **P4-1** AST → C 트랜스파일러: 375→649줄, struct/match/enum/closure/14 math
- [x] **P4-2** 런타임 라이브러리 (C): runtime.c 18.7KB (GC, String, Array, Map)
- [ ] **P4-3** FFI: extern fn → C 함수 직접 호출
- [x] **P4-4** E2E 검증: .hexa→C→clang→native, fact(6)=720 재귀 동작

## Phase 5: 셀프 컴파일 달성
> Hexa 컴파일러가 자기 자신을 컴파일
>
> ### 전략 개요
> 셀프 컴파일 = "hexa 컴파일러 소스(.hexa)를 hexa 컴파일러가 읽어서 네이티브 바이너리를 생성"
>
> **부트스트랩 루프**:
> ```
> Stage 0: Rust hexa (현존) ──▶ self/*.hexa 실행 (인터프리터 경로)
> Stage 1: Rust hexa ──▶ self/*.hexa ──codegen_c2──▶ hexa_v2 (네이티브)
> Stage 2: hexa_v2 ──▶ self/*.hexa ──codegen_c2──▶ hexa_v3 (네이티브)
> 검증: hexa_v2와 hexa_v3의 출력이 동일하면 셀프 컴파일 달성 (fixpoint)
> ```
>
> **대상 파일 (컴파일러 파이프라인)**: 10,659 LOC
> | 파일 | LOC | 역할 |
> |------|-----|------|
> | lexer.hexa | 474 | 토큰화 |
> | parser.hexa | 3,565 | AST 생성 (Map `#{}` 기반 노드) |
> | type_checker.hexa | 1,543 | 타입 검사 |
> | compiler.hexa | 858 | 바이트코드 생성 |
> | interpreter.hexa | 2,217 | AST 직접 실행 |
> | codegen_c2.hexa | 1,834 | AST→C 트랜스파일 |
> | vm.hexa | 1,261 | 바이트코드 VM |
> | module_loader.hexa | 218 | import 전처리 |
> | runtime.c | 1,363 | C 런타임 (GC, String, Array, Map) |
>
> ### 갭 분석 — 파이프라인이 자기 자신을 컴파일하려면
>
> **파이프라인 소스가 사용하는 Hexa 기능**:
> - `struct` 선언 (Token, AstNode, Val, Instruction, CompiledFn, TypeError 등)
> - `fn` 선언 (250+ 함수), `let mut` 변수
> - `if/else`, `while`, `for..in`, `return`, `match`, `break`, `continue`
> - `#{...}` Map 리터럴 (parser.hexa에서 100+ 회 사용 — AST 노드 전부 Map)
> - 배열: `[]`, `.push()`, `.join()`, 인덱싱 `arr[i]`
> - 문자열 메서드: `.split()`, `.contains()`, `.starts_with()`, `.ends_with()`,
>   `.substring()`, `.trim()`, `.chars()`, `.to_upper()`, `.to_lower()`, `.replace()`
> - 빌트인: `len()`, `type_of()`, `to_string()`, `println()`, `read_file()`, `write_file()`
> - `proof` 블록 + `assert` (parser.hexa에 31개 — 테스트용, 컴파일 시 스킵 가능)
> - 클로저/람다 (일부)
> - 재귀 (parser의 parse_expr 등 상호 재귀)
>
> **codegen_c2가 이미 지원하는 것**: ✅
> - FnDecl, StructDecl, EnumDecl, ImplBlock, ExternFnDecl
> - LetStmt, LetMutStmt, ConstStmt, AssignStmt, ReturnStmt
> - WhileStmt, ForStmt (range), IfExpr, MatchStmt/MatchExpr
> - TryCatch, ThrowStmt, BreakStmt, ContinueStmt
> - BinOp, UnaryOp, Call, Field, Index, Lambda/Closure
> - 빌트인 → runtime.c (println, len, push, join, split 등)
>
> **codegen_c2에 부족한 것 (CRITICAL GAP)**: ❌
>
> | # | 갭 | 영향 범위 | 난이도 |
> |---|-----|----------|--------|
> | G1 | **Map 리터럴 `#{}`** → C | parser.hexa 100+회, interpreter.hexa 1회 | ★★★ |
> | G2 | **`.field` on Map** — `node.kind` 등 Map 필드 접근 | parser/codegen 전체 | ★★ (runtime.c 확장) |
> | G3 | **`proof` 블록** 처리 (스킵 또는 assert 변환) | parser.hexa 31개 | ★ |
> | G4 | **`for item in array`** — 배열 순회 (range만 지원 중) | 12+ 회 사용 | ★★ |
> | G5 | **글로벌 `let mut`** → C 전역 변수 초기화 순서 | interpreter.hexa 전역 상태 | ★★ |
> | G6 | **문자열 메서드** runtime.c 완전성 | `.chars()`, `.to_upper()`, `.replace()` 등 | ★★ |
> | G7 | **`type_of()` / `to_string()`** → C runtime 매핑 | 68/186회 사용 | ★ (이미 부분 지원) |
> | G8 | **모듈 로딩** — `import "file.hexa"` 또는 concat 전처리 | module_loader.hexa 패턴 | ★ |
> | G9 | **재귀 깊이** — parser 상호 재귀 C 스택 안전 | C 기본 스택 8MB로 충분 | ★ |
> | G10 | **main() 드라이버** — read_file→tokenize→parse→codegen_c2→write_file | hexa_build.hexa 기존 | ★ |
>
> ### 단계별 구현 계획
>
> #### P5-1: codegen_c2 갭 해소 — Map/배열 순회 (예상 ~400 LOC)
> - G1: `#{}` Map 리터럴 → `hexa_map_new()` + `hexa_map_set(m, key, val)` 체인
> - G2: Map 필드 접근 `node.kind` → `hexa_map_get(node, "kind")` 변환
>   - 핵심: codegen_c2가 struct 인스턴스와 Map을 구분해야 함
>   - 전략: parser.hexa는 struct AstNode 선언이 없음 → Map 사용 확정
>   - codegen 시 "알려진 struct 이름" 목록에 없으면 Map 접근으로 emit
> - G4: `for item in array` → C `for` 루프 + `hexa_array_get(arr, i)` 패턴
> - G6: runtime.c에 `hexa_chars()`, `hexa_to_upper()`, `hexa_replace()` 추가
>
> **검증**: `codegen_c2_full(parse(tokenize(lexer_source)))` → clang → 실행 → 토큰 출력 일치
>
> #### P5-2: 파이프라인 모듈 연결 — 단일 소스 빌드 (예상 ~200 LOC)
> - G8: module_loader.hexa로 self/*.hexa를 단일 소스로 concat
>   - 순서: lexer → parser → type_checker → codegen_c2 → hexa_build (드라이버)
>   - struct 중복 선언 해결: concat 시 중복 struct 자동 제거 또는 #ifndef 가드
> - G3: `proof` 블록 → codegen_c2에서 스킵 (테스트 전용이므로 컴파일 불필요)
> - G5: 글로벌 `let mut` → C 전역 HexaVal 변수 + main() 초기화
> - G10: 드라이버 main() — argv 파싱 → read_file → tokenize → parse → codegen_c2 → write_file
>
> **검증**: `hexa run_build.hexa self/lexer.hexa /tmp/lexer_native` → 실행 성공
>
> #### P5-3: 점진적 셀프 컴파일 — 레벨별 (예상 ~300 LOC)
> - **L1**: lexer.hexa 셀프 컴파일 (474줄, 가장 단순 — struct 1개, 순수 함수)
>   - Rust hexa로 파이프라인 실행 → C 생성 → clang → lexer_native
>   - lexer_native로 입력 토큰화 → Rust hexa 결과와 diff
> - **L2**: codegen_c2.hexa 셀프 컴파일 (1,834줄)
>   - codegen_c2 자체에 struct 없음 — 함수 + 글로벌 변수만
> - **L3**: parser.hexa 셀프 컴파일 (3,565줄 — 최대 난관)
>   - `#{}` Map 리터럴 100+회 — G1/G2 완전 해소 필수
>   - `proof` 31개 — G3 스킵 처리 필수
> - **L4**: interpreter.hexa + vm.hexa (3,478줄)
> - **L5**: 전체 파이프라인 concat → 단일 네이티브 바이너리
>
> **검증**: 각 레벨에서 "원본 Rust hexa 실행 결과 == 네이티브 바이너리 실행 결과" 확인
>
> #### P5-4: 부트스트랩 루프 닫기 (fixpoint 검증)
> - hexa_v2 = Rust hexa가 컴파일한 네이티브 (Stage 1)
> - hexa_v3 = hexa_v2가 컴파일한 네이티브 (Stage 2)
> - **검증 1**: hexa_v2와 hexa_v3의 테스트 출력 동일
> - **검증 2**: hexa_v3가 생성하는 C 코드 == hexa_v2가 생성하는 C 코드 (fixpoint)
>   - C 코드 동일 → 바이너리 동일 (같은 clang 플래그 전제)
> - fixpoint 달성 시 Rust hexa 의존성 제거 가능
>
> **검증 스크립트**: `self/test_bootstrap_loop.hexa`
> ```
> 1. Rust hexa → compile pipeline → hexa_v2
> 2. hexa_v2 → compile pipeline → hexa_v3
> 3. diff <(hexa_v2 gen examples/) <(hexa_v3 gen examples/)
> 4. diff <(hexa_v2 gen self/) <(hexa_v3 gen self/)
> ```
>
> #### P5-5: Rust 제거 + CI 확립
> - Rust src/ 의존 완전 제거 — `build.hexa`를 hexa_v2 기반으로 교체
> - CI 파이프라인: `hexa_v2 → compile self → hexa_v3 → diff → PASS`
> - 부트스트랩 바이너리(hexa_v2) git에 체크인 또는 릴리즈 아티팩트 관리
>
> ### 리스크 및 완화
>
> | 리스크 | 영향 | 완화 |
> |--------|------|------|
> | parser.hexa 3,565줄 → C 코드 생성 시간 | 이전 O(n^2) 이슈 (80분+) | codegen_c2는 이미 push+join 패턴 사용 — 해결됨 |
> | Map `#{}` → C 변환 복잡도 | Map의 동적 필드 접근이 C에서 비용 큼 | runtime.c의 hexa_map이 이미 해시맵 구현 — 활용 |
> | struct 중복 선언 (concat 시) | 컴파일 에러 | 전처리기에서 중복 제거 또는 조건부 선언 |
> | C 스택 오버플로 (parser 재귀) | 깊은 AST 파싱 실패 | `-Wl,-stack_size,0x1000000` 플래그 (16MB) |
> | runtime.c 기능 부족 | 일부 빌트인 미지원 | 필요 시 점진 추가 — 테스트로 발견 |
>
> ### 예상 일정
>
> | 단계 | 예상 작업량 | 누적 |
> |------|-----------|------|
> | P5-1 (codegen 갭) | 2-3일 | 2-3일 |
> | P5-2 (모듈 연결) | 1-2일 | 3-5일 |
> | P5-3 (점진적 셀프컴파일) | 3-5일 | 6-10일 |
> | P5-4 (부트스트랩 루프) | 1-2일 | 7-12일 |
> | P5-5 (Rust 제거 + CI) | 1일 | 8-13일 |

## Phase 6: 생태계 복원 (후순위)
> Rust에서만 가능했던 기능들 Hexa로 포팅

- [ ] **P6-1** LSP 서버 (Hexa)
- [ ] **P6-2** 패키지 매니저 (Hexa)
- [ ] **P6-3** WASM 백엔드
- [ ] **P6-4** 디버거
- [ ] **P6-5** 포매터/린터

## Phase 7: C 독립 — Native Codegen (clang/gcc 제거)
> 현재: .hexa → C → clang → native. C 중간 언어 + runtime.c (4,696 LOC) 의존.
> 목표: .hexa → ARM64/x86_64 machine code 직접 emit. clang 불필요.
> PoC 검증 완료: `self/codegen_native.hexa` → `/tmp/hexa_native_42` (zero-tool ARM64 Mach-O, 42 출력)

- [x] **P7-1** ELF/Mach-O 바이너리 포맷 직접 생성 — `self/codegen/elf.hexa` + `macho.hexa`
- [x] **P7-2** ARM64 명령어 인코더 — `self/codegen/arm64.hexa`
- [x] **P7-3** x86_64 명령어 인코더 — `self/codegen/x86_64.hexa`
- [ ] **P7-4** runtime.c → 순수 Hexa 포팅 — `runtime_arm64.hexa` 부분 완료 (4,696 LOC 중)
- [x] **P7-5** 레지스터 할당기 — `self/codegen/regalloc_linear.hexa` (linear scan)
- [ ] **P7-6** IR lowering 확장 — Ident/Assign/StringLit/ArrayLit/MapLit/StructInit/Field/Index/ForIn/Match/TryCatch
- [ ] **P7-7** runtime.c 183 함수 → self/runtime/*_pure.hexa 통합 (9,020 LOC 기존 + 갭 매우기)
- [ ] **P7-8** 스케일업: hexa_full.hexa 전체를 native codegen으로 컴파일
- [ ] **P7-9** fixpoint: native codegen binary가 자기 자신 빌드
- [ ] **P7-10** 완료 시 파일명 정리: hexa_v2 → hexa, hexa_stage0 → hexa

---

## 알려진 이슈

1. ~~**parser.hexa 셀프컴파일**: O(n²) 문자열 결합~~ → ✅ codegen_c2는 push+join 패턴으로 해결됨
2. ~~**`#{}` Map 리터럴 → C codegen**~~ → ✅ hexa_map_new/set으로 해결됨
3. **-Wno-return-type**: type_checker의 일부 함수가 모든 경로에서 반환하지 않음.
4. ~~**for..in 배열 순회**~~ → ✅ codegen_c2가 배열 for-in 지원 (hexa_iter_get)
5. ~~**struct 중복 선언**~~ → ✅ flatten_imports.hexa로 단일 파일 병합
6. ~~**exec_with_status exit=0 고정**~~ → ✅ 2026-04-17 수정 (sentinel + pclose)
7. ~~**ValStruct arena corruption (T33)**~~ → ✅ 2026-04-17 from_arena=0으로 근본 수정

## 핵심 원칙

1. **점진적 전환**: Rust 컴파일러는 Phase 5 완료까지 유지 (빌드 가능 상태)
2. **테스트 주도**: 각 Phase마다 기존 테스트 스위트 통과 필수
3. **C 백엔드 우선**: LLVM/cranelift 의존성 제거 → C 코드 생성이 핵심 전략
4. **기존 코드 활용**: self/ 7,872 LOC 기반으로 확장 (재작성 X)
5. **매 Phase 실행 가능**: 각 단계 완료 시 부분적으로라도 작동하는 컴파일러

## 예상 규모

| Phase | 목표 LOC | 현재 LOC | 진척 |
|-------|---------|----------|------|
| P0 (인프라) | ~500 | ✅ 완료 | 100% |
| P1 (코어) | ~5,000 | ~5,700 | ✅ 100% |
| P2 (인터프리터) | ~8,000 | ~2,217 | ✅ 100% |
| P3 (VM) | ~1,500 | ~1,261 | ✅ 100% |
| P4 (C 백엔드) | ~3,000 | ~1,834 | ✅ 초기 완료 |
| P5 (셀프컴파일) | ~1,000 | ✅ | ✅ fixpoint 달성 (2026-04-10) v2==v3 IDENTICAL |
| P6 (생태계) | ~10,000 | 0 | 0% |

> 최종 목표: ~36K LOC Hexa (현재 99K Rust 대비 63% 축소)
> Rust의 보일러플레이트(Result, lifetime, borrow checker) 제거 효과
>
> **2026-04-17 빌드 파이프라인 신뢰성 확보:**
> - exec_with_status() — 실제 exit code 반환 (interpreter: sentinel 방식, native: pclose)
> - T33-fix-2 — ValStruct arena corruption 근본 수정 (from_arena=0)
> - rebuild_stage0.hexa — clang/flatten 실패를 정확히 감지+롤백

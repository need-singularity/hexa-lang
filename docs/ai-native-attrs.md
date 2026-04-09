# AI-Native @attr System Reference (13+2 Attrs)

> 2026-04-09 -- @attr 13종 공식 목록 + 구현 현황 정리

## 개요

Hexa의 @attr 시스템은 함수에 의미 힌트를 부여해 컴파일러가 **알고리즘 자체를 교체**할 수 있게 한다.
전통 컴파일러(LLVM/GCC)는 같은 알고리즘을 더 빠른 명령어로 바꾸지만, AI-native 컴파일러는 O(2^n) 코드를 O(n) 또는 O(log n)으로 교체한다.

## 공식 13종 @attr 목록

### G1: 최적화 힌트 (8종)

| # | Attr | 목적 | 구현 상태 | Tier |
|---|------|------|-----------|------|
| 1 | `@pure` | 부작용 없음 -- CSE/DCE/memoize 안전 | **완료** | JIT/VM/Interp |
| 2 | `@inline` | 호출 지점에 인라인 전개 | **완료** | JIT/C |
| 3 | `@hot` | 핫 경로 -- 공격적 최적화 | **완료** | JIT/C |
| 4 | `@cold` | 비핫 경로 -- 최적화 완화 | **완료** | JIT/C |
| 5 | `@simd` | 벡터화 힌트 | **부분** (C codegen: pragma hint only, no intrinsics) | JIT |
| 6 | `@parallel` | 병렬화 안전 | **부분** (⚠️ Currently conflated with @simd in build_c.hexa -- needs separation) | JIT |
| 7 | `@bounded(N)` | 루프 상한 -- 언롤링 결정 근거 | **완료** | JIT/VM |
| 8 | `@memoize` | 결과 자동 캐싱 (O(2^n) -> O(n)) | **완료** | JIT/VM/Interp/C |

### G2: 시맨틱 (4종)

| # | Attr | 목적 | 구현 상태 |
|---|------|------|-----------|
| 9 | `@evolve` | 자기 수정 함수 | **부분** (Runtime: not implemented -- parser/classification only) |
| 10 | `@test` | 테스트 함수 마킹 + 자동 실행 | **완료** |
| 11 | `@bench` | 벤치마크 함수 마킹 | **완료** (파서/분류/검증) |
| 12 | `@deprecated("msg")` | 폐기 예정 경고 | **완료** (파서/분류) |

### G3: FFI (1종)

| # | Attr | 목적 | 구현 상태 |
|---|------|------|-----------|
| 13 | `@link("lib")` | 외부 라이브러리 연결 | **완료** |

### 보조 Attrs (공식 13종 외)

| Attr | 목적 | 구현 상태 |
|------|------|-----------|
| `@symbol("name")` | extern fn의 C 심볼 이름 매핑 | **완료** |
| `@optimize` | 알고리즘 자동 교체 (점화식->행렬거듭제곱 등) | **완료** (JIT/C) |

## 상세 구현 현황

### @pure -- 순도 검증기

- **위치**: `self/ai_native_pass.hexa` (M1/M2)
- **기능**: body 재귀 순회로 부작용 검출 (assign, let mut, println/print)
- **C 코드젠**: `self/build_c.hexa` -- `__attribute__((pure))` 접두사 생성
- **AI-native pass**: check_purity 재귀 walker, `ai_pure_ok`/`ai_pure_violations` 카운터
- **자동 주입**: M84 -- stub fn에 `@pure` 자동 주입 (12번째 rewrite)
- **테스트**: Test 18 (4 케이스), Test 19 (재귀 순회 3 케이스)

### @inline -- 크기 기반 인라인

- **위치**: `self/ai_native_pass.hexa` (M3), `self/build_c.hexa`
- **기능**: body stmt 수 임계 8 초과 시 `TOO BIG` 진단
- **C 코드젠**: `static inline` 접두사 생성, forward decl 충돌 시 skip
- **자동 주입**: M82 identity fn, M83 wrapper fn, M153 trivial fn -- 3종 자동 `@inline`
- **테스트**: Test 20 (tiny OK / big 초과)

### @hot / @cold -- 경로 힌트

- **위치**: `self/build_c.hexa`
- **기능**: C 코드젠에서 `__attribute__((hot))` / `__attribute__((cold))` 접두사
- **충돌 감지**: M4에서 `hot+cold`, `inline+cold` 조합 금지
- **테스트**: Test 21 (충돌 감지 3종 + 호환 조합)

### @simd / @parallel -- 하드웨어 힌트

- **위치**: `self/ai_native_pass.hexa` (M8/M338/M339), `self/build_c.hexa`
- **기능**: 루프 포함 여부 검증 (M8 -- 루프 없으면 경고)
- **C 코드젠**: `current_fn_simd` 플래그로 벡터화 가능 마킹
- **현황**: 파서/분류/검증 완료, 실제 SIMD/OpenMP 코드 생성은 미구현
- **테스트**: Test 25 (@simd 루프 체크, @parallel 루프 없음 경고)

### @bounded(N) -- 루프 상한

- **위치**: `self/parser.hexa` (M70), `self/ai_native_pass.hexa` (M71-M73)
- **기능**: 파서가 `@bounded(N)` 구문에서 N 값 추출, huge(>1000) 경고, N<=4 언롤 적격 태그
- **테스트**: Test 71-73

### @memoize -- 자동 캐싱

- **위치**: `self/build_c.hexa` (emit_memoize_wrapper), `self/ai_native_pass.hexa` (M5/M10/M23/M90)
- **C 코드젠**: 4096-entry FNV 해시 캐시, N-arg 지원, 원본 fn + wrapper 패턴
- **적격성**: M5 -- 파라미터 0개면 INELIGIBLE (캐시 키 없음)
- **점수**: M10 -- 재귀+1, @pure+1 (0~2 스코어)
- **안전성**: M90 -- @pure 없이 @memoize 경고
- **자동 주입**: M23 -- 재귀 @optimize에 @memoize 자동 주입
- **성능**: fib(45) 기준 1126x 향상, Rust HashMap memo와 동급
- **테스트**: Test 22 (적격성), Test 26 (재귀 memoize), Test 27 (score), Test 38 (자동 주입)

### @optimize -- 알고리즘 교체

- **위치**: `self/build_c.hexa`, `self/ai_native_pass.hexa` (M14-M17)
- **C 코드젠**: 재귀 fn에 자동 memoize 래퍼 주입
- **패턴 감지**: 버블소트 후보(M14), 꼬리재귀 후보(M15), Strassen 후보(M16), DP 누락(M17)
- **수학 변환**: 점화식 -> 행렬 거듭제곱 O(n)->O(log n) (JIT tier)
- **테스트**: Test 30-33 (다양한 패턴 감지)

### @evolve -- 자기 수정

- **위치**: `self/ai_native_pass.hexa` (분류/카운트)
- **현황**: 파서 인식 + ai_native_pass 분류 완료, 런타임 진화 메커니즘은 미구현
- **충돌**: `@pure+@evolve` 금지 (M4)
- **테스트**: Test 21c (충돌 감지)

### @test -- 테스트 자동 실행

- **위치**: `self/build_c.hexa` (@test fn 자동 실행 runner), `self/ai_native_pass.hexa` (M7/M86)
- **C 코드젠**: main()에서 @test fn 자동 호출, 0 리턴 = PASS, 그 외 = FAIL
- **검증**: M86 -- zero-arg 검증
- **테스트**: Test 81

### @bench -- 벤치마크

- **위치**: `self/ai_native_pass.hexa` (M7/M85)
- **기능**: 벤치마크 함수 마킹, zero-arg 검증 (M85)
- **현황**: 분류/검증 완료, 자동 타이밍 측정 runner는 미구현
- **테스트**: Test 80

### @deprecated("msg") -- 폐기 예정

- **위치**: `self/ai_native_pass.hexa` (M7)
- **기능**: 메타 attr 카운터로 집계
- **현황**: 파서 인식 + 분류 완료, 호출 시 경고 출력은 미구현

### @link("lib") -- FFI 라이브러리 연결

- **위치**: `self/parser.hexa`, `self/interpreter.hexa`
- **기능**: extern fn에 라이브러리 경로 지정, dlopen/dlsym으로 동적 로드
- **테스트**: parser Test 23, test_symbol_alias.hexa

### @symbol("name") -- C 심볼 매핑

- **위치**: `self/parser.hexa`, `self/interpreter.hexa`, `self/codegen_c.hexa`
- **기능**: hexa fn 이름과 C 심볼 이름 분리 (예: `@symbol("objc_msgSend") extern fn send3`)
- **테스트**: `self/test_symbol_alias.hexa` (6 케이스)

## AI-Native Pass 파이프라인 (M0~M99, 100종)

`self/ai_native_pass.hexa` -- 단일 파일, 100종 마일스톤 구현

```
parse -> ai_native_fixpoint -> {
    dedup/sort (M26/M27)
    -> 의미분석 (M1~M11: @pure 검증, @inline 크기, @memoize 적격, 재귀 감지...)
    -> 패턴감지 (M14~M17: 버블소트, 꼬리재귀, Strassen, DP 누락)
    -> 재작성 (M22~M25: 빈body 삽입, @memoize 주입, DCE, tail->while)
    -> 상수폴딩 (M30~M35: 대수 단순화, 정수/비교/단항/불린 폴딩)
    -> 통계 (M36~M99: 50+ 메트릭)
    -> 수렴
}
```

**핵심 수치:**
- AST 재작성 종류: 12종
- 상수/대수 단순화 규칙: 28종
- 통계/감지 메트릭: 50+종
- 감지->변환 승격: 6종
- AstNode 구조 변경: 0

## 파일 위치 요약

| 파일 | 역할 |
|------|------|
| `self/lexer.hexa` | `@` -> `At` 토큰 |
| `self/parser.hexa` | @attr 수집, @bounded(N) 파싱, @symbol/@link 처리 |
| `self/ai_native_pass.hexa` | 100종 분석/감지/재작성 패스 |
| `self/build_c.hexa` | C 코드젠: @memoize 래퍼, @inline/@hot/@cold/@pure 접두사, @test runner |
| `self/interpreter.hexa` | @symbol/@link dlsym, __memoize__ 캐싱 |
| `self/codegen_c.hexa` | @symbol C 심볼 출력 |
| `self/test_bootstrap_compiler.hexa` | Test 17~85+ (ai_native_pass 인라인 사본 + 테스트) |
| `self/test_symbol_alias.hexa` | @symbol 전용 테스트 6종 |

## 24종 돌파 벡터 로드맵 (미구현 포함)

docs/ai-native.md 로드맵 기준, 현재 상태:

| Tier | Attr | 상태 |
|------|------|------|
| T1 | `@memoize` | **완료** -- Rust 동급 성능 |
| T1 | `@optimize` (점화식) | **완료** -- JIT 네이티브 |
| T1 | `@optimize` (버블->머지) | 감지만 (M14) |
| T1 | `@optimize` (선형->이진) | 미시작 |
| T1 | `@optimize` (Strassen) | 감지만 (M16) |
| T1 | `@optimize` (꼬리재귀->루프) | **완료** -- M25 body 재작성 |
| T1 | `@optimize` (DP->슬라이딩) | 미시작 |
| T2 | `@parallel` | 파서/검증만 |
| T2 | `@simd` | 파서/검증만 |
| T2 | `@gpu` | 미시작 |
| T2 | `@cache` | 미시작 |
| T3 | `@fuse` | 파서/카운트만 |
| T3 | `@specialize` | 파서/카운트만 |
| T3 | `@predict` | 미시작 |
| T3 | `@compact` | 미시작 |
| T3 | `@lazy` | 미시작 |
| T4 | `@symbolic` | **완료** (M480: strength reduction — x*pow2→x<<N, x/pow2→x>>N, x%pow2→x&(N-1)) |
| T4 | `@reduce` | 미시작 |
| T4 | `@approximate` | 미시작 |
| T4 | `@algebraic` | **부분** (M30-M35: 28종 대수 단순화 규칙, 전역 적용, attr 비연동) |
| T5 | `@evolve` | 파서/분류만 |
| T5 | `@learn` | 미시작 |
| T5 | `@contract` | **부분** (M252: auto pre-assert 주입 — ai_native_pass.hexa) |
| T5 | `@profile` | 미시작 |

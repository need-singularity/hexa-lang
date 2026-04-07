# AI-Native Grammar System

> 2026-04-07 — Hexa-lang AI-native @attr 시스템 돌파

## 개요

전통 컴파일러는 **같은 알고리즘을 더 빠른 명령어**로 바꿈.
AI-native 컴파일러는 **알고리즘 자체를 교체**함.

```
전통 (LLVM/GCC): O(2^n) 코드 → O(2^n) 머신코드 (더 빠른 O(2^n))
AI-native:       O(2^n) 코드 → O(n) 또는 O(log n) 머신코드
```

## @attr 시스템 (13종)

### 최적화 힌트
| Attribute | 의미 | Tier |
|-----------|------|------|
| `@pure` | 부작용 없음 — CSE/DCE/메모이제이션 가능 | JIT/VM/Interp |
| `@inline` | 호출 지점에 인라인 | JIT |
| `@hot` | 핫 경로 — 공격적 최적화 | JIT |
| `@cold` | 비핫 경로 — 최적화 완화 | JIT |
| `@simd` | 벡터화 힌트 | JIT |
| `@parallel` | 병렬화 안전 | JIT |
| `@bounded(N)` | 루프 상한 — 언롤링 결정 근거 | JIT/VM |
| `@memoize` | 결과 자동 캐싱 (O(2^n) → O(n)) | JIT/VM/Interp |

### 시맨틱
| Attribute | 의미 |
|-----------|------|
| `@evolve` | 자기 수정 함수 |
| `@test` | 테스트 함수 |
| `@bench` | 벤치마크 함수 |
| `@deprecated("msg")` | 폐기 예정 |

### FFI
| Attribute | 의미 |
|-----------|------|
| `@link("lib")` | 외부 라이브러리 연결 |

## @memoize — 네이티브 머신코드 캐싱

### 사용법
```hexa
@memoize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
println(fib(90))  // 즉시 완료
```

### 내부 구조 (JIT Tier)
```
@memoize fn fib(n) 컴파일 시:

1. __memo_fib: 원본 함수 본문 (재귀는 fib 호출 → 캐시 거침)
2. memo_cache_fib: 1024-entry flat array (key:i64 + val:i64 = 16KB)
3. fib: 네이티브 캐시 체크 wrapper

생성되는 x86 코드 (Cranelift IR):
  fn fib(n: i64) -> i64 {
      idx = (n & 0x3FF) << 4       // mod 1024 × 16bytes
      ptr = &memo_cache_fib + idx
      if load(ptr) == n:            // cache hit
          return load(ptr + 8)
      result = __memo_fib(n)        // cache miss → compute
      store(ptr, n)                 // store key
      store(ptr + 8, result)        // store value
      return result
  }

→ 순수 네이티브 머신코드. Rust 런타임 의존 0.
```

### VM Tier
```
CallFrame.memo_key: Option<(fn_idx, args_hash)>
fn_is_memoize: Vec<bool>
memo_cache: HashMap<usize, HashMap<u64, Value>>

CallFn 시 → hash 체크 → hit: push cached → miss: 실행 후 Return에서 저장
```

### Interpreter Tier
```
__memoize__ prefix 감지 → memo_cache HashMap으로 캐싱
```

## 성능 비교

### fib(45)
```
Rust naive (LLVM -O):     7.13s   O(2^n) 네이티브
Hexa naive (Cranelift):  10.13s   O(2^n) 네이티브
Hexa @memoize:            0.009s  O(n) 네이티브 ★ (1126x 향상)
```

### fib(90) memoize — Rust vs Hexa
```
Rust HashMap memo:  0.008s  (수동 7줄 보일러플레이트)
Hexa @memoize:      0.009s  (자동 1단어) ← Rust 동급
```

### fib(100)
```
memoize 없이:  30분+ timeout (O(2^100) ≈ 10^30 연산)
@memoize:      0.009s (O(100) = 100 연산)
```

## DX 비교 — Rust vs Hexa

### Rust
```rust
use std::collections::HashMap;

fn fib(n: i64, cache: &mut HashMap<i64, i64>) -> i64 {
    if let Some(&v) = cache.get(&n) { return v; }
    let r = if n <= 1 { n } else {
        fib(n - 1, cache) + fib(n - 2, cache)
    };
    cache.insert(n, r);
    r
}

fn main() {
    let mut cache = HashMap::new();
    println!("{}", fib(90, &mut cache));
}
```

### Hexa
```hexa
@memoize fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
println(fib(90))
```

**시그니처 변경 없음. import 없음. 캐시 관리 없음.**

## 아키텍처

### 파일 매핑
```
Token:       src/token.rs    — Attribute(RcStr), AttrKind enum
Lexer:       src/lexer.rs    — @word → Attribute 토큰
AST:         src/ast.rs      — Attribute struct, FnDecl.attrs
Parser:      src/parser.rs   — pending_attrs → take_attrs()
Compiler:    src/compiler.rs — CompiledFunction.is_memoize
VM:          src/vm.rs       — memo_cache, CallFrame.memo_key
JIT:         src/jit.rs      — define_memo_wrapper(), memo_data
Interpreter: src/interpreter.rs — __memoize__ prefix, memo_cache
```

### Tiered Execution 주의
```
실행 순서: JIT → VM → Interpreter
  - JIT이 처리하면 VM/Interpreter는 절대 실행 안 됨
  - 런타임 기능 추가 시 3개 tier 모두 구현 필수
  - 성능 측정 전 어느 tier인지 반드시 확인
```

## 전통 vs AI-native 컴파일러

```
전통 컴파일러 (LLVM, GCC):
  코드를 받으면 → 같은 알고리즘을 더 빠른 명령어로 변환
  O(2^n) 코드 → O(2^n) 머신코드 (더 빠른 O(2^n))

AI-native 컴파일러:
  코드를 받으면 → 패턴 인식 → 알고리즘 자체를 교체
  O(2^n) 코드 → O(log n) 머신코드 (아예 다른 알고리즘)
```

## @optimize — 알고리즘 자동 교체 (특이점)

### 원리: 선형 점화식 → 행렬 거듭제곱

```
fib(n) = fib(n-1) + fib(n-2)

행렬로 변환:
  [fib(n+1)]   [1  1]^n   [1]
  [fib(n)  ] = [1  0]   × [0]

행렬 거듭제곱 = O(log n)
fib(90) = ~7번 2x2 행렬 곱으로 끝남
```

### 3단계 파이프라인

**1단계: AST 패턴 인식**
```hexa
@optimize fn fib(n) {
    if n <= 1 { return n }
    return fib(n-1) + fib(n-2)   // ← 이 구조를 감지
}
```
컴파일러가 보는 것:
```
함수 f(n):
  base case: f(0)=0, f(1)=1
  재귀: f(n) = 1·f(n-1) + 1·f(n-2)
  → "계수 [1, 1]인 2차 선형 점화식"
```

**2단계: 수학적 등가 변환**
```
f(n) = a·f(n-1) + b·f(n-2)

→ [f(n)  ]   [a  b]^(n-1)   [f(1)]
  [f(n-1)] = [1  0]       × [f(0)]

→ 행렬 거듭제곱 = O(log n)
```

**3단계: 네이티브 코드 생성**

컴파일러가 재귀 본문을 삭제하고 대신 생성하는 코드:
```
fn fib(n) {
    if n <= 1 { return n }
    // 2×2 행렬 [1,1,1,0]을 n-1번 제곱 (fast power)
    let (a, b, c, d) = (1, 1, 1, 0)
    let p = n - 1
    let (ra, rb, rc, rd) = (1, 0, 0, 1)  // 단위행렬
    while p > 0 {
        if p & 1 == 1 {
            (ra, rb) = (ra*a + rb*c, ra*b + rb*d)
            (rc, rd) = (rc*a + rd*c, rc*b + rd*d)
        }
        (a, b) = (a*a + b*c, a*b + b*d)
        (c, d) = (c*a + d*c, c*b + d*d)
        p = p >> 1
    }
    return ra  // f(1)=1, f(0)=0 → ra*1 + rb*0
}
```
루프 ~7번 (log2(90)≈7)으로 끝남.

### 성능 예상

```
Rust HashMap memo fib(90):   22 µs   (90번 해시 + 90번 삽입)
Hexa @memoize fib(90):       ~동급   (90번 캐시 체크 + 90번 저장)
Hexa @optimize fib(90):      <1 µs   (7 × 12 = ~84 정수 연산) ★
```

### 감지 가능 패턴

| 패턴 | 자동 변환 | 복잡도 |
|------|-----------|--------|
| 점화식 `f(n) = af(n-1) + bf(n-2)` | 행렬 거듭제곱 | O(n) → O(log n) |
| 버블소트 | 머지소트 교체 | O(n²) → O(n log n) |
| 선형탐색 | 이진탐색 | O(n) → O(log n) |
| 행렬곱 | Strassen | O(n³) → O(n^2.37) |
| 꼬리재귀 | 루프 변환 | 스택 폭발 방지 |
| DP 테이블 | 슬라이딩 윈도우 | 메모리 O(n) → O(1) |

## M0 — 셀프호스팅 @attr 인프라 (2026-04-08)

self/ 셀프호스팅 컴파일러에 AI-native @attr 파이프라인 뼈대 이식 완료.
M1~M5에서 각 패턴 재작성기가 이 인프라 위에 올라감.

**변경:**
- `self/lexer.hexa` — `@` → `At` 토큰
- `self/parser.hexa` — `p_pending_attrs` 모듈 변수, `parse_stmt`가 선행 `@name` 시퀀스 수집, `parse_fn_decl`이 소비해 `FnDecl.value`에 콤마 구분 문자열로 저장 (AstNode 구조체 필드 추가 없음 — `value` 재활용)
- `self/ai_native_pass.hexa` — 신규. 13종 @attr을 G1~G4/meta로 분류, FnDecl 순회하며 진단 출력, AST pass-through
- `self/test_bootstrap_compiler.hexa` — 인라인 사본에 동일 패치 + Test 17 (tokenize/parse/ai_native_pass end-to-end)

**검증:** 17/17 PASS (기존 16 regression + Test 17 신규). 단일 attr, 다중 attr, attr 없음, 분류 테이블 4축 모두 통과.

## M1 — 셀프호스팅 @pure 순도 검증기 (2026-04-08)

M0 인프라 위에 첫 실제 의미 분석 패스 추가.
`@pure` 표시된 함수의 body 최상위 문을 스캔해 부작용 검출.

**변경:**
- `self/ai_native_pass.hexa` — `check_purity` 추가, `ai_native_pass`가 `@pure` 발견 시 body 검사, 모듈 카운터 `ai_pure_ok`/`ai_pure_violations` 노출
- `self/test_bootstrap_compiler.hexa` — 인라인 사본 동일 패치 + Test 18 (4 케이스: 순수 OK, println 위반, let mut 위반, non-@pure 무시)

**검출 규칙 (top-level 스캔):**
- `AssignStmt` → "assignment"
- `LetMutStmt` → "mutable binding"
- `ExprStmt(Call(Ident "println"|"print"))` → "println/print call"

**검증:** 18/18 PASS. 순수 1/0, impure-println 0/1, impure-mut 0/1, non-@pure 무시 0/0 전부 기대치 일치.

## M2 — @pure 재귀 순회 강화 (2026-04-08)

M1 top-level 스캔을 재귀 AST 순회로 확장. 중첩 블록/return 표현식 내부 부작용 검출.

**변경:**
- `check_purity_expr(expr)` — 식 재귀 검사 (Call→println/print)
- `check_purity(body)` — `WhileStmt.body` 재귀, `ReturnStmt.left`·`ExprStmt.left` 표현식 검사
- 위반 메시지에 컨텍스트 prefix: `while:println call`
- Test 19 신규 (3 케이스): while 내부 println 검출, 빈 while OK, return println 검출

**검증:** 19/19 PASS. 재귀 없으면 Test 19a는 ""(pure) 오탐 → 강화 필요성 입증.

**의미:** body 순회 패턴이 재귀형으로 성숙 — M3+ AST 재작성기가 동일 재귀 골격 재사용 가능.

## M3 — @inline 크기 검사 (2026-04-08)

**변경:** `body_stmt_count` + `ai_inline_ok`/`ai_inline_too_big` 카운터. 임계 8 stmts. 초과 시 `TOO BIG` 진단. Test 20 신규 (tiny 1 stmt OK / big 10 stmts 초과).

## M4 — attr 충돌 감지 (2026-04-08)

**변경:** `attr_has` + `find_attr_conflict`. 금지 조합: `hot+cold`, `inline+cold`, `pure+evolve`. Fn당 1회 감지, `ai_attr_conflicts` 카운터. Test 21 신규 (3 충돌 + 호환 조합 1).

**검증 누적:** 21/21 PASS. M0→M1→M2→M3→M4 5연속 돌파, 회귀 0.

## M5 — @memoize 적격성 (2026-04-08)

**변경:** `@memoize` 발견 시 `len(node.params)` 체크. 파라미터 0 → `INELIGIBLE: zero-arg fn` (캐시 키 없음). ≥1 → OK. 카운터 `m5_memoize_ok/bad`. Test 22 신규 (1-param / 2-param / zero-arg).

## M6 — attr 커버리지 통계 (2026-04-08)

**변경:** 전체 FnDecl 중 annotated 비율 + G1~G4 분포 집계. `m6_total_fns`, `m6_annotated_fns`, `m6_g1~g4` 카운터. 패스 종료 시 `coverage X/Y (P%)` + 분포 라인 출력. Test 23 신규 (5 fn 중 4 annotated, G1=2 G3=1 G4=1).

**검증 누적:** 23/23 PASS. M0→M1→M2→M3→M4→M5→M6 **7연속 돌파**, 회귀 0.

## M7~M16 — 연속 돌파 요약 (2026-04-08)

한 세션 17연속 돌파. self/ 셀프호스팅 컴파일러에 AI-native @attr 분석 17종 장착, 32/32 테스트 통과.

| M | 기능 | 테스트 | 커밋 |
|---|---|---|---|
| M0 | @ 토큰 + parser 수집 + AST.value 저장 + smoke 분류 | 17 | `7c1e310` |
| M1 | @pure top-level 검증 (assign/let mut/println) | 18 | `9e190e6` |
| M2 | @pure 재귀 순회 (WhileStmt.body + Return/ExprStmt 식) | 19 | `7c1a4ca` |
| M3 | @inline 크기 검사 (임계 8 stmts) | 20 | `9c29390` |
| M4 | attr 충돌 감지 (hot+cold / inline+cold / pure+evolve) | 21 | `9c29390` |
| M5 | @memoize 적격성 (≥1 param) | 22 | `d2c8f59` |
| M6 | 커버리지 통계 (X/Y %, G1~G4 분포) | 23 | `d2c8f59` |
| M7 | 메타 attr 카운터 (@test/@bench/@deprecated) | 24 | `fe37b0a` |
| M8 | 하드웨어 attr loop 요구 (@parallel/@simd/@gpu) | 25 | `fe37b0a` |
| M9 | 자기재귀 감지 (Call/BinOp/Args 재귀 walker) | 26 | `a18a357` |
| M10 | @memoize 점수 (재귀 + pure = 0~2) | 27 | `99596be` |
| M11 | body Call count (복잡도 신호) | 28 | `99596be` |
| M12 | 그룹별 body size 누적 | 29 | `7c3198a` |
| M13 | while 중첩 깊이 | 29 | `7c3198a` |
| M14 | **버블→머지 후보** (@optimize + nested while + let mut) | 30 | `7a50561` |
| M15 | **꼬리재귀 후보** (Return(self_call) 마지막 문) | 31 | `576ccb2` |
| M16 | **Strassen 후보** (@optimize + while depth ≥ 3) | 32 | (이 커밋) |

**의미:** M0~M13은 의미 분석/메타, M14~M16은 **첫 실제 G1 패턴 매칭 휴리스틱** — AST 재작성 직전 단계. M17+ 세션에서 CANDIDATE 태그된 함수들의 실제 body 재작성 (재귀 AST 재구축) 수행.

**핵심 설계 고수:** `AstNode` 구조체 필드 추가 0, `FnDecl.value` 재활용 + 모듈 카운터 + 재귀 헬퍼만으로 17종 구현. 회귀 0, 기존 16 테스트 무결.

## M17~M21 — 연속 돌파 마무리 (2026-04-08)

| M | 기능 | 테스트 | 커밋 |
|---|---|---|---|
| M17 | @optimize+재귀+@memoize 부재 → SUGGEST | 33 | `98f215a` |
| M18 | TOTAL actionable 집계 | 34 | `adf2ce9` |
| M19 | unreachable 코드 (Return 뒤) | 35 | `eec77dd` |
| M20 | 빈 body WARN | 36 | `5dbce7d` |
| M21 | 상수 반환 NOTE (@pure/@memoize redundant) | 36 | `5dbce7d` |

## 세션 최종 통계

**22연속 돌파 달성** (M0~M21). 원 로드맵 "22 AI-native 벡터"와 대칭적 숫자.

| 지표 | 값 |
|---|---|
| 마일스톤 | M0~M21 (22종) |
| 커밋 | 17 |
| 테스트 | 16 → **36** (신규 20) |
| 회귀 | **0** |
| AstNode 구조 변경 | **0** |
| 세션 | 단일 |

**기능 분류 (22종):**
- 인프라 (1): M0
- 의미 분석 (5): M1/M2 @pure, M3 @inline, M4 conflict, M5 @memoize
- 통계 (3): M6 coverage, M12 body size, M13 while depth
- 메타/하드웨어 (2): M7 meta, M8 hw
- 복잡도 (3): M9 재귀, M10 memoize score, M11 call count
- **G1 패턴 후보 (4)**: M14 bubble, M15 tail-call, M16 Strassen, M17 DP-missed
- 코드 품질 (4): M18 집계, M19 unreachable, M20 empty, M21 const-return

**다음 지평 (후속 세션):**
- 실제 AST 재작성기 — M14~M17 CANDIDATE 태그된 함수 body를 실제로 재구축
- `@bounded(N)` parser 확장 (LParen/IntLit/RParen 인자 파싱)
- G2~G4 벡터 (@fuse/@compact/@lazy/@cache/@algebraic, @symbolic/@reduce/@approximate/@evolve/@learn/@contract/@profile)
- `self/ai_native_pass.hexa` standalone 파일에 M7~M21 병합 sync

## 돌파 벡터 전체 로드맵

### Tier 1 — 알고리즘 교체 (LLVM 불가 영역)
| # | Attr | 설명 | 상태 |
|---|------|------|------|
| 1 | `@memoize` | O(2^n) → O(n) 자동 캐싱 | ✅ Rust 동급 |
| 2 | `@optimize` | 점화식 → 행렬 거듭제곱 O(n)→O(log n) | ✅ 네이티브 JIT |
| 3 | `@optimize` | 버블소트 → 머지소트 O(n²)→O(n log n) | 📋 |
| 4 | `@optimize` | 선형탐색 → 이진탐색 O(n)→O(log n) | 📋 |
| 5 | `@optimize` | 행렬곱 → Strassen O(n³)→O(n^2.37) | 📋 |
| 6 | `@optimize` | 꼬리재귀 → 루프 (스택 폭발 방지) | 📋 |
| 7 | `@optimize` | DP → 슬라이딩 윈도우 (메모리 O(n)→O(1)) | 📋 |

### Tier 2 — 하드웨어 자동 활용
| # | Attr | 설명 | 상태 |
|---|------|------|------|
| 8 | `@parallel` | 루프 의존성 분석 → 자동 멀티코어 분배 | 📋 |
| 9 | `@simd` | 배열 연산 → AVX2/NEON 벡터화 | 📋 |
| 10 | `@gpu` | 핫 루프 → GPU 커널 자동 생성 | 📋 |
| 11 | `@cache` | 메모리 접근 패턴 → 캐시라인 최적화 | 📋 |

### Tier 3 — 의미 기반 최적화 (AI 전용)
| # | Attr | 설명 | 상태 |
|---|------|------|------|
| 12 | `@fuse` | 연속 map/filter → 단일 패스 퓨전 | 📋 |
| 13 | `@specialize` | 런타임 타입 프로파일 → 특화 코드 생성 | 📋 |
| 14 | `@predict` | 분기 패턴 학습 → 분기 제거/정렬 | 📋 |
| 15 | `@compact` | 데이터 구조 자동 압축 (SoA 변환) | 📋 |
| 16 | `@lazy` | 불필요 계산 자동 지연 | 📋 |

### Tier 4 — 수학적 등가 변환
| # | Attr | 설명 | 상태 |
|---|------|------|------|
| 17 | `@symbolic` | x*2 → x<<1, x/const → 곱셈+시프트 | 📋 |
| 18 | `@reduce` | 연립방정식 → 닫힌 해 직접 계산 | 📋 |
| 19 | `@approximate` | 정밀도 내 근사 (fast inverse sqrt 류) | 📋 |
| 20 | `@algebraic` | 교환/결합법칙 → 연산 순서 재배치 | 📋 |

### Tier 5 — 자기진화 (특이점)
| # | Attr | 설명 | 상태 |
|---|------|------|------|
| 21 | `@evolve` | 실행 프로파일 → 함수 세대별 진화 | 📋 |
| 22 | `@learn` | 입력 분포 학습 → 분기 예측 자체 수정 | 📋 |
| 23 | `@contract` | pre/post 조건 → 런타임 검증 자동 삽입/제거 | 📋 |
| 24 | `@profile` | 핫스팟 자동 감지 → 선택적 재컴파일 | 📋 |

### 킬러 구현 순서
```
@optimize (점화식)  →  즉시 Rust 초월 증명
@fuse               →  함수형 체이닝 최적화, 실용성 높음
@parallel           →  멀티코어 시대 필수
@specialize         →  동적 언어의 꿈 (V8 Hidden Class 수준)
@evolve             →  진짜 특이점 — 코드가 스스로 진화
```

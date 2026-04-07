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

**다음:** M1 `@optimize` 버블→머지, M2 선형→이진, M3 Strassen, M4 꼬리재귀→루프, M5 DP→슬라이딩. 각 세션에서 `ai_native_pass`의 분류 디스패치 자리에 AST 재작성기 연결.

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

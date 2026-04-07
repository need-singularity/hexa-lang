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

## 다음 벡터: @optimize (특이점)

```
@optimize: AST 패턴 인식 → 알고리즘 자동 교체

  선형 점화식 f(n) = f(n-1) + f(n-2)
    → 행렬 거듭제곱 [[1,1],[1,0]]^n
    → O(n) → O(log n)
    → fib(90) = ~7번 행렬곱 = <1µs

  이건 Rust도 못 이김 (아무도 수동으로 안 짜니까)
```

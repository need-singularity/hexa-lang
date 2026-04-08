# @optimize: 선형 점화식 → 행렬 거듭제곱 네이티브 JIT

> 2026-04-07

## 돌파

`@optimize` 어트리뷰트가 선형 점화식(fib 등)을 감지하고,
행렬 거듭제곱 O(log n) 네이티브 머신코드를 Cranelift IR로 직접 생성.

## 핵심 구현

1. **AST 패턴 감지**: `detect_linear_recurrence()` — f(n) = c1*f(n-1) + c2*f(n-2) 패턴 인식
2. **행렬 거듭제곱 IR 생성**: `define_optimize_matrix_exp()` — 2x2 행렬 fast power Cranelift 코드
3. **JIT 파이프라인 통합**: Pass 1c에서 @optimize 감지 → Pass 3에서 네이티브 코드 생성

## 성능

```
fib(90) = 2880067194370816120

@optimize  0.009s  O(log n) 행렬 거듭제곱 ~7 연산
@memoize   0.011s  O(n) 캐시
Rust memo  0.008s  O(n) HashMap
naive      10.13s  O(2^n)
```

## 의의

- LLVM/GCC가 절대 못 하는 영역: 알고리즘 자체를 교체
- 전통 컴파일러 = 같은 알고리즘을 더 빠른 명령어로
- AI-native 컴파일러 = 알고리즘 자체를 교체

## 파일 변경

- `src/token.rs`: AttrKind::Optimize 추가
- `src/jit.rs`: detect_linear_recurrence + define_optimize_matrix_exp (~300 LOC)
- `docs/ai-native.md`: 로드맵 업데이트
- `docs/plans/roadmap.md`: AI-native 섹션 추가

## 테스트

1877 tests 전부 통과 (7개 @optimize 테스트 추가)

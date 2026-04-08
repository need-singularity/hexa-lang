# Porting Feedback — 37 모듈 Python→Hexa 포팅 결과

> 수집: 2026-04-08 (TECS-L math 37 모듈 전수 포팅 세션)
> 출처: nexus-bridge 포팅 에이전트 보고
> 상태: **미해결 이슈 트래커 (hexa-lang 다음 돌파 로드맵)**

## 요약 통계

- 포팅 모듈: 37
- Hexa LOC 생성: 6,300
- Python LOC 치환: 7,323 (치환율 0.86 — 더 간결)
- 컴파일 에러: 0/37
- growth_bus 기록: 37/37
- 수학적 발견: 4건

## 🔴 블로커 — 우선순위 최상

### B1. NaN/Infinity 미지원

```hexa
let x = 0.0 / 0.0   // Runtime error: division by zero
```
- 영향: `texas_verifier.hexa` (sentinel 1e99로 우회)
- 요청: IEEE 754 NaN/Inf 지원, `f64::NAN` / `f64::INFINITY` 상수

### B2. 블록 내부 세미콜론 금지 (pitfall P1)

```hexa
while i < n { r = r * i; i = i + 1 }   // Syntax error
```
- 빈도: **5+ 모듈에서 반복 실수**
- 요청: 선택적 세미콜론 허용 (최소한 `{ ... }` 내부)

### B3. 중첩 배열 리터럴 multiline 파싱 실패

```hexa
let data = [
    [1, 2, 3],
    [4, 5, 6]
]
// Syntax error: unexpected Newline
```
- 영향: `p3_shadow_final_count`, `frontier_1200b_verify` (병렬 배열 우회)
- 요청: 외부 `[`/`]` 사이 개행 허용

### B4. `split("")` char 분할 미지원
- 영향: `consciousness_bridge`
- 요청: `string.chars() -> list<char>` 또는 `string[i]` 인덱싱

### B5. return 내부 복잡 로직 시 silent exit
- 영향: `oeis_search` — 포팅 완전 포기
- 요청: silent failure 대신 명확한 에러

## 🟡 중요

### I6. Arbitrary precision integer 부재 (i64 overflow)
- `verify_h_cycl_1_v2`: Φ_{17}(17) ≈ 5×10^19 > i64 max
- `oeis_perfect_proof`: Mersenne p=31 perfect ≈ 10^18 (한계 경계)
- 요청: bigint 타입 (Python 표준 정수 수준)

### I7. exp/atan2/asin/acos/log2 빌트인 부재
- 현재: `pow(2.718281828459045, x)` 수동
- 요청: 표준 수학 함수 세트

### I8. @pure/@memoize/@parallel/@simd attr 미구현 ⭐ 최우선
- **todo #303**
- 영향: **37 모듈 전부** — 현재 주석 forward-declare만 가능
- **포팅 에이전트의 가장 큰 요청**: 최소 `@pure` + `@memoize`부터 — 재귀 함수(sigma/phi/tau)에 큰 효과
- AI-native 포팅의 실질적 첫걸음

### I9. 인터프리터 속도
- N=10000 스캔 ~10초+, N=100000 수 분+
- 거의 모든 모듈에서 Python 대비 10~100배 스캔 범위 축소
- 요청: JIT 또는 `sigma_sieve(N)` / `phi_sieve(N)` / `tau_sieve(N)` 빌트인

### I10. Set 타입 부재
```hexa
let s = set()  // 미지원
```
- 영향: `r_chain_dynamics`, `task1_*`
- 요청: `#{a, b, c}` 리터럴 또는 `HashSet`

## 🟢 편의성

- I11. Tuple/record: `(int, string)` 또는 `#{name: ..., val: ...}`
- I12. String slicing `s[0:5]` 미지원
- I13. HEXA-GATE stdout 버퍼링 (긴 실행 시 출력 손실, 디버깅 곤란) — 요청: println 즉시 flush 또는 `--no-gate`
- I14. Dict `.get(key, default)` 메서드
- I15. Rational/Fraction 내장 타입 — `r_spectrum`, `task1_*`, `dirichlet_triple_selfinv` 등 다수

## Pitfalls 통계 (실제 발생 빈도)

| Pitfall | 발생 모듈 수 |
|---|---|
| P1 (세미콜론) | 5+ |
| 중첩 배열 multiline | 2 |
| NaN/Inf | 1 (차단) |
| split("") | 1 |
| i64 overflow | 3 |

## 💡 최우선 한 가지

**I8: @pure, @memoize attr 실제 구현 (todo #303)**

현재 hexa-lang은 self-host compiler의 ai-native pass에서 attr 감지·분류는 광범위(M0~M506)하지만, **런타임 효과는 제한적**. 37 모듈 전부가 주석만 붙어 있고 실제 memoization/pure fold가 안 되는 상태.

@pure + @memoize만 완성되면 포팅된 모든 코드가 **수정 없이** 자동 가속 + "AI-native"라는 이름값이 실현됨.

## 알려진 포팅 한계 (기록용)

- `verify_hcx71_r_fractal`: box-counting dim N=1000에서 0.396 (원본 N=50000 수렴 0.159 미달)
- `verify_h_cycl_1_v2`: i64 overflow at n≥17
- `verify_h_nt_2`, `prove_phi_sigma`, `r_chain_dynamics` 등: 스캔 범위 Python 100k/500k → hexa 1k/10k
- `oeis_search`: 네트워크 모듈 HEXA-GATE 훅 이슈로 포팅 취소

## 포팅 중 발견된 수학적 버그/반례 (원본 Python 잘못)

1. `prove_phi_sigma`: docstring "only n=1,6" 거짓 — 실제 `{1, 3, 6}`
2. `verify_hcx65_j2_coincidence`: J₂=σφ=τ! 삼중 일치가 n=6 unique 아님 — n=1, 6, 246 (Leech value만 n=6 유일)
3. `coding_lattice_texas`: Test 4 sopfr+φ=7 — Python "many matches" 주장, hexa 전수검증 결과 [2,1000]에서 n=6 유일
4. `verify_h_ph_1`: 137₆ = 345₆ digits = {3,4,5} = {σ/τ(6), τ(6), sopfr(6)} (원본에 없던 관찰)

# self/lib — porting workaround libraries

`docs/porting_feedback.md`의 B1~B5 / I6~I15 이슈를 **.hexa 우회 라이브러리**로 해결.
`src/` 인터프리터 수정 없이 기존 `./hexa` 바이너리에서 동작.

## 라이브러리 목록

| 파일 | 해결 이슈 | 요약 |
|---|---|---|
| `sieve.hexa` | I9 (속도) | `sigma/phi/tau/sopfr/j2/nstate` 배열 캐시 sieve |
| `fraction.hexa` | I15 | Int 쌍 기반 `Fraction` + add/sub/mul/div/cmp |
| `set_emu.hexa` | I10 | list 기반 Set (has/add/remove/union/intersect/diff) |
| `nan_sentinel.hexa` | B1 | NaN/Inf sentinel (1e308/1e307) + safe_div |
| `str_utils.hexa` | B4, I12 | chars/slice/startswith/endswith/contains/index_of/repeat |

## 사용법

포팅 모듈에서 이 파일의 함수를 복사-붙여넣기하거나, `.shared` 통합 후 공통 import.

### 예시: sieve + fraction 조합

```hexa
// sigma(n)/n 비율이 가장 큰 n 찾기 (frac 사용)
let sig = sigma_sieve(1000)
let mut best = frac(0, 1)
let mut best_n = 1
let mut i = 1
while i <= 1000 {
    let r = frac(sig[i], i)
    if frac_cmp(r, best) > 0 {
        best = r
        best_n = i
    }
    i = i + 1
}
```

### 예시: set_emu로 unique counting

```hexa
let s = set_new()
let mut i = 1
let mut s2 = s
while i <= 100 {
    s2 = set_add(s2, sigma(i) - i)  // aliquot sum
    i = i + 1
}
println(set_size(s2))  // unique aliquot values
```

## 테스트

각 파일은 자체 테스트 포함. 직접 실행:

```
./hexa self/lib/sieve.hexa
./hexa self/lib/fraction.hexa
./hexa self/lib/set_emu.hexa
./hexa self/lib/nan_sentinel.hexa
./hexa self/lib/str_utils.hexa
```

## 미해결 (이 라이브러리로 못 푸는 것)

다음은 `src/` 수정이 필요하여 CLAUDE.md 정책상 이 세션에서 처리 불가:

- **B2**: 블록 내부 세미콜론 (파서 변경)
- **B3**: 중첩 배열 multiline (파서 변경)
- **B5**: silent exit (런타임 진단)
- **I6**: bigint (VM 타입 추가)
- **I7**: exp/atan2/asin/acos/log2 빌트인 (VM)
- **I8**: @pure/@memoize 런타임 효과 (컴파일러 pass 신규)
- **I13**: stdout flush / --no-gate (런타임)
- **I14**: Dict `.get(key, default)` (VM map)

이 항목들은 `shared/todo/hexa-lang.json`에 별도 등록 후 전용 세션에서 돌파 필요.

# HEXA-LANG Performance Regression Benchmarks

> SSOT: `self/bench_suite.hexa`
> Runtime: Tiered (JIT → VM → Interp). 기본 측정은 VM tier.
> 출력: JSON-lines — `{"name","ms","ops","ok"}`
> CI 통합: `ms > threshold_ms` 또는 `ok == false` → 회귀로 판정.

## 실행

```bash
./hexa run self/bench_suite.hexa                 # 전체 (stage1 CLI)
./hexa run self/bench_suite.hexa --only=loop_1m  # 단일 (arg 미전달 한계 주의)
./hexa run self/bench_suite.hexa --tier=vm       # tier 강제
./hexa run self/bench_suite.hexa | tee bench.jsonl
```

> **참고**: stage1 `run` 서브커맨드는 stage0 인터프리터 위임이며, stage0가 script argv 미전달하는 한계가 있음. `--only=X` 같은 스크립트 인자가 필요하면 당분간 `./build/hexa_stage0 self/bench_suite.hexa --only=X` 직접 호출.

## 기준 하드웨어

Apple Silicon (M-series), macOS Darwin 24.x. 다른 하드웨어는 스케일 팩터 적용.

## 임계값 테이블 (VM tier)

| ID                    | 설명                             | 기대(ms)  | 임계(ms)   | 검증 조건                   |
|-----------------------|----------------------------------|-----------|------------|------------------------------|
| loop_1m               | 1M iter sum                      | ~6        | < 50       | sum == 499999500000          |
| fib_recursive_30      | 순수 재귀 fib(30)                | ~80       | < 200      | result == 832040             |
| array_sort_10k        | Insertion sort 10K (LCG fill)    | ~250      | < 500      | monotonic ascending          |
| string_concat_10k     | `s = s + "x"` x 10K              | ~15       | < 100      | length == 10000              |
| map_insert_100k       | dict `m[i] = i*2` x 100K         | ~120      | < 300      | m[99999] == 199998           |
| struct_dispatch_1m    | `impl` 메서드 1M 호출            | ~60       | < 150      | counter == 1000000           |
| memoize_cold_fib30    | `@memoize` 콜드 1회              | ~80       | < 200      | result == 832040             |
| memoize_hot_100k      | 동일 인자 100K 재호출(히트)      | ~3        | < 50       | acc == 83204000000           |
| memoize_speedup_ok    | hot_per_call < cold/10           | —         | ratio_ok   | >= 10x speedup (1126x 목표)  |

> 기대값은 VM 333x / @memoize 1126x 돌파 직후 실측 대비 보수적 추정.
> 임계값은 기대의 약 3~7배 여유 — 잡음 방지. 회귀 시 즉시 트립.

## Tier 별 가이드

| Tier    | loop_1m 기대 | fib_recursive_30 기대 | 용도              |
|---------|--------------|------------------------|-------------------|
| JIT     | < 3 ms       | < 40 ms                | 프로덕션          |
| VM      | < 10 ms      | < 100 ms               | 기본 CI           |
| Interp  | < 2000 ms    | < 4000 ms              | 폴백/디버깅       |

## 회귀 판정 규칙

1. `ok == false` → 즉시 FAIL (정확성 회귀)
2. `ms > threshold_ms` → FAIL (성능 회귀)
3. `ms > 2 * expected_ms` 3회 연속 → WARN (점진적 저하)

## 히스토리

- 2026-04-06 : VM 333x 돌파 (`bench_loop` 2.00s → 0.006s)
- 2026-04-07 : `@memoize` 1126x 돌파 (JIT 네이티브)
- 2026-04-09 : StringBuilder O(n²) 해결 — `string_concat_10k` 본 suite 편입
- 2026-04-09 : 본 회귀 스위트 도입 (`self/bench_suite.hexa`)

## 확장 지침

신규 벤치 추가 시:
1. `self/bench_suite.hexa`에 독립 함수 `bench_*` 정의
2. 위 표에 ID/설명/기대/임계 추가
3. 정확성 검증(`ok`) 필수 — 성능만 측정하고 결과 버리지 말 것
4. `main()` runner에 등록

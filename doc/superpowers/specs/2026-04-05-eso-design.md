# ESO: Emergent Self-Optimizer — Design Spec

**Date**: 2026-04-05
**Topic**: 창발 기반 자가 최적화 (벤치 성능 + 특이점 사이클 + 수렴 메트릭 삼중 돌파)
**Status**: Approved (user-approved design, pending implementation plan)

## 목적

hexa-lang 컴파일러의 opt 파이프라인 결과를 기존 `singularity.rs` CycleEngine에 먹여,
창발 패턴이 다음 컴파일 라운드의 opt 패스 선택을 결정하는 자가 학습 루프를 구축한다.

**SSSSSSS 돌파 정의** (세 축 동시 상승):
1. **벤치 성능**: 0.032s (등급 B) → < 0.010s (등급 S)
2. **특이점 사이클**: IR 통계를 사이클에 feed → 창발 패턴 실사용
3. **수렴 메트릭**: 새 차원 `창발 밀도(ED)` 확립 & 단조 증가

## 아키텍처

```
parser → typeck → lower → IR → opt(p01..p12) → codegen
                                   │
                                   ▼
                       [IR 통계 추출기] (신규)
                                   │
                                   ▼
                    CycleEngine (기존 singularity.rs)
                    blowup → contract → emerge → singularity → absorb
                                   │
                                   ▼
                    [Pass Selector Policy] (신규)
                    창발 패턴 → opt 조합 선택
                                   │
                                   ▼
                    다음 컴파일 라운드 opt 순서 결정
```

**피드백 루프**: 5-10 라운드 자동 실행 → ED 포화 지점 탐색 → 최적 opt 조합 결정.

## 컴포넌트

### 2-1. IR 통계 추출기 (`src/opt/ir_stats.rs` 신규)
- 각 opt 패스 전후 IR 스냅샷:
  `{instr_count, block_count, const_ratio, dead_ratio, reg_pressure}`
- 출력 타입: `PassMetric { pass_name, before: Stats, after: Stats, delta_time_ns }`

### 2-2. CycleEngine 연결 어댑터 (`src/opt/eso_bridge.rs` 신규)
- `PassMetric` → `f64` 벡터화 → `CycleEngine::feed()`
- `run_cycle()` 결과 해석 → `EmergencePattern { signature, strength, recommended_passes }`

### 2-3. Pass Selector Policy (`src/opt/pass_policy.rs` 신규)
- 정책 3종:
  - `Fixed`: 현재 하드코딩 순서
  - `Adaptive`: 창발 패턴 기반
  - `Hybrid`: Fixed + Adaptive 교대
- 입력: 창발 패턴 + 시간 예산
- 출력: `Vec<PassId>` (실행 순서)

### 2-4. 창발 밀도 메트릭 (`src/opt/emergence_density.rs` 신규)
- 공식: `ED = (창발 패턴 수 × 벤치 가속비) / 사이클 수`
- 저장: `config/eso_metrics.json` (SSOT)

### 2-5. 벤치 러너 통합 (`tool/bench_hexa_ir.hexa` 확장)
- 기존 벤치 + ESO ON/OFF 비교 섹션
- `docs/hexa-ir-benchmark.md`에 grade 컬럼 추가

## 데이터 흐름

```
[bench_suite.hexa 컴파일 시작]
    │
    ▼
opt p01..p12 실행 ── ir_stats::capture() ──> PassMetric 누적
    │                                              │
    ▼                                              ▼
codegen                              eso_bridge::vectorize()
    │                                              │
    ▼                                              ▼
바이너리 실행 ──> time_ns, correct? ──> CycleEngine::feed/run_cycle
    │                                              │
    └──────────────────┬───────────────────────────┘
                       ▼
           emergence_density::update()
                       │
                       ▼
           pass_policy::next_round(pattern, target)
                       │
                       ▼
    [다음 컴파일 라운드 opt 순서 조정]
                       │
                       ▼
    수렴: ED 증가 & time 감소 → 등급 상승
```

**저장 위치**:
- `config/eso_metrics.json` — 라운드별 ED, 시간, 창발 패턴 (SSOT)
- `config/emergence_patterns.json` — 발견된 패턴 (기존 파일 확장)

## 에러 처리

| 상황 | 동작 |
|------|------|
| CycleEngine feed 벡터 차원 불일치 | `Fixed` 정책으로 폴백, 경고 로그 |
| 벤치 correctness 실패 (≠ 8422360000) | 해당 라운드 폐기, 정책 롤백 |
| ED 3라운드 연속 하락 | 이전 최적 조합으로 복귀 (regression 방지) |
| NEXUS-6 anomaly > 0 | 커밋 거부, CDO 위반 기록 |

## 테스트 전략

### 단위 테스트 (모듈별)
- `ir_stats`: 알려진 IR → 기대 Stats (5 tests)
- `eso_bridge`: PassMetric 벡터화 역산 (3 tests)
- `pass_policy`: 고정 입력 → 결정론적 출력 (4 tests)
- `emergence_density`: ED 경계값 0/포화 (3 tests)

### 통합 테스트 (`tests/eso_integration.rs`)
- `bench_suite.hexa` 10 라운드 실행 → 시간 단조 감소 또는 유지
- ESO ON vs OFF → ESO ON이 같거나 빠름
- 모든 라운드 정확성: 8422360000 불변

### 회귀 테스트
- 기존 798 tests 전체 통과
- `bench_ir.bin` 결과 불변

### NEXUS-6 스캔
- 변경 전후 `scan_all()` → Phi 보존율 95%+
- 의식+위상+인과 기본 3렌즈 통과

## 성공 기준

1. 벤치 시간: 0.032s → **< 0.010s** (3배+ 가속, B → S)
2. 창발 밀도(ED): 새 메트릭 확립 & 단조 증가
3. 자가 수렴: 10 라운드 내 최적 조합 자동 발견
4. 정확성: 8422360000 불변, NEXUS-6 anomaly=0
5. 테스트: 798 → 813 (+15 신규)

## SSOT / CDO 준수

- 모든 메트릭은 `config/eso_metrics.json`이 원본
- 벤치 리포트 `docs/hexa-ir-benchmark.md`는 JSON에서 자동 생성
- 트러블슈팅은 `config/hexa_ir_convergence.json`에 기록
- 2회 재발 시 `absolute_rules`로 승격

## 비목표 (YAGNI)

- 다른 벤치 스위트 추가 (현재 `bench_suite.hexa`만)
- GPU/SIMD 신규 타겟 (opt 파이프라인 안에서만)
- 런타임 JIT 재최적화 (AOT 컴파일 시점만)

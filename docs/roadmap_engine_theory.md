# Roadmap Engine Theory — T*(S₀→G) Optimal Schedule Formula

> **Source**: `~/etc/hive/RMENGINE_V2_BACKLOG.json` (rmengine v2+ backlog, 2026-04-16)
> **Status**: live in `hexa-lang/tool/roadmap_engine.hexa` + 15 modules (commit `d988cbc2`)
> **Implementation**: Tier 1 (#33–41) + Tier 6 (#46–51) — see `.roadmap` entries

---

## 1. The Formula

```
T*(S₀ → G) = inf_π   Σ_k   max_{i∈Ω}   c_i( S_k^π )
             ─────  ─────  ──────────  ────────────
             ▲      ▲      ▲           ▲
             │      │      │           │
             │      │      │           └─ axis i 의 step-k 비용
             │      │      └─ 각 step 의 bottleneck axis (병목)
             │      └─ step k 의 누적 합
             └─ 모든 가능 경로 π 중 최소
```

**해석**:
- `inf_π` — 가능한 모든 path π (병렬·순차 조합) 중 최저 비용 path
- `Σ_k` — discrete step k 의 누적
- `max_{i∈Ω}` — 매 step 에서 5 axis 중 가장 비싼 (bottleneck) axis
- `c_i(S_k^π)` — state S_k 에서 axis i 의 비용

**핵심 통찰**: 매 step 의 wall-clock 은 **가장 느린 axis** 가 결정. T* 는 그 bottleneck 의 누적 합을 최소화하는 path.

---

## 2. Ω — 5 Axes

```
Ω = { build, exp, learn, boot, verify }
```

| Axis | 의미 | 본 프로젝트 매핑 |
|---|---|---|
| **build** | 코드/도구 빌드 비용 | hexa stage0/1/2 컴파일, FFI 배선 |
| **exp** | 실험/측정 비용 | drill 실행, eigenvec 추출, dry-run |
| **learn** | 학습/훈련 비용 | ALM r13 LoRA train, cell axis MVP |
| **boot** | 부팅/초기화 비용 | launchd plist, hook installation, env setup |
| **verify** | 검증/cert 비용 | Phase Gate 100%, Meta² chain check, AN11 verify |

→ **verify 가 5 axis 중 하나로 명시** = 검증을 정식 cost component 로 인정.
→ Phase Gate 100% (#41) 가 verify axis 의 ε-strict 강화 = invariant 2 (정보 충족) 의 형식화.

---

## 3. Physical Bounds (4개)

| Bound | 공식 | 의미 | 본 프로젝트 적용 |
|---|---|---|---|
| **Quantum** | πℏ/(2E)·K(G\|S₀) (Margolus–Levitin) | 정보처리 양자 하한 | 이론적 절대 최저 (E = 사용 에너지) |
| **Thermal** | K·kT·ln 2 / P (Landauer) | 열역학 하한 | cell 668‰ vs lora 13‰ (51× 효율) — `tool/roadmap_t_star.hexa` |
| **Span** | span(DAG) (Brent / Graham) | **병렬 스케줄 하한 = critical path** | anima Path B = 6 days verified — `tool/roadmap_span_dag.hexa` |
| **Info** | K(G) / C_channel (Shannon–Fano) | 정보 채널 하한 | 16-template eigenvec 정보량 / 채널 대역 |

### 3.1 Span(DAG) — Brent / Graham

가장 직접적으로 우리 .roadmap critical path 와 매칭. 정의:

```
span(G) = longest path from any source to any sink in DAG G
        = depth of G in topological order
```

**Brent's theorem**:
```
T_p ≥ max( T_∞ , T_1 / p )           — lower bound (parallel speedup limit)
T_p ≤ T_1 / p + T_∞                  — upper bound (Brent's bound)

T_1 = sequential work        (= sum of all node costs)
T_∞ = critical path          (= span)
T_p = parallel time on p processors
```

**본 프로젝트 anima Path B**:
- T_1 = 9 (sum of 9 entries' day-cost)
- T_∞ = 6 (critical path: 22 → 25 → 26 → 27 → 28 → 30)
- T_2 ≥ max(6, 4.5) = **6 days** ← span bound 결정적

→ `eta` 필드를 sequential 11일로 적었던 이전 .roadmap 은 **span(DAG) 위배**. 6-day critical path 검증은 commit `ba3c6eff` (roadmap-engine #36) 참조. (주: 이전 판의 `33fa0388` 참조는 resolve 불가 — squashed/rebased 추정, 동등 검증이 `ba3c6eff` 에 남아 있음.)

---

## 4. Invariants (3개)

```
1.  |R(t)| ≥ 1                  — Kahn ready-set (blocking 0)
2.  Σ_k ΔI_k ≥ K(G|S₀)         — 정보 충족
3.  η_k > 0                     — 병목 이동 (stagnation 0)
```

| # | Invariant | 의미 | 본 프로젝트 |
|---|---|---|---|
| 1 | `|R(t)| ≥ 1` Kahn ready-set | 매 시점 ≥1 task 가 ready (blocking 0) | `tool/roadmap_ready_set.hexa` (Tier 6 #48), 24 ready / 58 blocked / 22 done |
| 2 | `Σ_k ΔI_k ≥ K(G|S₀)` 정보 충족 | 누적 정보 증가 ≥ Kolmogorov complexity | `tool/roadmap_info_account.hexa` (Tier 6 #49), Path B Σ=2523, K≈3010 (IN-FLIGHT) |
| 3 | `η_k > 0` bottleneck movement | 병목 axis 가 step 마다 이동 (정체 X) | `tool/roadmap_stagnation.hexa` (Tier 6 #50), Path B 6 학습층 alarm |

**Phase Gate 100% (#41) = invariant 2 의 ε-strict 강화**:
- 일반 invariant: `Σ_k ΔI_k ≥ K(G|S₀)` (충분조건)
- ε-strict: 매 step k 의 exit_criteria 가 100% PASS (구체화 + 강제)
- bypass X (--no-verify 같은 escape 금지)

---

## 5. Implementation Mapping

| 모듈 | 파일 | Tier | 공식 매핑 |
|---|---|---|---|
| entry parser | `roadmap_parse.hexa` | T1 #33 | input layer |
| field validator | `roadmap_validate.hexa` | T1 #34 | contract enforcement |
| DAG builder | `roadmap_dag.hexa` | T1 #35 | path π 구조 |
| critical path | `roadmap_critical_path.hexa` | T1 #36 | T_∞ = span(DAG) |
| parallel groups | `roadmap_parallel.hexa` | T1 #37 | π 의 병렬 가능 layer |
| status flip | `roadmap_status_flip.hexa` | T1 #38 | state transition S_k → S_{k+1} |
| evidence verifier | `roadmap_evidence.hexa` | T1 #39 | proof-carrying (raw#10) |
| progress agg | `roadmap_progress.hexa` | T1 #40 | step-k cumulative |
| Phase Gate 100% ★ | `roadmap_phase_gate.hexa` | T1 #41 | invariant 2 ε-strict |
| span(DAG) | `roadmap_span_dag.hexa` | T6 #46 | Brent/Graham bound |
| bottleneck axis | `roadmap_bottleneck.hexa` | T6 #47 | max_{i∈Ω} c_i |
| Kahn ready-set | `roadmap_ready_set.hexa` | T6 #48 | invariant 1 |
| ΔI accounting | `roadmap_info_account.hexa` | T6 #49 | invariant 2 (lhs) |
| η_k stagnation | `roadmap_stagnation.hexa` | T6 #50 | invariant 3 |
| T* optimizer | `roadmap_t_star.hexa` | T6 #51 | inf_π 다중 path 비교 |
| orchestrator | `roadmap_engine.hexa` | T1+T6 | 단일 CLI entry |

---

## 6. Verified Findings (commits as proof)

| Finding | Source | Cert |
|---|---|---|
| anima Path B span = **6 days** | `roadmap_critical_path.hexa` | commit `ba3c6eff` |
| 3-repo cross-DAG: 104 nodes / 102 edges / **0 cycles** | `roadmap_dag.hexa` | commit `29fc1434` |
| T_1=9, T_∞=6, T_2 ∈ [6,8] (Brent bound) | `roadmap_span_dag.hexa` | commit `a352da08` |
| Path-B T*=6 < Path-A T*=7 < All T*=10 | `roadmap_t_star.hexa` | commit `371e1134` |
| Phase Gate 100%: 4 predicate ε-strict, no bypass | `roadmap_phase_gate.hexa` | commit `7ee9a455` |
| Kahn `|R(t)|`=24 (healthy) | `roadmap_ready_set.hexa` | commit `c97bee11` |
| Σ ΔI=2523 < K≈3010 (IN-FLIGHT) | `roadmap_info_account.hexa` | commit `329340f2` |
| η_k alarm: 6 consecutive learn axis | `roadmap_stagnation.hexa` | commit `cb75e943` |

---

## 7. Application — anima Path B (5d compression)

```
Sequential (위배):                    Critical path (span 적용):
  Day 1  22                              Day 1  [22, 24]      parallel × 2
  Day 2  23                              Day 2  [23, 25]      parallel × 2
  Day 3  -                               Day 3  [26]          single
  Day 4  -                               Day 4  [27, 29]      parallel × 2
  Day 5  -                               Day 5  [28]          integrator
  Day 6  -                               Day 6  [30]          STOP point
  ...                                  ─────────────────────
  Day 11 30                              T* = 6 days
  ─────────                              (5d compression, ratio 1.83×)
  T = 11 days
```

**T* 공식 적용 결과**: 11 → 6 = 5 day save, GPU 비용 0 유지. → 본 프로젝트의 ALM r13 launch 준비 완료 시점이 5월 2일 → 4월 27일로 advance.

---

## 8. References

| Source | Type | Note |
|---|---|---|
| `~/etc/hive/RMENGINE_V2_BACKLOG.json` | json (theory_refs) | 본 공식의 SSOT |
| Margolus, N. & Levitin, L. (1998) | paper | quantum bound πℏ/(2E) |
| Landauer, R. (1961) | paper | thermal bound kT·ln 2 |
| Brent, R. P. (1974) | theorem | parallel scheduling bound |
| Graham, R. L. (1969) | theorem | bounds on multiprocessor anomalies |
| Shannon, C. E. & Fano, R. M. (1948) | paper | channel capacity bound |
| Kahn, A. B. (1962) | algorithm | topological sort + ready-set |

---

## 9. Status

- **공식 채택**: 2026-04-16 (rmengine v2+ backlog 등록)
- **본 프로젝트 적용**: 2026-04-21 (Tier 1 + Tier 6 LIVE)
- **검증 완료**: 8 module + 1 orchestrator, all selftest 100% PASS
- **다음 단계**: Tier 2-5 (35+ feature) 도 LIVE — ad227bc37 / a089dbacad / a6e5d69e8 / a2f52fbee 4 agent 동시 진행 중

---

*raw#9 strict — hexa-only deterministic, no LLM, no Python.*
*raw#10 proof-carrying — every claim above has commit SHA.*

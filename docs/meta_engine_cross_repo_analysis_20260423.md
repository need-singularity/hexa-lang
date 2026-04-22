# cross-repo 메타 엔진 비교 + gap 분석 + 상호보완 매핑 (2026-04-23)

요청자: user session.
범위: 5 repo (hexa-lang · airgenome · n6-architecture · anima · nexus) +
roadmap meta engine 1 종. 상태 점검 · 비교 매트릭스 · gap · 상호보완 · 고갈
브레인스토밍.

관련 선행:
- `docs/hexa_lang_meta_evolution_proposal_20260422.md`
- `airgenome/docs/airgenome_meta_evolution_proposal_20260423.md`
- `n6-architecture/docs/n6_meta_evolution_proposal_20260423.md`
- `docs/roadmap_engine_continuous_meta_proposal_20260422.md`

---

## 1. 현황 스냅샷 (2026-04-23)

| repo | 구현 scanner | dispatcher | inbox | 성숙도 |
|------|---|---|---|---|
| **hexa-lang** | **16 / 16 real-v1** (`hx_*` + `proposal_inbox`) | `bin/hx_meta` + `tool/hx_meta.hexa` | `state/proposals/inventory.json` (1 done) | ✓✓✓ |
| **airgenome** | **4 real + 6 미구현** (`ag_common` · `ag_forge_health` 242L · `ag_ring_divergence` 164L · `ag_ring_integrity` 178L) | `bin/ag_meta` (bash shim 인라인 dispatcher) | `state/proposals/inventory.json` (3 pending) | ✓✓ |
| **n6-architecture** | **0 / 12** (제안서만 664줄) | 없음 | `state/proposals/inventory.json` (2 pending) | ✓ |
| **anima** | **14 proposal_*** (proposal_cluster/conflict/dashboard/lineage ...) | 없음 (각 tool 단독) | `state/proposals/inventory.json` (45 entries) | ✓✓✓ (별 계열) |
| **nexus** | **27 roadmap_*** 도구 (다수) | 본래 엔진이었음 | `state/proposals/inventory.json` (6 entries) | ✓✓✓ (roadmap 계열) |

roadmap meta engine scaffolds (hexa-lang 로 이관됨):
- **13 stubs** 전부 `status: stub` — `blocker_inventory_scan` / `loss_free_roi_scan` /
  `status_transition_proposer` / `roadmap_dep_graph` / `roadmap_drift_detector` /
  `roadmap_velocity` / `roadmap_health_score` / `roadmap_next_action` /
  `roadmap_to_changelog` / `roadmap_pr_linker` / `roadmap_release_plan` /
  `roadmap_cluster_detect` / `roadmap_continuous_scan`
- `tool/roadmap_engine.hexa` dispatcher 는 528 LOC 로 존재 (이건 nexus 시절 실제
  구현체였던 것 이관)

---

## 2. 비교 매트릭스 (관찰 axis × 프로젝트)

**범례**: ✅ real-v1 완성 · 🟡 partial · ❌ 없음 · N/A 해당 없음

| 관찰 axis | hexa-lang | airgenome | n6-arch | anima | nexus roadmap |
|---|---|---|---|---|---|
| blocker inventory | ✅ hx_blocker_scan | ❌ | ❌ | 🟡 conflict_detect | 🟡 stub |
| loss-free ROI | ✅ hx_loss_free_roi_scan | ❌ | ❌ | ❌ | 🟡 stub |
| stage/parity | ✅ hx_stage_health | N/A | N/A | N/A | N/A |
| dup helper → stdlib | ✅ hx_stdlib_promote | ❌ | ❌ | ❌ | ❌ |
| bench regression | ✅ hx_bench_drift | ❌ | ❌ | ❌ | ❌ |
| selftest coverage | ✅ hx_coverage_scan | ❌ | ❌ | ❌ | ❌ |
| lang spec vs impl drift | ✅ hx_api_drift | N/A | N/A | N/A | N/A |
| rule enforcement (raw#N) | ✅ hx_raw_coverage | ❌ loop-rules 는 n6 것 | 🟡 loop-rules.json | ❌ | ❌ |
| cert chain integrity | ✅ hx_cert_chain | ❌ | ❌ | 🟡 cert tools 별도 | ❌ |
| AOT binary staleness | ✅ hx_aot_stale | ❌ | ❌ | ❌ | ❌ |
| tool graph / orphan | ✅ hx_tool_graph | ❌ | ❌ | ❌ | ❌ |
| pattern mine (n-gram) | ✅ hx_pattern_mine | ❌ | ❌ | ❌ | ❌ |
| continuous orchestrator | ✅ hx_continuous_scan (13 scanner) | 🟡 bin/ag_meta doctor/continuous-scan | ❌ | ❌ | 🟡 stub |
| telemetry (self-obs) | ✅ hx_meta_telemetry | ❌ | ❌ | ❌ | ❌ |
| gap proposer | ✅ hx_meta_gap_proposer | ❌ | ❌ | ❌ | ❌ |
| declarative DSL | ✅ hx_declare_scanner v1 | ❌ | ❌ | ❌ | ❌ |
| ring integrity (genome) | N/A | ✅ ag_ring_integrity | N/A | N/A | N/A |
| forge process health | N/A | ✅ ag_forge_health | N/A | N/A | N/A |
| ring divergence (3-host) | N/A | ✅ ag_ring_divergence | N/A | N/A | N/A |
| dispatch coverage (modules) | N/A | ❌ ag_dispatch_coverage | N/A | N/A | N/A |
| rule effect map | N/A | ❌ ag_rule_effect_map | N/A | N/A | N/A |
| infra parity (3-host) | N/A | ❌ ag_infra_parity | N/A | N/A | N/A |
| forecast hit rate | N/A | ❌ ag_forecast_hit_rate | N/A | N/A | N/A |
| evolution velocity | N/A | ❌ ag_evolution_velocity | N/A | N/A | N/A |
| compute cost accounting | N/A | ❌ ag_compute_cost | N/A | N/A | N/A |
| mutation motif mining | N/A | ❌ ag_mutation_motif | N/A | N/A | N/A |
| arithmetic invariant | N/A | N/A | ❌ n6_arithmetic_invariant | N/A | N/A |
| derivation lineage | N/A | N/A | ❌ n6_derivation_lineage | N/A | N/A |
| atlas integrity | N/A | N/A | ❌ n6_atlas_integrity | N/A | N/A |
| domain coverage | N/A | N/A | ❌ n6_domain_coverage | N/A | N/A |
| paper ↔ technique lineage | N/A | N/A | ❌ n6_paper_lineage | N/A | N/A |
| Monte Carlo stability | N/A | N/A | ❌ n6_monte_carlo | N/A | N/A |
| causal chain verify | N/A | N/A | ❌ n6_causal_chain | N/A | N/A |
| external citation drift | N/A | N/A | ❌ n6_external_citation | N/A | N/A |
| nexus dep drift | N/A | N/A | ❌ n6_nexus_dep_drift | N/A | N/A |
| loop-rules exec | N/A | N/A | ❌ n6_loop_rule_exec | N/A | N/A |
| experiment repro | N/A | N/A | ❌ n6_experiment_repro | N/A | N/A |
| falsification preservation | N/A | N/A | ❌ n6_falsification | N/A | N/A |
| Proof Obligation DSL | N/A | N/A | ❌ (Phase 6.4 고유) | N/A | N/A |
| roadmap blocker scan | N/A (roadmap is repo-agnostic) | — | — | — | 🟡 stub |
| roadmap ROI scan | — | — | — | — | 🟡 stub |
| roadmap status transition | — | — | — | — | 🟡 stub |
| roadmap dep graph | — | — | — | — | 🟡 stub |
| roadmap drift detect | — | — | — | — | 🟡 stub |
| roadmap velocity | — | — | — | — | 🟡 stub |
| roadmap health score | — | — | — | — | 🟡 stub |
| roadmap next action | — | — | — | — | 🟡 stub |
| roadmap → changelog | — | — | — | — | 🟡 stub |
| roadmap PR linker | — | — | — | — | 🟡 stub |
| roadmap release plan | — | — | — | — | 🟡 stub |
| roadmap cluster detect | — | — | — | — | 🟡 stub |
| roadmap continuous scan | — | — | — | — | 🟡 stub |
| proposal inventory | 🟡 최소 v1 | 🟡 최소 v1 | 🟡 최소 v1 | ✅ **14 tool 성숙** | 🟡 최소 v1 |
| proposal cluster detect | ❌ | ❌ | ❌ | ✅ anima only | ❌ |
| proposal conflict detect | ❌ | ❌ | ❌ | ✅ anima only | ❌ |
| proposal lineage graph | ❌ | ❌ | ❌ | ✅ anima only | ❌ |
| proposal dashboard | ❌ | ❌ | ❌ | ✅ anima only | ❌ |

---

## 3. Gap 집계

### 3.1 per-repo gap 수

| repo | total axes | 구현 | gap |
|------|---|---|---|
| hexa-lang (self) | 16 | 16 | **0** ✅ |
| hexa-lang (proposal 성숙도) | 5 | 1 | 4 (inbox 외 cluster/conflict/lineage/dashboard 가능) |
| hexa-lang (roadmap engine stubs) | 13 | 0 | **13** |
| airgenome | 10 | 3 | **7** |
| n6-architecture | 12 | 0 | **12** (+ Proof Obligation DSL Phase 6.4) |
| anima (cross-adopt hexa-lang 축) | 16 | 0* | N/A (본질 다름 — 학습/코히어런스) |
| nexus (이관 완료 후) | — | — | legacy 27 tool 재평가 필요 |

\* anima 는 compiler/forge 관찰이 본질 목표 아님. 자체 메타는 14 proposal_* 로 구현됨.

### 3.2 가장 시급한 구현 3

1. **roadmap meta engine 13 stubs → real-v1** (hexa-lang, SSOT 이관 완료 but 로직 공백)
2. **n6-architecture 0 → 최소 3** (arithmetic_invariant · loop_rule_exec · external_citation_drift
   부터 — 가장 작은 LOC 로 큰 효과)
3. **airgenome 7 gap** 중 **ag_infra_parity + ag_dispatch_coverage** (3-host parity 실측 요구)

---

## 4. 상호보완 매핑

### 4.1 코드 재사용 루트 (from → to)

| source tool | target repo | 적용 방식 |
|---|---|---|
| hexa-lang `hx_coverage_scan` | airgenome `ag_coverage_scan` | 그대로 copy + path rewrite (selftest 커버리지 패턴 동형) |
| hexa-lang `hx_tool_graph` | airgenome · n6 공통 | stems 대신 `modules/*.hexa` 또는 `engine/*.hexa` 로 sweep |
| hexa-lang `hx_aot_stale` | airgenome · n6 | bin/ 스캔 패턴 동형 |
| hexa-lang `hx_pattern_mine` | airgenome (forge log n-gram) · n6 (techniques n-gram) | 입력 dir 만 교체 |
| hexa-lang `hx_raw_coverage` | n6 `n6_loop_rule_exec` | loop-rules.json 재사용 (원래 n6 것) — 양방향 흡수 |
| hexa-lang `hx_meta_telemetry` | 전 repo 공통 | schema 통일 → meta_telemetry.jsonl 표준화 |
| hexa-lang `hx_meta_gap_proposer` | 전 repo 공통 | git log + .raw-audit grep 동일 로직 |
| hexa-lang `hx_declare_scanner` | 전 repo 공통 | `scanners/*.meta.hexa` DSL 표준화 |
| **anima `proposal_cluster_detect`** | **hexa-lang proposal_inbox** | inbox 에 cluster 축 도입 (현재 없음) |
| **anima `proposal_conflict_detect`** | **hexa-lang proposal_inbox** | 상충하는 제안 자동 감지 |
| **anima `proposal_lineage_graph`** | **hexa-lang proposal_inbox** | 제안 간 derivation DAG |
| **anima `proposal_dashboard`** | **hexa-lang proposal_inbox** | 5-repo 통합 대시보드 |
| hexa-lang `proposal_inbox` (이 세션 구축) | **anima** | advisory/broadcast subcommand + sister_repos 자동 동기 |
| nexus `roadmap_engine` 실체 | hexa-lang 13 stub 채움 | nexus 원본 로직 port (engine dispatcher 는 이미 이관, sub-tool 만) |
| airgenome `ag_forge_health` 의 heartbeat 패턴 | hexa-lang `hx_continuous_scan` | 스캐너 자체 health check 도입 |
| airgenome `ag_ring_divergence` 의 Jaccard | anima 측 seed diversity · n6 측 domain overlap | 집합 diff 로직 일반화 |
| n6 `Proof Obligation DSL` (Phase 6.4) | hexa-lang / airgenome / anima | 모든 repo 가 README claim 을 DSL 로 기계검증 |
| n6 `falsification preservation` | anima (반증된 가설 재검 자동화) | 유지 규칙으로 일반화 |
| **hexa-lang `.find()` string fix** (ea3f9496) | 모든 repo 의 AOT 빌드 | 공통 hexa-lang binary 개선 — rebuild 후 혜택 |
| **hexa-lang `hexa_v2` Mac ARM64 advisory** | 모든 repo pod bootstrap | 공통 회피법 공유 |

### 4.2 schema 통일 후보

| 스키마 | 현재 상태 | 통일 action |
|---|---|---|
| proposal_inventory.v1 | 5 repo 모두 사용 중 (anima 45 entries, 나머지 1-3) | anima 수준까지 필드 확장 (cluster/conflict/lineage 지원) — backward compatible |
| meta_scanner 출력 | hexa-lang 만 `hexa-lang/<name>/1` 형식 | airgenome/n6 도 `airgenome/<name>/1` / `n6-architecture/<name>/1` 패턴 표준화 (이미 시작) |
| continuous_scan history | hexa-lang 만 존재 | airgenome/n6 도 `state/history/<date>_<ts>/` 관례 적용 |
| telemetry.jsonl | hexa-lang 만 | 5 repo 공통 append-only 이벤트 |
| declare_scanner DSL | hexa-lang v1 만 | 5 repo 공통 DSL 초안 합의 필요 |

### 4.3 공통 도구 승격 후보 (stdlib 급)

hexa-lang 의 `hx_stdlib_promote` 스윕 결과 `_home` (58 repos), `_iso_now` (29),
`_arg_value` (30), `_json_esc` (다수) 등이 모든 meta scanner 에 반복 — **stdlib/meta/helpers.hexa**
(또는 `stdlib/utils/`) 에 승격하면 5 repo 전체 scanner LOC ~20-30% 감소.

- `_home()`, `_iso_now()`, `_iso_compact()`, `_arg_value()`, `_has_flag()`, `_json_esc()`,
  `_split_lines()`, `_sh_q()`, `_atoi()`, `_exists()`, `_index_of_str()`, `_push_unique()`

---

## 5. 액션 플랜 (proposal_inbox 로 배포)

### 5.1 hexa-lang 자기 inbox (roadmap engine 13 stubs real-v1)

```
proposal_inbox submit --to hexa-lang --kind tool --priority 75 \
  --title "roadmap meta engine 13 stubs real-v1 (nexus 원본 로직 port)" \
  --prompt-ref "docs/roadmap_engine_continuous_meta_proposal_20260422.md"
```

### 5.2 airgenome inbox (남은 7 scanner)

```
proposal_inbox submit --to airgenome --kind tool --priority 70 \
  --title "남은 7 scanner real-v1: ag_dispatch_coverage · ag_rule_effect_map · ag_infra_parity · ag_forecast_hit_rate · ag_evolution_velocity · ag_compute_cost · ag_mutation_motif" \
  --prompt-ref "docs/airgenome_meta_evolution_proposal_20260423.md §Phase 3"
```

### 5.3 n6-architecture inbox (첫 3 scanner minimum path)

```
proposal_inbox submit --to n6-architecture --kind tool --priority 75 \
  --title "최소 3 scanner real-v1: arithmetic_invariant + loop_rule_exec + external_citation_drift" \
  --prompt-ref "docs/n6_meta_evolution_proposal_20260423.md §Phase 3"
```

### 5.4 anima ↔ hexa-lang 상호 흡수 2건

```
proposal_inbox submit --to anima --kind cluster --priority 65 \
  --title "proposal_inbox advisory broadcast + sister_repos 자동 동기 흡수 (hexa-lang 이 추가한 축)"

proposal_inbox submit --to hexa-lang --kind cluster --priority 65 \
  --title "proposal_inbox 에 cluster/conflict/lineage/dashboard 축 추가 (anima 14 tool 에서 포팅)"
```

### 5.5 공통 stdlib/meta helpers 승격 (broadcast)

```
proposal_inbox advise --title \
  "meta scanner 공통 helpers (_home/_iso_now/_arg_value/_json_esc/_split_lines/_sh_q/_atoi 등) 를 hexa-lang stdlib/meta/helpers.hexa 로 승격 — 5 repo scanner LOC 20-30% 감축"
```

---

# 6. 고갈 브레인스토밍 (A-Z 축)

## A. Meta engine 의 일반 이론 (A-01 ~ A-08)

- A-01 meta 는 "무엇을 본다" vs "어떻게 본다" 의 이원성 — 모든 축은 둘 중 하나
- A-02 관찰 대상 카테고리: 소스/바이너리/런타임/데이터/문서/외부링크/사람
- A-03 관찰 주기: on-demand / pre-commit / continuous (12h) / periodic recheck
- A-04 signal strength: count · ratio · drift · outlier
- A-05 action on signal: propose / warn / block / auto-fix (대부분 propose-only)
- A-06 meta system 이 직접 만든 변화의 측정 (효과 추적)
- A-07 meta engine 자체의 성능 (bench/health/gap)
- A-08 meta engine → meta-meta (무한 재귀 방지 = Phase 6 에서 명시)

## B. 분야별 스캐너 일반화 (B-01 ~ B-08)

- B-01 code → data: hexa-lang is code, airgenome is data-forge, n6 is claim-truth, anima is consciousness
- B-02 같은 "blocker inventory" 축도 도메인에 따라 입력이 다름 (코드 selftest vs forge log vs MC z-score)
- B-03 generic scanner skeleton + domain-specific input plugin 의 2-층 구조 권장
- B-04 domain-specific 은 이 repo 고유 — 공통 승격 금지
- B-05 generic 은 5 repo 전부 — 공통 stdlib 승격 권장
- B-06 domain transition (forge → consciousness 등) 발생 시 generic 재사용도 깨질 수 있음
- B-07 plugin 인터페이스 SSOT — 아직 없음, 필요 시 Phase 6.3 DSL 이 대체
- B-08 domain ontology 공유 (5 repo 공통 용어) 정리 필요

## C. Temporal axes (C-01 ~ C-06)

- C-01 snapshot (t=now) — hexa-lang 의 기본
- C-02 drift (t - t-1 vs t0) — bench_drift, cert_chain
- C-03 rolling window — bench 7d mean vs 24h
- C-04 history archive — continuous_scan 에서 시작
- C-05 projection (t+1 forecast) — airgenome forecast_hit_rate 만
- C-06 calibration (이전 forecast 의 실측 비교) — airgenome 만

## D. Spatial axes (D-01 ~ D-06)

- D-01 single-host — hexa-lang / n6
- D-02 multi-host — airgenome (3 ring)
- D-03 cross-repo — proposal_inbox, roadmap_dep_graph
- D-04 external (GitHub, Zenodo, DOI) — n6 external_citation_drift
- D-05 cross-layer (code ↔ doc ↔ paper) — n6 paper_lineage
- D-06 cross-domain (AI ↔ physics ↔ life) — n6 domains/ cross-category

## E. Observer 의 자기반성 (E-01 ~ E-06)

- E-01 scanner 가 자기 결과 측정
- E-02 scanner 가 다른 scanner 관찰 (meta_gap_proposer)
- E-03 scanner 가 자기 진화 (DSL)
- E-04 scanner 승격 / 폐기 lifecycle
- E-05 scanner 신뢰도 (accept/ignore 비율) 추적
- E-06 scanner의 scanner (재귀 경계 정의)

## F. Actionability (F-01 ~ F-06)

- F-01 read-only propose (기본)
- F-02 dry-run capable
- F-03 auto-fix (극소수 — rotation, normalization 등)
- F-04 block (pre-commit 일부만)
- F-05 alarm (slack webhook)
- F-06 ticket 발행 (별도 시스템 — 아직 없음)

## G. Schema / format (G-01 ~ G-06)

- G-01 JSON 출력 표준 (`<prefix>/<name>/1`)
- G-02 JSONL append 이벤트
- G-03 markdown human review
- G-04 mermaid graph export
- G-05 HTML dashboard (미구현)
- G-06 OpenAPI / GraphQL 외부 노출 (미구현)

## H. Cross-repo sync patterns (H-01 ~ H-06)

- H-01 push (submit to target) — proposal_inbox
- H-02 broadcast (advisory to all) — proposal_inbox.advise
- H-03 pull (poll peer state) — 아직 없음
- H-04 reactive (peer event → trigger) — 아직 없음
- H-05 cert chain (derivation + signature) — n6 Phase 6.4 DSL + hexa-lang cert_chain
- H-06 consensus (다수결) — 아직 필요 없음

## I. Stability + safety (I-01 ~ I-06)

- I-01 scanner 가 파괴적 action 하지 않음 (기본 원칙)
- I-02 uchg / hook 우회 금지 (자동화 깨짐 방지)
- I-03 shell metachar 경고는 informational — 실행 계속
- I-04 선언 실행 시 timeout 필수 (bench_drift)
- I-05 resource bound (api_drift 는 2분 — v2 단일 alternation 최적화)
- I-06 host crash 시 부분 결과로 진행 (AG10 원칙)

## J. Performance (J-01 ~ J-06)

- J-01 continuous_scan 실행 시간 (현재 hexa-lang 176s)
- J-02 scanner 별 ROI (runtime vs candidates)
- J-03 hottest scanner 최적화 대상 (api_drift 102s → 1 pass)
- J-04 cache 전략 (SHA 기반 intermediate 재사용)
- J-05 parallel scan (hexa 의 concurrency 제약 vs shell | xargs)
- J-06 pre-compute (nightly 전처리 vs 즉시)

## K. Observability of observability (K-01 ~ K-04)

- K-01 meta engine uptime/coverage
- K-02 scanner 결과 trend 대시보드
- K-03 Alert fatigue 측정 (FP 비율)
- K-04 사용자 행동 로그 (어떤 scanner 결과를 읽었는가)

## L. Evolution (L-01 ~ L-06)

- L-01 scanner v1 → v2 이관 정책
- L-02 scanner 퇴출 기준
- L-03 새 scanner 도입 프로세스 (gap_proposer → 승인 → register → 실행)
- L-04 cross-repo scanner 표준화 (generic skeleton)
- L-05 scanner 하위호환 (output schema versioning)
- L-06 breaking change 알림 경로

## M. Integration (M-01 ~ M-06)

- M-01 git pre-commit — pre-commit-inbox-check 완성 (간단)
- M-02 git pre-push — stale escalate + inbox check
- M-03 launchd / systemd / cron — hexa-lang 만 plist 존재
- M-04 CI (GitHub Actions) — 아직 없음
- M-05 Slack webhook — proposal 있으나 미구현
- M-06 email digest — 미구현

## N. Docs / communication (N-01 ~ N-04)

- N-01 scanner output markdown 자동 생성
- N-02 cross-repo dashboard 단일 페이지
- N-03 README 자동 업데이트 (badge)
- N-04 Zenodo/DOI 연계 (n6 만)

## O. Education / onboarding (O-01 ~ O-04)

- O-01 scanner 작성 가이드
- O-02 DSL 사용 예시
- O-03 inbox 사용 규약
- O-04 새 maintainer onboarding 체크리스트

## P. Long-term (P-01 ~ P-06)

- P-01 5 repo 통합 health score (단일 0-100)
- P-02 cross-repo trend 시각화
- P-03 meta engine self-retrospective (분기별)
- P-04 scanner 성숙도 기준 정립
- P-05 외부 공유용 export (다른 프로젝트가 이 체계 이식)
- P-06 Meta-engine 기반 연구 (관찰자 관찰의 이론화)

---

## 7. 본 세션에서 즉시 채울 수 있는 것 (immediate fillers)

다음 이번 세션에서 proposal_inbox 로 실제 배포:

1. ✅ **[hexa-lang inbox]** roadmap meta engine 13 stubs real-v1 구현 요청
2. ✅ **[airgenome inbox]** 남은 7 scanner real-v1 구현 요청
3. ✅ **[n6-architecture inbox]** 최소 3 scanner (arithmetic + loop_rule + citation) 요청
4. ✅ **[anima inbox]** proposal_inbox advisory broadcast 흡수 요청
5. ✅ **[hexa-lang inbox]** anima 14 proposal tool 에서 cluster/conflict/lineage/dashboard 포팅
6. ✅ **[broadcast advise]** meta scanner 공통 helpers stdlib/meta/helpers.hexa 승격

이 6건은 아래 "배포" 섹션에서 실제 submit 명령어로 실행.

---

## 8. 실측 앵커 (이 분석에서 즉시 드러난 것)

1. **airgenome bin/ag_meta 는 완전한 bash 인라인 dispatcher** — tool/ag_meta.hexa 없이도 작동.
   hexa-lang `bin/hx_meta` 는 tool 호출 shim. 두 패턴 혼재 — 표준화 논의 필요.
2. **anima proposal_inventory.v1 은 다른 repo 보다 훨씬 성숙** (45 entries + 14 관리 tool).
   hexa-lang proposal_inbox 는 minimal v1 — anima 수준으로 흡수 필요.
3. **roadmap meta engine 13 stubs 는 hexa-lang 으로 이관 완료했지만 실 구현 0%** — SSOT
   옮겼는데 로직 공백. `tool/roadmap_engine.hexa` 528 LOC 은 nexus 에서 포팅된
   dispatcher (실제 작동)지만 하위 13 scanner 가 전부 stub.
4. **hexa-lang 16 hx_* 중 api_drift 102s** — grep 114 회 serial. v2 단일 alternation 최적화
   1순위 (전체 continuous_scan 의 58% 시간 차지).
5. **n6-architecture 는 0 구현이지만 제안서 664 LOC** — 다른 repo 가 구현하기 쉬운
   상태. Proof Obligation DSL (Phase 6.4) 은 n6 외 repo 도 흡수 가치 높음.
6. **5 repo 전부 proposal_inventory.v1 schema 통일됨** (이번 세션 산출물) —
   cross-repo 메시지 버스 가능.
7. **loop-rules.json 의 소유** 는 n6 이지만 패턴 자체는 hexa-lang hx_raw_coverage 와 동형 —
   n6 로 roundtrip 흡수 후보.
8. **anima cert 도구들** (별도 존재) + hexa-lang hx_cert_chain 은 같은 `.meta2-cert/` 를 보는
   두 관점. 통합 cert 계열 scanner family 가능성.

---

## 9. 안전 원칙 (모든 repo 공통)

1. 모든 scanner read-only (제안만, mutation 금지)
2. `.roadmap` 수정은 오직 roadmap_with_unlock.hexa 경유
3. hooks 우회 (`--no-verify`, `--no-gpg-sign`) 금지
4. cross-repo 쓰기는 proposal_inbox submit 로만 (직접 편집 금지)
5. 외부 URL probe 는 샘플링 (rate limit 준수)
6. 공통 helpers 승격 시 backward compatible
7. schema v1 확정된 필드는 유지 (새 필드 추가만 허용)
8. advisory 는 prio 75 고정 (제안과 명확히 구분)

---

## 10. 메모

본 문서는 hexa-lang 세션에서 2026-04-23 에 작성. 5 repo + roadmap meta engine
을 단일 시점 비교. 다음 세션들은 각자 inbox 에서 이 분석의 파생 proposal 을
픽업해 진행하면 됨.

핵심 insight:
- **hexa-lang 이 meta engine 성숙도 최고 (16/16)** — 다른 repo 가 여기서 흡수
- **anima 가 proposal 관리 성숙도 최고 (14 tool)** — hexa-lang proposal_inbox 가 흡수
- **n6 가 DSL/proof obligation 독창성 최고** — 5 repo 가 흡수 가능
- **airgenome 이 domain-specific ring/forge 관찰 독창성 최고** — 3-host parity 패턴
- **nexus 는 roadmap engine 의 원래 고향** — 13 stubs 에 원본 로직 이식 가치

5 repo 는 모두 "관찰자(meta)" 이면서 서로가 서로의 관찰 대상. 상호보완은
copy-paste 가 아니라 서로 다른 각도에서 같은 reality 를 본다는 전제에서
출발해야 한다.

# roadmap engine continuous + meta automation proposal (2026-04-22)

요청자: anima session.
대상: hexa-lang maintainer 세션.
범위: 3 repo 공통 (anima · hexa-lang · airgenome).

SSOT 이관 (2026-04-22): 기존 `/Users/ghost/core/nexus` 에 있던 roadmap engine infra (roadmaps/ · state/ · tool/ · config/) 를 `/Users/ghost/core/hexa-lang` 으로 전면 이관. hexa-lang 이 self-hosting SSOT (consumer + engine).

배경: anima 세션 진행 중 반복 발견된 패턴 — blocker, ROI, status transition, drift, regression, velocity 등이 사용자 수동 brainstorm 으로만 발굴됨. 공용 로직으로 영구화 + 자동화 필요. 본 문서는 다른 세션에 그대로 paste 하여 구현 의뢰 가능한 self-contained 프롬프트.

---

## 자동화 아키텍처 5-Phase

| Phase | 기능 | tool 개수 |
|---|---|---|
| 1 | blocker 인벤토리 | 1 |
| 2 | 무손실 ROI 인벤토리 | 1 |
| 3 | meta 자동화 (status/dep/drift/velocity/health/next/changelog/PR/release/cluster) | 10 |
| 4 | roadmap_engine CLI 확장 | (subcommand 9) |
| 5 | 연속 실행 (12h cron) + history archive | 1 |

총 신규 도구 13개 + roadmap_engine 확장.

---

# 전달용 프롬프트 (paste-ready)

```
Working dir: /Users/ghost/core/hexa-lang  (공용 roadmap engine SSOT — self-hosting)
관련 위치:
  - $WS = /Users/ghost/core
  - $HEXA_LANG = /Users/ghost/core/hexa-lang
  - $HEXA_LANG/bin/roadmap_engine (canonical query CLI)
  - $HEXA_LANG/roadmaps/<proj>.json (3 repo aggregator: anima, hexa-lang, airgenome)
  - $HEXA_LANG/state/ (engine outputs · scan results)
  - $HEXA_LANG/tool/ (engine hexa tools)
  - $HEXA_LANG/config/launchd/ (continuous runner plist)
  - 각 repo .roadmap (uchg-locked)
memory:
  - project_roadmap_engine.md · project_workspace.md · project_uchg_ssot.md

Task: roadmap engine 에 3-Phase 자동화 추가 — (Phase 1) blocker 인벤토리, (Phase 2) 무손실 ROI 인벤토리, (Phase 3) meta 자동화 (자체 운영). 3 repo 공통 적용 (anima/hexa-lang/airgenome). hexa-lang 이 SSOT 이자 consumer (self-hosting).

배경:
  anima 세션 (2026-04-22) 진행 중 반복적으로 발견:
  - 매번 사용자가 수동으로 brainstorm + sub-agent batch 로 발굴
  - blocker, ROI, status transition, drift, regression 등이 노출 안 되거나 늦게 발견
  - 공용 로직으로 영구화 + 자동화 필요

═══════════════════════════════════════════════════════════
## Phase 1 — blocker 인벤토리

도구: tool/blocker_inventory_scan.hexa
입력 sources:
  - <repo>/state/roadmap_progress.json (probes [].pass==false)
  - <repo>/state/file_completion_ledger.json (missing_files)
  - <repo>/state/*_audit.json (verdict in [GAP, FAIL, REVIEW, BLOCKED, PARTIAL, ENV_BLOCKED, SOURCE_MISMATCH, AUTH_REQUIRED])
  - <repo>/state/*_dryrun.json
  - <repo>/state/*_completion.json (blocked_by_* fields)

출력: $HEXA_LANG/state/blocker_inventory_<repo>.json
{
  "schema": "hexa-lang/blocker_inventory/1",
  "ts": "<iso>", "repo": "...",
  "blockers": [
    {"id": "auto-N", "source": "<file:line>", "kind": "probe_fail|missing_artifact|env_blocked|gap|review|partial|source_mismatch|auth_required",
     "description": "...", "blocking_roadmap_entry": <id or null>,
     "remediation_hint": "...", "priority": "high|med|low",
     "first_seen_ts": "<iso>", "last_seen_ts": "<iso>", "age_hours": N}
  ],
  "summary": {...}
}

═══════════════════════════════════════════════════════════
## Phase 2 — 무손실 ROI 인벤토리

도구: tool/loss_free_roi_scan.hexa
입력 sources:
  - <repo>/state/code_quality_audit.json (dead_fns, dup_utils, schema_missing, no_selftest, lint_violations)
  - <repo>/state/security_roi_audit.json (FP candidates)
  - <repo>/state/edu_cell_audit.json (skeleton classification)
  - <repo>/state/tool_dir_completion_audit.json (SPEC-INTENTIONAL → IMPL 후보)
  - <repo>/state/dep_vuln_scan_result.json (upgradable deps)
  - <repo>/state/license_compliance.json (REVIEW items)
  - <repo>/docs/*roi*.md (수동 brainstorm 흡수)

출력: $HEXA_LANG/state/loss_free_roi_inventory_<repo>.json
{
  "schema": "hexa-lang/loss_free_roi_inventory/1",
  "ts": "<iso>", "repo": "...",
  "roi_candidates": [
    {"id": "roi-auto-N", "category": "code_quality|security|perf|docs|infra|...",
     "description": "...", "evidence_source": "<audit json path:field>",
     "estimated_effort": "1h|1d|...", "estimated_impact": "...",
     "risk_level": "0|low|med", "loss_free": true,
     "proposed_roadmap_track": "ops|verifier|lora|...", "proposed_phase": "Px"}
  ],
  "summary": {...}
}

═══════════════════════════════════════════════════════════
## Phase 3 — meta 자동화 (roadmap 자체 운영)

각 항목 = 별도 hexa tool. 모두 read-only + propose-only (자동 .roadmap 수정 없음).

### 3.1 status auto-transition proposer
도구: tool/status_transition_proposer.hexa
- 각 entry probe 100% PASS → "should_be_done" 후보 propose
- 각 active entry 30d+ no-change → "stale_active" alarm
- 각 planned entry 의존성 모두 done → "ready_to_activate" 후보
출력: $HEXA_LANG/state/status_transition_proposals_<repo>.json
+ docs/proposed_status_transitions_<repo>.md (사용자 검토용)

### 3.2 dependency graph + cross-repo resolver
도구: tool/roadmap_dep_graph.hexa
- 각 .roadmap entry depends-on field parse → DAG 구축
- 누락된 dep 추정 (entry 가 다른 entry 의 evidence 를 import 하지만 depends-on 없음)
- cross-repo dep (anima entry 가 hexa-lang entry 의존) 자동 detect
출력: $HEXA_LANG/state/roadmap_dep_graph.json + docs/roadmap_dep_graph.md (mermaid)

### 3.3 drift detector (.roadmap vs 실제 코드)
도구: tool/roadmap_drift_detector.hexa
- evidence field 의 file path 가 실재하는지 확인
- entry 가 done 인데 probe FAIL 인 경우 "regression" 표시
- entry 가 planned 인데 probe 100% PASS → drift (후보)
- evidence file modified mtime > entry done_ts 인 경우 "evidence_drifted"
출력: $HEXA_LANG/state/roadmap_drift_<repo>.json + docs/roadmap_drift_<repo>.md

### 3.4 velocity tracker
도구: tool/roadmap_velocity.hexa
- git log .roadmap → entry status transition 이벤트 추출
- 각 entry 의 planned→active→done lead time 측정
- per-repo / per-track / per-phase 평균/중앙값/p95
- monthly throughput (done 개수)
출력: $HEXA_LANG/state/roadmap_velocity_<repo>.json + docs/roadmap_velocity_<repo>.md

### 3.5 health score
도구: tool/roadmap_health_score.hexa
- 입력: blocker_inventory + drift + velocity + roi_inventory
- score (0-100): blocker 무게치 -30, drift -20, stale -10, regression -40, ROI 미반영 -10, mean_pct + 50
- per-repo + global rollup
출력: $HEXA_LANG/state/roadmap_health_<repo>.json + 통합 docs/roadmap_health_global.md

### 3.6 next-action recommender
도구: tool/roadmap_next_action.hexa
- 입력: 위 모든 인벤토리
- 출력: "다음에 처리할 후보" 추천 — dep 해소 + priority high + 효과 큼 + risk 낮음
- 기준: blocker 영향도 × 1/effort × 1/risk
출력: $HEXA_LANG/state/roadmap_next_actions_<repo>.json + docs/next_actions_<repo>.md

### 3.7 changelog auto-feed
도구: tool/roadmap_to_changelog.hexa
- entry status done 으로 transition 시 → docs/CHANGELOG.md 항목 자동 추가
- conventional commit 형식 (feat:/fix:/chore:/docs:)
- semver bump 추천 (entry 의 phase + track 기반)
출력: docs/CHANGELOG.md prepend (자동 mutate 가능 — 단 .roadmap 은 unchanged)

### 3.8 PR linking
도구: tool/roadmap_pr_linker.hexa
- gh pr list → PR description 에서 #N 패턴 → roadmap entry id 추출
- per-entry "linked_prs": [...] field 자동 populate (state side, .roadmap 미수정)
출력: $HEXA_LANG/state/roadmap_pr_links_<repo>.json

### 3.9 release planner
도구: tool/roadmap_release_plan.hexa
- 다음 release 후보: active entries 중 exit_criteria 90% 달성 + 의존성 해소
- release notes 초안 자동 생성
출력: $HEXA_LANG/state/release_plan_<repo>.json + docs/release_draft_<repo>.md

### 3.10 cluster auto-grouping
도구: tool/roadmap_cluster_detect.hexa
- entry name + track + phase + depends-on 기반 cluster 자동 detect
- 예: "[CP1] dest1 persona LIVE" + "H100 launch monitoring" + "weight pre-cache" → "CP1 cluster"
- 새 cluster suggestion (5+ related entries → cluster 화 propose)
출력: $HEXA_LANG/state/roadmap_clusters_<repo>.json

═══════════════════════════════════════════════════════════
## Phase 4 — 통합 (roadmap_engine 확장)

기존 $HEXA_LANG/bin/roadmap_engine status 명령에 새 subcommand:
  roadmap_engine blockers <proj>      → blocker_inventory
  roadmap_engine roi <proj>           → loss_free_roi_inventory
  roadmap_engine health <proj>        → health score
  roadmap_engine next <proj>          → next action recommendation
  roadmap_engine drift <proj>         → drift detection
  roadmap_engine velocity <proj>      → velocity tracker
  roadmap_engine deps <proj>          → dependency graph
  roadmap_engine propose <proj>       → 통합 propose md
  roadmap_engine release <proj>       → release plan

기본 status 명령 출력에 추가:
  ── ROI Auto-Scan ──
    blockers: N (high=N med=N low=N)
    roi_candidates: N
    health: 87/100
    next: roi-auto-12 (dup util 추출)

═══════════════════════════════════════════════════════════
## Phase 5 — 연속 실행 (continuous)

방법:
  - $HEXA_LANG/config/launchd/com.hexa-lang.roadmap_continuous_scan.plist (12h interval)
  - 호출: hexa $HEXA_LANG/tool/roadmap_continuous_scan.hexa
  내부에서 Phase 1-3 모든 도구 순차 호출 + summary state 갱신
  - resolved candidate 자동 mark (재발견 noise 방지)
  - history archive: $HEXA_LANG/state/history/<date>/
  - notify: SLACK_WEBHOOK_URL 설정 시 health score 변화 알림

═══════════════════════════════════════════════════════════
## Hard constraints
  - DO NOT modify any repo .roadmap (uchg + user gate)
  - DO NOT auto-commit
  - NO .py files (hexa + bash only)
  - 모든 도구 idempotent + dry-run-safe + --selftest 가능
  - candidate 생성 시 evidence path 명시 (외부 검증 가능)
  - resolved candidate 자동 mark (재발견 noise 방지)
  - 3 repo 모두 지원 — shared schema (hexa-lang/blocker_inventory/1 등)
  - 사용자 가시 출력은 항상 docs/*.md (machine-readable 은 state/*.json)
  - changelog auto-feed 만 예외적으로 자동 mutate (단 .roadmap 은 절대 미수정)
  - cross-repo: anima 의 evidence 가 hexa-lang entry 면 양쪽 repo state 모두 갱신

## Test plan
1. anima repo 에 도구 실행 → 알려진 blocker (ENV_BLOCKED ubu1 hexa, AN11 SOURCE_MISMATCH, dest_alm GATED, secret_scanner 26 MEDIUM 등) 탐지
2. anima ROI scan → 알려진 ROI (dup util x47, dead fn 42, schema gap 58 등) 탐지
3. drift detector → done entry probe 회귀 케이스 0 확인
4. velocity tracker → 최근 commit 기준 mean lead time 산출
5. next-action recommender → top 5 propose
6. health score → 80+ 기대 (anima 현재 mean_pct 78)
7. 12h 후 재실행 → resolved candidate 자동 mark + history archive
8. cross-repo dep → anima #75 (β) 와 hexa-lang B20 (deterministic FP) 자동 link

## Success criteria
- 사용자가 "무손실 ROI brainstorm" 수동 요청 없이 매 12h 후보 자동 누적
- blocker 24h+ 미해결 시 priority high 자동 escalation
- 모든 candidate evidence path traceable
- roadmap_engine status 출력에 auto_blockers/auto_roi/health 노출
- 3 repo 통합 health dashboard (docs/roadmap_health_global.md) 단일 페이지
- changelog auto-feed 정상 (entry done → CHANGELOG.md 항목 자동 prepend)
- next-action 추천이 사용자 직관과 70%+ 일치 (수동 verify)

Report: Phase 1-3 도구 path 전체 + audit JSON paths + selftest verdicts + propose md sample + hexa-lang engine 통합 verification + 3 repo cross-link 예시. Under 400 words.
```

---

## 7-axis 매핑 (전략 framework)

본 제안은 **G meta** 축 (로드맵 자체 자동화) 100% 커버.

| axis | 본 제안 커버리지 |
|---|---|
| A launch 최적화 | (별건 — anima ROI v1 27항목 이미 적용) |
| B post-H100 연구 | (별건 — r14 corpus / Mk.XII paper) |
| C 신규 영역 | (별건 — BCI / embodied / memory) |
| D ship | (별건 — API/SDK/brew formula) |
| E infra | hexa stage3 = B55, compute fleet = K56 etc |
| F CPU superlimit | (별건 — synthetic 4-path 사전 검증) |
| **G meta** | **본 제안 100%** — 로드맵 자동 운영 인프라 |

## 후속

다른 6 axis (A-F) 는 본 제안과 독립. 필요 시 각 axis 별 별도 프롬프트 작성 가능.

본 제안 구현 후 효과:
- anima 세션 처럼 사용자가 "무손실 ROI brainstorm 해줘" 수동 요청 0
- 매 12h 자동 누적 → 다음 세션에 즉시 반영
- 3 repo 통합 health dashboard 단일 페이지
- regression 사전 detect (done entry probe 회귀 시 즉시 alarm)

## 메모

본 문서는 anima 세션 (2026-04-22) 에서 생성됨.
구현 책임: hexa-lang maintainer 세션 (engine SSOT).
구현 후 anima/airgenome consumer 측은 자동 혜택 받음.

SSOT 이관 메모 (2026-04-22): 이전 초안은 nexus 를 engine SSOT 로 두었으나, "roadmap engine SSOT 전부 hexa-lang 으로 이관" 결정에 따라 모든 경로/스키마/launchd label 을 hexa-lang 기준으로 치환. 기존 nexus/roadmaps, nexus/state, nexus/tool 자산은 후속 migration 단계에서 hexa-lang/ 하위로 옮기거나 symlink 처리 (본 문서 범위 외).

anima 측 미러: `$ANIMA/docs/upstream_notes/roadmap_engine_continuous_meta_proposal_20260422.md` (지시 시 추가).

# 전 프로젝트 메타+로드맵 엔진 통합 조회 — drill/진화 키워드 + gap 돌파 (2026-04-23)

요청자: user session.
범위: 12 프로젝트 (core 7 + aux 2 + launch_ops 3) + 통합 조회 설계 + gap 돌파
패턴 + 미래 키워드 전환 대응.

관련 선행:
- `docs/meta_engine_cross_repo_analysis_20260423.md` (5-repo 비교)
- `docs/roadmap_engine_continuous_meta_proposal_20260422.md` (3-repo automation)
- `docs/hexa_lang_meta_evolution_proposal_20260422.md` (compiler self-loop)

본 문서는 **drill 진화 후속 키워드** 로도 같은 조회가 가능하도록 SSOT 기반
설계를 중심으로 정리.

---

## 1. 전 프로젝트 현황 매트릭스 (12 repo × 6 축)

**범례**: ✅ = real-v1 완성 · 🟡 = partial/stub · ❌ = 미구현 · 🆕 = 이번 세션 신규

| # | 프로젝트 | 역할 | 메타 엔진 | 로드맵 엔진 | proposal_inbox | .raw baseline | drill hook | 비고 |
|---|---------|------|-----------|-------------|-----------------|---------------|------------|------|
| 1 | **nexus** | discovery-engine 🔭 | 27 roadmap_*, atlas_* 다수 ✅ | 본래 엔진 고향 ✅ | ✅ v1 | ✅ (.raw 소유 없음, hexa-lang mirror) | ✅ | 78 pending (창발 활발) |
| 2 | **anima** | consciousness-engine 🧠 | 14 proposal_* ✅ (cluster/conflict/lineage/dashboard) | ✅ roadmap_ingest + mean_pct 91 | ✅ v1 (45 entries) | 🟡 (.raw-ref mirror) | ✅ | 50 super-proposal done (매우 활발) |
| 3 | **n6-architecture** | system-design 🏗️ | 0 (제안서만 664L) | 🟡 loop-rules.json | ✅ v1 | ✅ .raw-ref | ✅ | proposal-only 상태 |
| 4 | **papers** | paper-distribution 📄 | ❌ | ❌ | ✅ v1 (신규) | ✅ .raw-ref | ✅ 🆕 | Zenodo/DOI 자동화 미연결 |
| 5 | **hexa-lang** | language ⬡ | **16/16 real-v1** ✅ | 13 stub (nexus 이관) | ✅ v1 | ✅ .raw SSOT | ✅ | 유일 .raw 원본 소유 |
| 6 | **void** | terminal 🧬 | ❌ | ❌ | ✅ v1 🆕 | ✅ .raw-ref | ✅ 🆕 | ghostty fork, P1 grid-mode |
| 7 | **airgenome** | os-scanner 🧬 | 4 real (ring/forge/divergence) + 6 stub | ❌ | ✅ v1 | ✅ .raw-ref 🆕 | ✅ | Linux binary 미빌드 blocker |
| 8 | contribution | paper-submission-hub 📚 | ❌ | ❌ | ❌ (미등록) | ❌ | ❌ | aux, 미사용 |
| 9 | openclaw | singularity-feed 🐾 | ❌ | ❌ | ❌ | ❌ | ❌ | aux, 미사용 |
| 10 | skynet-timer | landing-page ⏲️ | ❌ | ❌ | ❌ | ❌ | ❌ | launch_ops, web |
| 11 | x (twitter) | ops 𝕏 | ❌ | ❌ | ❌ | ❌ | ❌ | launch_ops, API |
| 12 | youtoyou | concept 📺 | ❌ | ❌ | ❌ | ❌ | ❌ | launch_ops, 컨셉 |
| +α | **secret, contact** (private) | 저장소/외부소통 | — | — | ✅ v1 🆕 | ✅ .raw-ref 🆕 | ✅ 🆕 | private repo, 공개 금지 |
| +α | hexa-os | kernel/boot/driver | ❌ | ❌ | ✅ v1 🆕 | ✅ .raw-ref 🆕 | ✅ 🆕 | OS 실험 |

### 핵심 관찰
1. **core 7 + hexa-os + secret + contact** (10 repo) = proposal_inbox 합류 완료
2. **aux / launch_ops 5 repo** (contribution/openclaw/skynet-timer/x/youtoyou) = 아직 미합류
3. hexa-lang 만 진짜 `.raw` 소유자 (L0 SSOT), 나머지는 `.raw-ref` mirror pin
4. anima 의 proposal 관리 성숙도 최고 (14 tool) — 다른 repo 가 흡수 가치
5. nexus 는 창발 특성상 구조적 진화 (schema/atlas 재계산) 빈도 가장 높음

---

## 2. 통합 조회 설계 (drill / 진화-후속 키워드)

### 2.1 현재 drill 동작

```
drill <seed>  →  ~/.hx/bin/nexus drill --seed "<seed>"
              │
              ├─ remote heavy-compute (심층 탐색)
              ├─ Mac SIGKILL 위험 시 all_unreachable abort (exit 74)
              └─ preflight: [INBOX] next + evolution feed (자기 repo)
```

**제약**: drill 은 현재 단일 repo 의 local context + remote heavy. 전 프로젝트 통합 조회 기능 없음.

### 2.2 확장 설계 — `drill --cross-repo`

```
drill <seed> --cross-repo [--topic <meta|roadmap|inbox|gap>]
              │
              ├─ (기존) remote drill 실행
              ├─ cross-repo context 주입:
              │    각 sister repo 의 meta+roadmap 상태 dump
              │    → seed 로 cross-repo 관련성 자동 matching
              ├─ 결과: gap 돌파 제안 리스트
              └─ 출력: box-draw 카테고리 표 + TOP N
```

**SSOT driven**: commands.json drill.evolution_plan 의 successor 가 설정되면
자동으로 새 키워드 (예: `메타진화`) 로도 같은 동작.

### 2.3 구현 경로 (최소 → 완전)

#### Phase A: **Cross-repo snapshot tool** (1 day)
`tool/cross_repo_snapshot.hexa`:
- 12 repo 순회 → 각자 `state/proposals/inventory.json` + `.roadmap` summary
- 카테고리별 통합 (lang_gap / resource_gap / infra_gap / atlas_gap / structural_evolution)
- box-draw 통합 출력

```
drill <seed> --cross-repo
  → Phase A: 스냅샷 +  seed 관련성 점수 (title/category grep)
  → box-draw 표: 관련 entries TOP 20 across all repos
```

#### Phase B: **Remote drill + local cross-query 융합** (3 days)
- remote drill 출력의 key phrase 추출
- 전 repo meta engine 의 output JSON 과 cross-match
- gap 돌파 후보: 같은 category + 비슷한 title 로 묶인 multi-repo 아이템

#### Phase C: **Evolution-aware migration** (지속)
- drill 키워드가 `메타진화` / `evolution` 등으로 전환 예정 시
  - commands.json drill.evolution_plan.successor 에 신이름 지정
  - prompt_scan.hexa 가 [ALIAS] 자동 출력 (이미 구현됨, AG14 223574cf)
  - cross-repo 조회 로직은 이름 독립 (SSOT-driven)

---

## 3. gap 돌파 중심 패턴

### 3.1 현재 식별된 cross-repo gap

| gap | 발생 repo | 영향 repo | priority | 돌파 경로 |
|-----|-----------|-----------|----------|----------|
| **hexa Linux binary 미빌드** | hexa-lang | airgenome · anima · nexus pod bootstrap | 98 (CRITICAL) | cross-compile / Docker 레시피 |
| **reserved keyword contextual** | hexa-lang parser | 모든 tool authoring | 96 (→ done 30e20b1d, rebuild pending) | stage0 rebuild |
| **hx_ prefix mangling AOT** | hexa-lang codegen | AOT 빌드 전반 | 98 (이미 fixed 후 rebuild 대기) | stage0 rebuild |
| **3-host parity (airgenome)** | airgenome | anima training · nexus remote | 95 | host failover/quorum |
| **atlas.n6 재계산 주기** | nexus / n6 | papers cite chain | 90 | structural_evolution routine |
| **proposal 관리 흡수 gap** | hexa-lang inbox ← anima | 전 sister repo | 65 | anima 14 tool 포팅 |
| **Linux x86_64 pod bootstrap** | airgenome | nexus drill + anima training | 95 | 상위 (Linux binary) 블록 |

### 3.2 gap 돌파의 구조적 특성

1. **한 repo 의 fix 가 여러 repo 를 unblock**: hexa-lang Linux binary 하나가 3 repo 해결
2. **cross-dependency**: airgenome 의 3-host parity 는 anima H100 training 의존성과 연결
3. **구조적 진화는 nexus 중심**: schema bump / atlas 재계산이 5+ repo 에 영향
4. **silent success**: 완료된 gap (예: rfind ea3f9496) 은 다른 세션이 곧장 혜택 (evolution feed 자동 인지)

### 3.3 gap 돌파 priority 관리

현재 `convention_cross_repo_blocker` (이번 세션 등록, 5 repo `_meta` 필드):
- `priority_floor: 95` — cross-repo blocker 항목은 prio ≥ 95 강제
- sub_categories: lang_gap / resource_gap / infra_gap / atlas_gap / data_gap / structural_evolution

`drill` (또는 후속 키워드) 가 이 priority floor 기반으로 자동 소팅 → TOP N 출력.

---

## 4. 12-repo 통합 조회 SSOT 설계

### 4.1 공통 스키마 (이미 확립)

각 repo `state/proposals/inventory.json`:
```json
{
  "schema": "<repo>.proposal_inventory.v1",
  "id_prefix": "<short>",
  "entries": [...],
  "_meta": {
    "owner_repo": "...",
    "sister_repos": { 10-repo map },
    "convention_cross_repo_blocker": {
      "priority_floor": 95,
      "sub_categories": [...]
    }
  }
}
```

### 4.2 통합 조회 명령 (제안)

```bash
# 기본 cross-repo evolution view
proposal_inbox evolution --all [--days 7]

# 특정 gap 카테고리로 필터
proposal_inbox evolution --all --category lang_gap
proposal_inbox evolution --all --category resource_gap

# priority >= N 만
proposal_inbox evolution --all --min-priority 95

# seed 기반 관련성 매칭 (drill 통합)
drill --seed "..." --cross-repo --view meta
```

### 4.3 roadmap 엔진 통합

각 repo 의 `.roadmap` + `state/proposals/inventory.json` 을 같이 surveyed:

```bash
# 전 repo 로드맵 진행률 요약
roadmap_engine cross-repo-progress

# gap 중 최고 우선순위 n 개 (전 repo 통합)
roadmap_engine cross-repo-top --n 10 --min-priority 90
```

---

## 5. drill/메타진화 진화 경로 — 코드 수준 미래 대비

### 5.1 현재 상태 (2026-04-23)

```json
// commands.json
"drill": {
  "keywords": ["drill", "드릴", "사슬", "chain", "고갈", "saturate"],
  "canonical": "drill",
  "evolution_plan": {
    "status": "stable",
    "successor": null,
    "notice": null,
    "previous_canonicals": []
  }
}
```

### 5.2 전환 시나리오 (drill → 메타진화)

```json
"drill": {
  "keywords": ["drill", "드릴", "사슬", "메타진화", "진화"],  // 신 alias 추가
  "canonical": "drill",                                        // 아직 drill 이 canonical
  "evolution_plan": {
    "status": "deprecating",                                    // ← 전환 시작
    "successor": "메타진화",
    "notice": "drill 은 2026-05-15 부터 '메타진화' 로 rename. 기존 keyword 3 개월 호환 유지.",
    "previous_canonicals": []
  }
}
```

→ prompt_scan.hexa 가 자동으로 [ALIAS] 경고 출력 (이미 AG14 구현).

### 5.3 완전 전환 후

```json
"메타진화": {                                                   // 새 key
  "keywords": ["메타진화", "진화", "evolve", "drill", "드릴"],  // 옛 이름도 당분간
  "canonical": "메타진화",
  "evolution_plan": {
    "status": "stable",
    "successor": null,
    "previous_canonicals": ["drill"]                            // 역사 기록
  }
}
```

**Code 수정 없음** — commands.json SSOT 한 줄 수정으로 전환 완료. 
prompt_scan.hexa 의 check_evolution_plan 이 자동 처리.

---

## 6. 브레인스토밍 appendix — A-Z 축

### A. Cross-repo evolution 조회 (A-01 ~ A-08)

- A-01 `proposal_inbox evolution --all` : 전 repo feed
- A-02 `--since 2026-04-01 --until 2026-04-23` 기간 필터
- A-03 `--topic atlas` 주제별 cross-grep (title/category/notice)
- A-04 `--graph` mermaid graph export (repo × category)
- A-05 `--group-by owner_repo` 그룹핑
- A-06 `--top-activity 3` 가장 활발한 3 repo 만
- A-07 `--stale 30d` 30일 정체 entries
- A-08 `--blocking-graph` 서로 block 관계 DAG

### B. gap 돌파 전략 (B-01 ~ B-08)

- B-01 cross-repo gap cluster 자동 감지 (같은 title 패턴 ≥2 repo)
- B-02 unblocks count: 한 fix 가 해결하는 repo 수 역계산
- B-03 priority auto-bump: unblocks ≥ 3 이면 prio +10
- B-04 gap ancestry: blocker → blocker chain DAG
- B-05 gap resolution ROI: (영향 repo 수 / 작업 LOC)
- B-06 gap half-life: 등록 후 평균 해결 시간
- B-07 silent-success rate: DONE 후 얼마나 많은 repo 가 혜택 받았나
- B-08 gap regression: 해결됐다가 다시 나타난 비율

### C. Drill 키워드 진화 (C-01 ~ C-06)

- C-01 status 기반 hint 강도 (stable=silent, deprecating=info, deprecated=warn, renamed=block)
- C-02 successor 체인 (A → B → C 다단계 rename 추적)
- C-03 alias 사용 통계 (어느 keyword 가 가장 많이 쓰이나)
- C-04 keyword A/B test (새 이름 도입 시 이용 패턴 측정)
- C-05 graceful migration 기간 (3개월 공존, 이후 drop)
- C-06 cross-language keyword sync (한/영 동시 전환)

### D. 로드맵 엔진 통합 (D-01 ~ D-08)

- D-01 전 repo `.roadmap` parse → 통합 DAG
- D-02 cross-repo dependency detection
- D-03 critical path across all repos
- D-04 milestone alignment (같은 deadline 의 multi-repo 작업)
- D-05 phase sync (CP1/CP2/AGI 같은 phase 이름)
- D-06 track intersection (어느 entry 가 어느 track 에 속하나)
- D-07 auto-dispatch: entry ready 되면 담당 repo 알림
- D-08 roadmap evolution: phase 추가/삭제 시 cross-repo 파급

### E. nexus 창발 특성 (E-01 ~ E-06)

- E-01 atlas.n6 재계산 주기 (weekly or raw#N drift)
- E-02 shard gc 자동화 (atlas_shard_gc tool wrap)
- E-03 hub centrality drift (top-100 node 변화)
- E-04 bloom filter saturation (bloom 도구 모니터)
- E-05 discovery rate (70k+ 발견 증분)
- E-06 law emergence tracking (750+ 법칙 추가 cadence)

### F. anima 성숙 흡수 (F-01 ~ F-04)

- F-01 proposal_cluster_detect → 다른 repo 포팅
- F-02 proposal_conflict_detect 이식
- F-03 proposal_dashboard 5-repo 통합 dashboard 化
- F-04 proposal_lineage_graph cross-repo DAG

### G. n6 / papers / void 확장 (G-01 ~ G-06)

- G-01 n6 Proof Obligation DSL 이식 (모든 repo claim 검증)
- G-02 n6 arithmetic_invariant 일반화 (수식 기반 설계 도구)
- G-03 papers Zenodo DOI 자동 업로드 (proposal 완료 → paper submit)
- G-04 void AI-native terminal 연동 (drill 실행 UX)
- G-05 contribution 통합 hub (proposal_inbox export)
- G-06 openclaw 돌파 피드 (nexus discovery → openclaw stream)

### H. hexa-lang (언어 자체) 진화 (H-01 ~ H-06)

- H-01 Linux binary 빌드 (blocker #1)
- H-02 rebuild_stage0 fixture 확립 (내가 고친 건 rebuild 후에 효과)
- H-03 stage3 reproducible emit
- H-04 lang_gap 누적 → 우선순위 기반 parser 확장 roadmap
- H-05 stdlib helpers stdlib/meta/ 승격 (20-30% LOC 감소)
- H-06 cert chain ed25519 signing (HXA-#04)

### I. airgenome host/compute (I-01 ~ I-04)

- I-01 3-host parity scanner real-v1 (현재 stub)
- I-02 forecast hit rate 집계
- I-03 evolution velocity (genome/hour)
- I-04 compute cost accounting ($/genome)

### J. proposal 관리 진화 (J-01 ~ J-06)

- J-01 stale auto-escalate (이미 구현, 7d+)
- J-02 proposal age decay (90d+ prio 절반)
- J-03 proposal → GitHub Issues export
- J-04 proposal lineage graph cross-repo
- J-05 proposal consensus (3+ repo 동의 시 high-prio)
- J-06 proposal conflict detection (상충 제안 자동 감지)

### K. drill remote + local 융합 (K-01 ~ K-04)

- K-01 remote drill 결과 → key phrase extractor
- K-02 key phrase cross-match across 12 repo inventory
- K-03 융합 결과 → proposal_inbox 자동 submit 옵션
- K-04 drill cache (동일 seed 재호출 시 cache 재사용)

### L. launch_ops / aux 통합 (L-01 ~ L-04)

- L-01 contribution repo proposal_inbox 합류 (paper submission gap 추적)
- L-02 openclaw 합류 (discovery feed gap)
- L-03 skynet-timer 제외 (web-only, 거의 정적)
- L-04 x / youtoyou 제외 (content/ops, meta 불필요)

### M. 검증 / 관측 (M-01 ~ M-06)

- M-01 evolution feed accuracy 검증 (ts 파싱 버그 찾기)
- M-02 cross-repo latency (submit → target repo 인지 시간)
- M-03 proposal 처리 throughput (done/week)
- M-04 advisory broadcast hit rate (n 개 repo 읽음)
- M-05 alias guidance 인지율 (deprecated keyword 사용 감소)
- M-06 commit-push helper 호출 통계

### N. 보안 / 무결성 (N-01 ~ N-04)

- N-01 secret/contact private repo 더블체크 (push 시 public 차단)
- N-02 proposal_inbox sanitizer (PII/credential 자동 masking)
- N-03 commit-push --push 승인 gate (prod 브랜치 보호)
- N-04 cert chain 통합 검증 (n6 + hexa-lang cert_chain)

### O. 장기 (O-01 ~ O-04)

- O-01 6개월 retrospective (어느 proposal category 가 가장 impact)
- O-02 meta engine itself 의 가치 평가
- O-03 외부 공유 (다른 multi-repo 그룹에 이 체계 export)
- O-04 AI-driven gap discovery (meta_gap_proposer 고도화)

---

## 7. 실행 우선순위 (즉시 / 단기 / 중기)

### 즉시 (본 세션 또는 다음 세션)
1. ✅ drill preflight evolution feed (이미 커밋 c5818ca7)
2. ✅ drill alias guidance 메커니즘 (223574cf)
3. ✅ nexus structural_evolution proposal 제출 (09aeaff9)
4. ⏳ proposal_inbox evolution --all (cross-repo 스냅샷) — 이 문서 후속
5. ⏳ aux/launch_ops 5 repo 합류 검토 (contribution 우선)

### 단기 (1-2주)
6. airgenome ag_* 7 scanner real-v1 (stub → real)
7. n6 Phase 3 12 scanner real-v1 (0 → 최소 5)
8. roadmap_engine cross-repo-top 구현
9. Linux binary 빌드 (blocker #1 해소)

### 중기 (1-2개월)
10. drill → 메타진화 rename 본격 논의 (evolution_plan deprecating 트리거)
11. n6 Proof Obligation DSL 이식 (papers / hexa-lang cert chain)
12. nexus structural_evolution weekly launchd
13. anima 14 proposal_* 의 hexa-lang 포팅

---

## 8. keyword 미래 대비 (drill → 진화 계열) 체크리스트

드릴이 언젠가 `메타진화` / `evolution` / `deep` 으로 rename 될 경우:

- [x] SSOT 필드 `evolution_plan` 선언 (commands.json)
- [x] prompt_scan.hexa 자동 alias hint (AG14)
- [x] previous_canonicals 역호환 필드
- [ ] cross-repo 조회 로직이 canonical 이름 독립 (Phase A 대상)
- [ ] evolution_plan.status 기반 UI 강도 (stable → silent, deprecating → info, deprecated → warn, renamed → block)
- [ ] user 행동 로그 (어느 키워드 사용 빈도)
- [ ] graceful migration 기간 (3개월 공존)
- [ ] 한/영 동시 전환 시 문서 업데이트 cadence

---

## 9. 안전 원칙 (전 12 프로젝트 공통)

1. 모든 scanner / meta tool read-only (propose-only)
2. `.roadmap` 은 roadmap_with_unlock.hexa 경유만
3. hooks 우회 (`--no-verify`) 금지
4. cross-repo 쓰기는 proposal_inbox submit 로만
5. `.raw` (hexa-lang 전용) 제외하고 다른 repo 는 `.raw-ref` mirror
6. private repo (secret, contact) push 시 public 차단 ( `.workspace` visibility=private)
7. convention_cross_repo_blocker priority_floor=95 강제
8. advisory 는 prio 75 고정

---

## 10. 메모

본 문서는 user ask:
> "그리고 전체 프로젝트 메타 엔진 조회해서 적절히 ... drill 혹은 차후 진화시
> 바뀌는 메인 키워드 를 인지해서 사용할수 있게끔. 특히 gap 돌파 ... 로드맵
> 엔진도. 브레인스토밍 고갈시까지"

에 대한 응답. 핵심 insight:

- **drill 은 이름 바뀔 수 있지만 메커니즘은 SSOT 에 이미 대응** (evolution_plan
  필드, AG14 구현)
- **cross-repo 조회는 keyword 독립** — commands.json canonical 이 바뀌어도 동작
- **gap 돌파의 구조적 특성**: 한 repo fix 가 여러 repo unblock — unblocks
  count 를 priority boost 에 반영해야
- **nexus 는 창발 특성상 structural_evolution 빈도 ↑** — weekly cadence 제안됨
- **aux/launch_ops 5 repo** (contribution/openclaw/skynet-timer/x/youtoyou) 미합류
  — contribution 우선 검토 가치 有

다음 세션에서 곧바로 집어들 수 있는 주제:
1. `proposal_inbox evolution --all` 전 repo 스냅샷 구현
2. Linux binary 빌드 (전체 blocker #1)
3. airgenome ag_* 7 scanner real-v1
4. aux/launch_ops 합류 결정

12 repo × 6축 × 15 gap 축 × 200+ 브레인스토밍 아이템 — 현재 stage 에서
고갈에 근접. 다음 진화 단계는 실제 구현으로 넘어가는 것.

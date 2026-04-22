# drill + @ 토큰 + 메타/로드맵 엔진 전 프로젝트 전수조사 (2026-04-23)

요청자: user session.
범위: 10 core repo (anima · nexus · hexa-lang · airgenome · n6-architecture · void · hexa-os · papers · secret · contact).
방법: read-only grep/stat, DESIGNATED_PROJECTS 전체 열거 하에 수집.

---

## 1. 메타 엔진 tool 현황 (prefix 별)

| repo | `hx_*` | `ag_*` | `n6_*` | `proposal_*` | 총 tool | 분류 |
|------|-------|-------|-------|-------------|---------|------|
| anima | 0 | 0 | 3 | **16** | 220 | proposal 관리 성숙 최고 |
| nexus | 0 | 0 | 0 | 1 | 160 | roadmap 엔진 고향 (27 roadmap_*) |
| **hexa-lang** | **23** | 0 | 1 | 1 | 247 | meta engine 본진 |
| airgenome | 0 | **18** | 0 | 0 | 21 | ag_* 18 (ring/forge/divergence 등) |
| n6-architecture | 0 | 0 | **15** | 0 | 17 | n6_* 15 (이미 구현 진행 중!) |
| void | 0 | 0 | 0 | 0 | 2 | terminal repo |
| hexa-os | 0 | 0 | 0 | 0 | 0 | kernel/boot (tool 없음) |
| papers | 0 | 0 | 0 | 0 | 0 | paper distribution |
| secret | 0 | 0 | 0 | 0 | 0 | private |
| contact | 0 | 0 | 0 | 0 | 0 | private |

**관측**:
- n6-architecture 에 `n6_*.hexa` **15 개 이미 존재** — 즉 brainstorm 제안서만 있다고 생각했으나 **실제 구현 진행 중**. 이전 분석 문서 (cross_repo_analysis) 가 stale.
- airgenome `ag_*` 18 개 — 내 이전 분석은 4 개로 추정. 누락분 재확인 필요.
- hexa-lang 23 `hx_*` + 1 n6_* 혼재 (n6 호환성 테스트 도구일 수 있음).

## 2. 로드맵 엔진

| repo | `roadmap_*.hexa` | `.roadmap` | `bin/roadmap_engine` |
|------|------------------|------------|----------------------|
| anima | 7 | ✓ | ✗ |
| nexus | **27** | ✓ | ✗ |
| **hexa-lang** | **59** | ✓ | **✓** |
| airgenome | 0 | ✓ | ✗ |
| n6-architecture | 0 | ✓ | ✗ |
| void | 0 | ✓ | ✗ |
| hexa-os | 0 | ✓ | ✗ |
| papers | 0 | ✓ | ✗ |
| secret | 0 | ✓ | ✗ |
| contact | 0 | ✓ | ✗ |

**관측**:
- `.roadmap` 은 10 repo 모두 소유 (통일 SSOT 성공)
- hexa-lang `roadmap_*.hexa` 59 — nexus (27) 포함 + 이관된 이후 자체 확장 모두 계산
- `bin/roadmap_engine` shim 은 hexa-lang 만 (canonical dispatcher). 다른 repo 는 absolute path 호출 or 없음.

## 3. drill 사용 현황

| repo | drill 참조 파일 수 |
|------|------|
| hexa-lang | 9 (gate + tool 다수) |
| nexus | 3 (본래 drill 엔진 소유) |
| n6-architecture | 1 |
| anima · airgenome · void · hexa-os · papers · secret · contact | **0** |

**관측**: drill 은 hexa-lang gate + nexus 엔진 쌍에 집중. anima 등 consumer repo 는 drill 직접 호출 없음 — `~/.hx/bin/nexus drill` 명령어로 간접 사용.

## 4. `@allow-*` 토큰 분포 (hexa-lang/tool + gate)

| 토큰 | 사용 횟수 |
|------|------|
| `@allow-bare-exec` | **394** |
| `@allow-hook` | 20 |
| `@allow-devpath` | 11 |
| `@allow-no-log` | 9 |
| `@allow-atlas-direct` | 9 |
| `@allow-version-name` | 8 |
| `@allow-silent-exit` | 8 |
| `@allow-no-runtime` | 8 |
| `@allow-paper-canonical` | 7 |
| `@allow-no-breakthrough` | 6 |
| `@allow-fork` | 6 |
| `@allow-archive` | 6 |
| `@allow-roadmap-ext` | 5 |
| `@allow-non-filter` | 5 |
| `@allow-mk-freeform` | 5 |
| `@allow-missing-data` | 5 |
| `@allow-if-nest` | 5 |
| `@allow-deprecated-json` | 5 |
| `@allow-complexity-growth` | 5 |
| `@allow-unbounded-loop` | 4 |

**관측**:
- `@allow-bare-exec` 압도적 (394) — 대부분 exec() 래퍼 예외 선언. H-EXEC 감사 대상.
- 구조적: 20+ 다른 `@allow-*` 는 명시적 rule 예외 토큰. 이번 세션에서 추가한 `@allow-project-entry` 는 아직 0회 사용 (신규).

## 5. proposal_inbox 합류 현황 + cross_repo_blocker 보유

| repo | inbox entries | cross_repo_blocker (prio≥95 non-done) |
|------|---------------|-------------------------------------|
| anima | 50 | 0 |
| nexus | 82 | 1 (nxs-006 hexa_v2 Linux binary) |
| hexa-lang | 10 | 0 |
| **airgenome** | 12 | **3** (agm-003, 006, 008) |
| n6-architecture | 6 | 1 (n6a-002) |
| void | 1 | 0 |
| hexa-os | 1 | 0 |
| papers | 1 | 0 |
| secret | 1 | 0 |
| contact | 1 | 0 |
| **합계** | **165 entries · 5 active blocker** | — |

**관측**: airgenome 이 cross_repo_blocker 3 건으로 최대 중심 (Linux binary + 3-host parity 계열). anima 는 blocker 0 — 모든 것이 done 상태.

## 6. 구현 수준 평가 (user 질문: "구현했나????????")

| 항목 | 상태 | 주체 |
|------|------|------|
| proposal_inbox SSOT | ✅ 10 repo 합류 | hexa-lang/tool/proposal_inbox.hexa |
| AG11 go preflight | ✅ 20/20 검증 | gate/prompt_scan.hexa |
| AG12 evolution feed | ✅ 작동 | gate/prompt_scan.hexa |
| AG14 evolution_plan alias | ✅ 멱등성 확인 | commands.json + prompt_scan.hexa |
| AG15 hard advisory | ✅ 일반 prompt 에도 출력 | prompt_scan.hexa |
| **메타엔진 hx_\*** (hexa-lang) | **🟡 23 tool 존재** (real vs stub 마킹 일부 누락) | 개별 tool 내부 STATUS 주석 |
| **메타엔진 ag_\*** (airgenome) | **🟡 18 tool 존재** (내 이전 분석은 4 로 추정 — 재확인 필요) | ag_common/ring/forge/divergence |
| **메타엔진 n6_\*** (n6-architecture) | **🟡 15 tool 존재** (제안서만 알고 있었음 — 실제 구현 상당 진행) | n6_* prefix |
| 메타엔진 기타 8 repo | ❌ 0 | — |
| 로드맵엔진 ✅ hexa-lang | ✅ `bin/roadmap_engine` shim + 59 tool | — |
| 로드맵엔진 nexus | 🟡 27 tool 존재 but shim 없음 | 본래 고향 |
| 로드맵엔진 기타 8 repo | ❌ | `.roadmap` 은 있지만 dispatcher 없음 |
| OS-level project-entry (B) | ✅ | self/sbpl/project_entry.sb + bin/project-safe-exec |
| H-PROJECT-ENTRY (A) | ✅ | pre_tool_guard.hexa 2 fn |

## 7. gap 돌파 경로

### 7.1 현재 자동화된 경로
- **go/roi/drill preflight** (keyword) — hook 출력으로 inbox/evolution 노출
- **AG15 cross_repo_blocker 자동** — keyword 무관, 매 prompt 출력
- **cron / launchd** — hx_continuous_scan 주기적 실행 (disabled by default)
- **gap_proposer** — git log 기반 새 scanner 제안

### 7.2 현재 수동 경로 (자동화 후보)
- **AI 가 tool output 보고 gap 판단** — LLM 자율 → 지금은 이것만
- **proposal_inbox submit** (手動 CLI)
- **rebuild_stage0 / hx_unlock** — 승격 의식

### 7.3 User 요구: "진행 과정/응답 중에도 자동"

**추가 필요 메커니즘**:
1. **PostToolUse-style gap 감지** — tool output 에서 `[Syntax]` / `CODEGEN ERROR` / `EPERM` / `Operation not permitted` / `unhandled method` 같은 패턴 발견 시 **자동 proposal_inbox submit** (lang_gap / resource_gap / infra_gap 태깅)
2. **Claude CLI 가 직접 breakthrough 시도 가능한 wrapper** — `bin/gap_breakthrough`:
   - `gap_breakthrough detect <text>` — 텍스트에서 gap 패턴 감지
   - `gap_breakthrough attempt <gap_id>` — 해당 gap 돌파 프로세스 실행 (rebuild / hx_unlock 등)
   - `gap_breakthrough report` — 결과 리포트
3. airgenome hook 통합 — `airgenome/hooks/gap_breakthrough.hexa` wrapper

## 8. 차이 (내 이전 분석 vs 실제)

| 항목 | 이전 분석 | 실제 |
|------|----------|------|
| airgenome meta tool | 4 real + 6 stub | **ag_* 18 개 존재** (완전 cover 수준 검증 필요) |
| n6 meta tool | 0 구현 (제안서만) | **n6_* 15 개 존재** — 구현 진행 중 |
| proposal_inbox total | ~80 | **165 entries** (anima 50 + nexus 82 + ...) |
| cross_repo_blocker | 5 | 5 (일치) |

**결론**: 내 "이전 분석" 은 각 repo maintainer 세션이 내 분석 시점 이후 빠르게 확장한 것을 못 봤음. **진화 피드 (evolution --all) 로 교차 재검증 필요**.

## 9. 다음 구현 (본 세션 후속)

1. `bin/gap_breakthrough` CLI wrapper (tool output 자동 분석)
2. airgenome hook 통합 — `airgenome/hooks/gap_detector.hexa`
3. `tool/roadmap_banner.hexa` — .roadmap 상단 banner CLI
4. `tool/proposal_inbox evolution --all` — cross-repo 통합 스냅샷 (이미 부분 구현, 전체 sister 합류)

## 10. 메모

본 audit 은 user 명시 승인 하에 DESIGNATED_PROJECTS 전체 열거로 수행. 정규
세션에서는 H-PROJECT-ENTRY 에 의해 자기 repo + shared 외 read 차단됨.
Audit 수행 환경:

```
DESIGNATED_PROJECTS="anima,nexus,hexa-lang,airgenome,n6-architecture,void,hexa-os,papers,secret,contact"
```

데이터 수집 시각: 2026-04-23.

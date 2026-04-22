# hexa-lang self meta system — continuous + meta-evolution proposal (2026-04-22)

요청자: user session.
대상: hexa-lang maintainer 세션.
범위: hexa-lang repo 단독 (compiler + stage0/1/2 + stdlib + 60+ tool + self-host cert chain).
관련 제안: `docs/roadmap_engine_continuous_meta_proposal_20260422.md` (3 repo
cross-repo automation). 본 제안은 hexa-lang **자체** 를 관찰·개선·진화시키는
self-loop.

배경
 - hexa-lang 은 self-hosting 언어 (Stage0 C bootstrap → Stage1 → Stage2),
   60+ tool/*.hexa, stdlib, raw#N 규칙 생태계. 사용자가 수동으로 건강도·
   드리프트·기술부채를 관찰하는 것은 불가능.
 - 현재 세션에서 즉시 드러난 패턴:
   - homebrew hexa 0.2.0 vs repo-local 최신 build 간 codegen/parse 드리프트
     (`byte_at unhandled method`, `[Syntax] unexpected token Semicolon`).
   - 13개 신규 stub 에 `_home / _iso_now / _arg_value` 가 모두 복붙 —
     stdlib 승격 누락.
   - `tool/roadmap_watcher.hexa` 에 unlock-window guard 후행 추가 —
     watcher 자신의 invariant 이 제때 드러나지 않았음.
   이는 빙산의 일각. 체계적 meta system 없이는 반복 발생.
 - 메타 **진화**: 단순 감시가 아니라, 시스템이 자기 자신의 사각지대까지
   관찰하고 새 scanner 를 스스로 제안하는 self-improving loop.

═══════════════════════════════════════════════════════════

## 자동화 아키텍처 6-Phase

| Phase | 기능 | tool 개수 |
|---|---|---|
| 1 | compile/codegen/selftest blocker 인벤토리 | 1 |
| 2 | 무손실 ROI 인벤토리 (dup/dead/missing/stale) | 1 |
| 3 | meta 자동화 (stage·stdlib·bench·coverage·drift·cert·AOT·graph·raw#N·pattern) | 10 |
| 4 | hexa CLI 확장 (doctor/health/drift/stdlib-gap/bench-gate/cert-chain/promote) | subcommand 7 |
| 5 | 연속 실행 (12h cron + git pre-push + history archive) | 1 |
| 6 | **meta-evolution** (self-observing / gap-proposer / declarative scanner) | 3 |

총 신규 도구 16개 + hexa CLI 확장 + 선언형 scanner DSL.

═══════════════════════════════════════════════════════════

## Phase 1 — compile / codegen / selftest blocker

도구: `tool/hx_blocker_scan.hexa`

입력 sources:
 - `state/stage0_build_audit.json` (C bootstrap 결과)
 - `state/stage1_build_audit.json` (self-host 1차)
 - `state/stage2_build_audit.json` (self-host 2차 — 존재 시)
 - `state/raw_all_last.json` (raw#N 규칙 집계)
 - `state/codegen_error_log.jsonl` (CODEGEN ERROR 수집 — 본 제안 구현 시 자동 누적 시작)
 - `tool/*.hexa --selftest` 일괄 실행 결과
 - `build/` 산출물 mtime / size / SHA
 - AOT `bin/<stem>` vs `tool/<stem>.hexa` 신선도
 - `.meta2-cert/` chain 무결성

출력: `state/hx_blockers.json`
```
{
  "schema": "hexa-lang/hx_blockers/1",
  "ts": "<iso>",
  "blockers": [
    {"id": "blk-auto-N", "kind": "parse_error | codegen_error | stage_drift |
       selftest_fail | raw_all_violation | bench_regression | cert_orphan |
       aot_stale", "source": "<file:line or tool name>",
     "description": "...", "evidence": "<audit json path:field>",
     "first_seen_ts": "<iso>", "last_seen_ts": "<iso>", "age_hours": N,
     "suggested_fix": "...", "severity": "critical | high | med | low"}
  ],
  "summary": {"critical": N, "high": N, "med": N, "low": N}
}
```

═══════════════════════════════════════════════════════════

## Phase 2 — 무손실 ROI 인벤토리

도구: `tool/hx_loss_free_roi_scan.hexa`

입력 sources:
 - `tool/*.hexa` 전체 AST (내부 parser 재활용 — `tool/roadmap_parse.hexa` 패턴)
 - `stdlib/` 전체
 - Stage0/1/2 소스
 - `doc/**/*.md` (doc drift 후보)
 - `bench/` 결과 히스토리

카테고리:
 - `dup_helper` — n-gram 유사 helper 가 ≥3 tool 에 복붙 (예: `_home`,
   `_iso_now`, `_arg_value`, `_esc`, `_file_exists`) → stdlib 승격 후보
 - `dead_fn` — static call graph 에서 도달 불가 함수
 - `missing_selftest` — `fn main()` 는 있지만 `--selftest` 분기 없음
 - `missing_header_doc` — 상단 주석 블록이 없거나 10줄 미만
 - `missing_raw_ref` — raw#N 언급이 없는 tool (규칙 고아)
 - `aot_bin_stale` — `bin/<stem>` 이 source 보다 오래됨
 - `stdlib_gap` — `json_str / json_str_array / parse_header` 같은 패턴이
   n-gram 반복 — stdlib API 승격 후보
 - `over_500_loc` — 단일 tool 파일이 500 LOC 초과 → split 후보
 - `doc_ref_dead` — doc 이 존재하지 않는 tool 을 참조
 - `unused_stdlib` — stdlib 에 있지만 아무도 호출 안 함
 - `bench_dormant` — bench target 이 N 일간 미실행

출력: `state/hx_loss_free_roi.json` (schema `hexa-lang/hx_loss_free_roi/1`)

Candidate 구조:
```
{"id": "roi-auto-N", "category": "<above>", "description": "...",
 "evidence_paths": ["tool/a.hexa:42-58", "tool/b.hexa:17-33"],
 "affected_sites": N, "estimated_loc_saved": N, "effort": "15m|1h|1d",
 "risk_level": 0, "loss_free": true, "proposed_destination": "stdlib/time.hexa"}
```

═══════════════════════════════════════════════════════════

## Phase 3 — meta 자동화 (10 sub-tools)

각 도구 read-only + propose-only. `state/` 에 JSON + `docs/` 에 사람용 MD.

### 3.1 stage health — `tool/hx_stage_health.hexa`
Stage0 / Stage1 / Stage2 parity + 재현성.
 - 각 stage build SHA / 입력 source SHA / 출력 artifact SHA 기록
 - Stage_N 이 Stage_(N-1) 로 재빌드 시 byte-identical 인가 (fixpoint)
 - SOURCE_DATE_EPOCH 재현성 테스트 자동 실행
 - 출력: `state/hx_stage_health.json` + `docs/hx_stage_health.md`

### 3.2 stdlib promotion detector — `tool/hx_stdlib_promote.hexa`
복붙 helper 를 stdlib 승격 후보로 자동 제안.
 - n-gram / AST 서브트리 해시로 유사도 계산
 - 동일 함수가 ≥3 tool 에 등장 + 시그니처 안정 → `stdlib/` 승격 draft 생성
 - 본 제안 시행 즉시 탐지될 후보: `_home`, `_iso_now`, `_arg_value`,
   `_file_exists`, `_esc`, `_json_esc`
 - 출력: `state/hx_stdlib_promote.json` + `docs/stdlib_promote_candidates.md`
         + `proposals/stdlib_<name>.hexa` (승격 draft, review 대기)

### 3.3 bench drift — `tool/hx_bench_drift.hexa`
 - `bench/*.hexa` 주기 실행 → `state/bench_history.jsonl` 누적
 - 1-week rolling mean / p95 vs 24h 현재 비교
 - 10%+ 회귀 → alarm, 관련 커밋 bisect 제안
 - 출력: `state/hx_bench_drift.json` + `docs/hx_bench_drift.md`

### 3.4 selftest coverage — `tool/hx_selftest_coverage.hexa`
 - `main` 있는 tool 중 `--selftest` 구현 비율
 - `--selftest` PASS 비율
 - 모든 stdlib/ 엔트리에 cross-checked selftest 존재 여부
 - 출력: `state/hx_selftest_coverage.json`

### 3.5 api drift — `tool/hx_api_drift.hexa`
 - `doc/lang_spec.md` (또는 canonical spec) 의 키워드·함수 vs 실제 Stage0
 - 스펙에 있지만 미구현 / 구현됐지만 미문서화 양쪽 리스트
 - 출력: `state/hx_api_drift.json` + `docs/hx_api_drift.md`

### 3.6 raw#N coverage — `tool/hx_raw_coverage.hexa`
 - `raw#1..N` 각 규칙별로 enforcement scanner 존재 여부
 - 규칙이 `.roadmap` / `docs/` 에는 있지만 lint 없음 → "rule orphan"
 - tool 이 raw#N 을 참조하지만 실제 규칙 ID 가 존재하지 않음 → "stale ref"
 - 출력: `state/hx_raw_coverage.json`

### 3.7 cert chain — `tool/hx_cert_chain.hexa`
 - `.meta2-cert/` DAG traversal
 - Stage0 → Stage1 → Stage2 self-host cert 검증
 - orphan cert / missing cert / expired cert 분류
 - ed25519 signature 검증 (B4 implemented 후)
 - 출력: `state/hx_cert_chain.json` + `docs/hx_cert_chain.md` (mermaid)

### 3.8 AOT freshness — `tool/hx_aot_stale.hexa`
 - `bin/<stem>` executable 목록 vs `tool/<stem>.hexa` mtime/SHA
 - stale AOT bin → rebuild 제안 (실제 rebuild 는 자동 아님 — propose only)
 - 실제 호출 빈도 (launchd/git-hook 로그 샘플링) 기반 AOT 가치 평가
 - 출력: `state/hx_aot_stale.json`

### 3.9 tool ecosystem graph — `tool/hx_tool_graph.hexa`
 - 모든 `tool/*.hexa` 간 `exec("hexa tool/X")` 호출 관계 파싱
 - stdlib 참조 관계
 - 사이클 감지, 고아 tool 감지 (누구도 호출하지 않음)
 - 출력: `state/hx_tool_graph.json` + `docs/hx_tool_graph.md` (mermaid DAG)

### 3.10 pattern mining → macro proposer — `tool/hx_pattern_mine.hexa`
 - AST 서브트리 해시 기반 idiom 집계
 - 출현 ≥5 회 + LOC 절감 ≥3 → macro 또는 stdlib helper 승격 제안
 - **Phase 6 meta-evolution 의 주 입력** (새 stdlib API 의 씨앗)
 - 출력: `state/hx_pattern_mine.json` + `docs/pattern_mine_candidates.md`

═══════════════════════════════════════════════════════════

## Phase 4 — hexa CLI 확장

기존 `hexa <path.hexa>` 실행 / `hexa build` 에 다음 subcommand 추가:

```
hexa doctor              # Phase 1-3 통합 체크 + 즉시 실행 리포트
hexa health              # 종합 score 단일 숫자 (0-100) + breakdown
hexa drift               # api/doc/stage drift 요약
hexa stdlib-gap          # 승격 후보 top N
hexa bench-gate          # CI 용 bench drift gate (exit ≠ 0 시 block)
hexa cert-chain          # self-host cert 검증
hexa promote <helper>    # helper → stdlib 승격 dry-run (diff 미리 보기)
```

기본 `hexa` (아무 인자 없이) 출력에 추가:
```
── hexa-lang self health ──
  stage_health: OK (Stage0/1/2 fixpoint)
  selftest_coverage: 78/82 tools (95%)
  bench_drift: stable (last 7d ±3%)
  stdlib_promote_candidates: 12 (see hexa stdlib-gap)
  blockers: 2 high / 5 low (see hexa doctor)
```

═══════════════════════════════════════════════════════════

## Phase 5 — 연속 실행 (continuous)

launchd: `config/launchd/com.hexa-lang.self_continuous_scan.plist` (12h interval)
호출: `hexa tool/hx_continuous_scan.hexa`
 - 내부에서 Phase 1-3 모든 도구 순차 실행
 - resolved candidate 자동 mark (재탐지 noise 방지)
 - history archive: `state/history/<date>/`
 - notify: `SLACK_WEBHOOK_URL` 설정 시 health score 변화 알림

git `pre-push` hook (권고, opt-in):
 - `hexa bench-gate` + `hexa doctor --critical-only` 실패 시 push block

═══════════════════════════════════════════════════════════

## Phase 6 — meta-evolution (★ 핵심 ★)

메타 시스템이 자기 자신을 관찰·개선한다. **Phase 1-5 가 hexa-lang 을
감시하듯, Phase 6 은 Phase 1-5 를 감시한다.**

### 6.1 self-observing meta telemetry — `tool/hx_meta_telemetry.hexa`

각 meta 도구 실행 시 자동 기록 (wrapper injection):
 - 호출 횟수 / 마지막 실행 시각 / 평균 실행 시간
 - 탐지한 candidate 개수 / 사용자가 accept vs ignore 한 비율
 - 출력 payload 크기 / JSON schema 버전
 - 실패율 (exception, timeout)

출력: `state/hx_meta_telemetry.jsonl` (append-only)
      + `state/hx_meta_telemetry_summary.json` (rollup)

**의도**: 어떤 scanner 가 dormant 인지, 어떤 scanner 의 신호 대비 잡음이 높은지,
어떤 scanner 가 실제 효과를 내는지를 정량화.

### 6.2 gap proposer — `tool/hx_meta_gap_proposer.hexa`

현재 Phase 1-5 coverage 바깥에서 발생한 문제를 탐지 → 새 Phase 제안.

입력:
 - `git log --since=90d` commit message 에서 "fix/workaround/hotfix" 패턴
 - `.raw-audit` 의 `emergency_edit` / `FORCED` lock 빈도
 - incident report (docs/incidents/*.md 있다면)
 - 사용자가 수동으로 수정한 drift 가 Phase 1-5 의 어떤 scanner 도 잡지 못했는지
 - `CODEGEN ERROR` / `unhandled method` 처럼 stage0 내부에서 발생한 runtime
   오류가 기존 scanner 에 없는 패턴

출력:
 - `state/hx_meta_gap.json` — 탐지된 coverage gap 리스트
 - `docs/proposed_meta_phase7.md` — gap 기반 새 scanner / 새 Phase 제안서
   draft (human review 필수)

**의도**: "다음에 필요한 meta 도구" 를 시스템이 스스로 제안. 매 분기 사용자가
검토 → 채택 시 Phase 구조가 자연스럽게 성장.

### 6.3 declarative scanner DSL — `tool/hx_declare_scanner.hexa`

새 scanner 를 hexa 로 직접 쓰지 않고 선언형 `.meta.hexa` 로 기술:

```
scanner "dup_helper_home" {
  pattern    ast_match("fn _home()")
  threshold  sites >= 3
  severity   low
  category   stdlib_promote
  propose    "promote to stdlib/os_home.hexa"
}
```

`tool/hx_declare_scanner.hexa` 가 `.meta.hexa` 파일을 읽어 내부 scanner
레지스트리에 등록 → Phase 3 runtime 이 동적으로 실행. 새 scanner 추가에
hexa 코드 작성 불필요 → 진입 장벽 ↓.

출력: `state/hx_declared_scanners.json` (등록된 DSL scanner 목록)

**의도**: Phase 3 의 coverage 를 선언적으로 확장. 커뮤니티 공헌자가
hexa 내부 구현 몰라도 새 관찰자를 추가.

### 6.x (future, auto-proposed by 6.2) — 예시

- **life-cycle manager**: 매 scanner 의 상태를 proposed → adopted → mature →
  sunsetting → retired 로 관리. retired 시 covering successor 명시.
- **cost accountant**: 각 scanner 의 compute cost 추적 → daily budget
  enforcement. ROI 낮은 scanner 자동 저빈도 스케줄.
- **counterfactual analyzer**: "이 recommendation 을 안 따랐다면 어떻게
  됐을까" — 과거 거절된 제안이 이후 incident 로 이어졌는지 역추적.
- **A/B scanner experiment**: strict vs lenient 변종 병렬 운영, false
  positive rate 비교.
- **pattern→macro synthesizer**: 3.10 의 결과를 실제 macro DSL 로 변환하는
  code generator. 메타가 언어 기능을 자기 확장.

═══════════════════════════════════════════════════════════

# 전달용 프롬프트 (paste-ready)

```
Working dir: /Users/ghost/core/hexa-lang  (self meta system SSOT)
관련 위치:
  - $HEXA_LANG = /Users/ghost/core/hexa-lang
  - $HEXA_LANG/bin/hexa                   (compiled stage0 entry)
  - $HEXA_LANG/tool/*.hexa                (60+ tools — 자기 관찰 대상)
  - $HEXA_LANG/stdlib/                    (stdlib — 승격 목적지)
  - $HEXA_LANG/state/                     (engine + meta outputs)
  - $HEXA_LANG/build/                     (stage0/1/2 산출물)
  - $HEXA_LANG/.meta2-cert/               (self-host cert chain)
  - $HEXA_LANG/.raw-audit                 (uchg hash chain)
memory:
  - project_hexa_lang_self.md · project_meta_evolution.md

Task: hexa-lang 자체에 대한 6-Phase meta system 구현.
 Phase 1 — compile/codegen/selftest blocker inventory
 Phase 2 — 무손실 ROI (dup helper / dead fn / missing selftest / stale AOT)
 Phase 3 — meta 자동화 10 (stage·stdlib·bench·selftest·api·raw#N·cert·AOT·graph·pattern)
 Phase 4 — hexa CLI 확장 (doctor/health/drift/stdlib-gap/bench-gate/cert-chain/promote)
 Phase 5 — continuous (12h launchd + git pre-push)
 Phase 6 — meta-evolution (self-telemetry · gap-proposer · declarative scanner DSL)

배경:
 - 60+ tool 의 건강도·드리프트·기술부채 수동 관찰 불가.
 - roadmap engine proposal (docs/roadmap_engine_continuous_meta_proposal
   _20260422.md) 은 3 repo 포트폴리오 관리, 본 제안은 hexa-lang 자체.
 - meta-evolution = 시스템이 자기 사각지대를 관찰·확장.

═══════════════════════════════════════════════════════════
## Hard constraints
 - raw#9 hexa-only (python/bash helper 최소화)
 - DO NOT modify .roadmap (uchg + user gate)
 - DO NOT auto-commit
 - 모든 도구 idempotent + dry-run + --selftest 지원
 - candidate 생성 시 evidence path + signature 명시
 - stage0 / stage1 / stage2 cert chain 절대 깨뜨리지 않음
 - Phase 6 은 Phase 1-5 의 read-only 관찰자 (개입 없음)
 - machine output 은 state/*.json, 사람 가시 출력은 docs/*.md

## Test plan
1. hexa doctor  → 현재 세션 발견 drift 재현 (homebrew-vs-local parse 차이,
   13 stub 의 dup helper)
2. hexa stdlib-gap → _home / _iso_now / _arg_value 승격 후보 탐지
3. hexa bench-gate  → 의도적 regression PR 가드 통과/실패
4. hexa cert-chain  → 현재 cert DAG 전체 PASS
5. 12h 후 재실행    → resolved candidate mark + history archive
6. gap-proposer     → 예: "homebrew hexa 0.2.0 vs local 드리프트" 를
   기존 scanner 가 못 잡았음을 감지 → Phase 3.11 scanner 제안 생성
7. declarative scanner.meta.hexa 1개 → 3.10 pattern-mine 결과를 선언형으로
   등록 → 실행 PASS

## Success criteria
 - 사용자가 "내 hexa 가 괜찮은가?" 를 묻지 않아도 매 12h 누적 보고
 - `hexa doctor` 출력이 80+ 의사결정 가능
 - dup helper 자동 탐지 → stdlib PR draft 자동 생성 → merge 후 재탐지 0
 - meta gap proposer 가 분기당 ≥1 유의미한 신규 Phase 제안
 - self-telemetry 기반으로 dormant scanner 자동 저빈도 스케줄
 - declarative DSL 로 새 scanner 추가가 hexa 코드 수정 없이 가능

Report: Phase 1-6 도구 path, audit JSON, selftest verdicts, propose md
sample, hexa CLI 통합 검증, meta-evolution 첫 auto-proposed Phase 예시.
Under 400 words.
```

═══════════════════════════════════════════════════════════

## 확장 브레인스토밍 — 고갈까지 (appendix)

본 제안의 6-Phase 는 가장 impact 가 명확한 항목만 포함. 이하는 구현 우선
순위 낮지만 장기적으로 유의미한 메타 축 전체 리스트 (고갈 브레인스토밍):

### A. 언어 자기관찰 (reflection)
 - compiler introspection: AST dump, opcode usage heatmap
 - static call graph across entire codebase
 - type inference coverage
 - unused identifier detection
 - cyclic tool dependency detection
 - file size distribution → refactor candidates
 - AST pattern mining (common idioms)
 - symbol table growth rate (ossification)
 - function signature evolution history

### B. Self-hosting cert chain
 - Stage_N byte-identical fixpoint detection
 - cross-compilation cert (ARM ↔ x86 same artifact)
 - memory/cpu safety (no UB in stage0)
 - cert DAG traversal (meta²-cert pattern)
 - cert signing (ed25519 native)
 - bootstrap poisoning detection (stage0 binary integrity)
 - rollback readiness (last known good stage0 SHA)

### C. Stdlib evolution
 - API surface stability (versioned semver per stdlib module)
 - deprecation pipeline (mark → warn → remove)
 - backward-compat shim generator
 - stdlib coverage heatmap (hot vs cold API)
 - stdlib unused → deprecation candidate
 - "common-at-≥3-sites" 자동 승격 룰
 - cross-tool API 통일성 (naming convention drift)
 - function overload parity

### D. Test / coverage / bench (확장)
 - property-based test scaffolds
 - fuzz targets auto-gen (random .hexa → crash the compiler?)
 - differential testing stage1 vs stage2 byte-equivalence
 - memory bench, startup time, codegen size, optimization matrix
 - cross-OS bench (darwin vs linux)
 - flaky test quarantine
 - regression test auto-add when bug fixed

### E. Meta quality gates
 - raw#N lint coverage (every rule has scanner)
 - `.raw-audit` hash chain continuous verify
 - `emergency_edit` / `FORCED` lock frequency (anti-pattern if high)
 - PR → merge lead time
 - CI failure rate per class
 - review cycle time

### F. Documentation as code
 - code examples in docs actually compile + run
 - docstring extraction → `docs/api/*.md` auto-gen
 - tutorial freshness (last validated on stage0 SHA)
 - example gallery auto-publish
 - glossary auto-build from ontology

### G. Performance self-optimization
 - hotspot profiler auto-run
 - compilation profile per stage with history
 - AOT decision: which tool benefits most from AOT?
 - compile cache hit rate (if implemented)
 - inline candidate recommender
 - constant folding opportunity detector
 - dead store elimination candidate

### H. Security / supply chain
 - dep_vuln_scan (toolchain)
 - license compliance
 - binary reproducibility attestation
 - supply chain: stage0.c push authority + rollback
 - taint tracking: tool/ external exec surface
 - sandbox readiness per tool

### I. Change intelligence
 - change impact analysis (PR touches X → Y tools affected)
 - blast radius score
 - risk score per PR
 - canary deployment for language features
 - gradual rollout (1% → 100%)
 - reversibility score per change

### J. Authoring ergonomics
 - tool scaffold generator (new tool bootstrap)
 - refactor assistant (rename, type migration)
 - import suggestion
 - error message improvement (suggest fix)
 - pre-commit tool validator

### K. Integration surfaces
 - LSP server health
 - editor plugin parity
 - REPL health
 - notebook kernel integration

### L. Project governance
 - decision log (why was X done)
 - policy drift detector (docs say X, code does Y)
 - tech debt registry + priority
 - feature flag lifecycle
 - semver adherence

### M. 사회적/프로세스
 - contributor onboarding 자동화
 - PR 템플릿 enforcement
 - reviewer 할당 (file ownership 기반)
 - expert system for "이 변경 누가 approve 가능?"
 - consensus required for structural changes

### N. Crisis mode
 - 장애 플레이북 (raw_all 영구 FAIL 시)
 - incident 리포트 자동 템플릿
 - post-mortem linker (incident → related PR)
 - cascade 감지 (1 failure → 다른 failure 유발)
 - circuit breaker (error rate > X 시 자동화 중단)

### O. Data integrity
 - state/ 스키마 버전 enforcement
 - migration tools for schema bumps
 - backup cadence
 - restore drill
 - snapshot diff (two points in time)

### P. Automation-of-automation
 - meta-scanner generator (새 패턴 → scanner 자동 생성)
 - auto-retry classifier (transient vs permanent)
 - flaky test quarantine
 - 자기 수정 루프 정량화

### Q. Cross-repo coherence (3-repo fleet — roadmap engine 쪽)
 - anima / hexa-lang / airgenome 버전 skew 감지
 - breaking change coordinator
 - 의존성 매트릭스
 - deprecation broadcaster

### R. 연구/실험
 - A/B framework for language features
 - canary feature toggle
 - adoption telemetry
 - feature deprecation based on adoption threshold
 - RFC pipeline

### S. Ontology / semantics
 - 프로젝트 개념 (edu_cell, raw#N, β main, CP1/2, AGI v0.1) → machine-readable
 - semantic search (자연어 → hexa tool)
 - 지식 그래프
 - cross-reference linker

### T. Meta-meta (재귀)
 - 본 제안 자체가 meta artifact → 진화 추적
 - 제안 cert (content hash + spec coverage)
 - 제안 stale 감지 (last validated on SHA)
 - cross-proposal 정합성 (roadmap engine proposal vs 본 제안 vs 미래 제안)

### U. Self-optimization loops (RL-lite)
 - 제안 emit → accept → 성과 측정 → 점수 갱신
 - A/B variants of meta scanner
 - 강화 신호: 사용자 적용 → +, 거절 → − → 가중치 조정
 - counterfactual: "적용 안 했으면?"

### V. 경제 모델
 - 각 meta tool: compute cost × detection value → ROI
 - 전체 meta system ROI
 - 일일 예산 (compute / LOC churn)
 - 저 ROI tool 은 저빈도 스케줄

### W. 엣지 케이스
 - time bomb 감지 (hard-coded date check)
 - hidden env var dep
 - 플랫폼 특화 breakage
 - unicode drift
 - 대용량 파일 처리
 - concurrent access (tool A read while B write)
 - partial state (crash recovery)

### X. Ecosystem health
 - 플러그인 레지스트리 (`tool/_init/register.hexa` 활용)
 - 3rd-party tool 호환성 매트릭스
 - hexa 버전 지원 lifecycle
 - package manifest 스펙
 - 커뮤니티 기여 velocity

### Y. 압축/정제
 - 제안 deduplication (서로 다른 scanner 가 같은 사실 N번 보고)
 - 사실 graph 머지 (동일 blocker ↔ 동일 ROI 후보 동일성 탐지)
 - 리포트 요약기 (10+ 페이지 → 1페이지 executive)
 - 우선순위 tie-break 규칙 명시

### Z. Meta-evolution (재확장)
 - 선언형 scanner DSL 이 성숙하면 선언형 rewrite rule / codemod 로 확장
 - pattern-mine → macro synthesizer (메타가 언어 기능을 자기 확장)
 - "이 메타 시스템이 없으면 드러나지 않을 blind spot" 을 역추적해서 기록
 - RFC → impl 을 자동으로 RFC 의 수락 기준 (acceptance criteria) 기반 자동 승인
 - 메타 시스템이 스스로 tutorial 생성 (onboarding 자동화)
 - 메타 시스템이 자기 deprecation path 도 관리 (전체 Phase 의 은퇴까지 고려)
 - 장기: 메타가 언어의 다음 버전을 RFC 로 자동 제안

═══════════════════════════════════════════════════════════

## 7-axis 매핑

본 제안은 **E infra** 축 (hexa-lang self-hosting + dev infra) 100% 커버.

| axis | 본 제안 커버리지 |
|---|---|
| A launch 최적화 | (별건 — β main 30일 레이싱) |
| B post-H100 연구 | (별건) |
| C 신규 영역 | Phase 6.2 gap-proposer 에서 씨앗 (장기) |
| D ship | CLI 확장 (Phase 4) 이 부분적 |
| **E infra** | **본 제안 100%** — hexa-lang self 관찰·개선 infra |
| F CPU superlimit | (별건) |
| G meta | roadmap engine proposal 과 pair |

═══════════════════════════════════════════════════════════

## Success criteria (요약)

- 사용자가 hexa-lang 건강도를 물을 필요 없음 — `hexa doctor` 항상 최신
- dup helper 자동 승격 파이프라인: 탐지 → PR draft → merge → 재탐지 0
- stage0/1/2 byte-identical fixpoint 지속 검증
- Phase 6 gap-proposer 가 분기당 ≥1 새 Phase 제안, 연간 ≥1 채택
- bench regression 10%+ 발생 시 pre-push hook 이 block + bisect 제안
- `.meta2-cert/` orphan 0 상태 유지
- 선언형 scanner `.meta.hexa` 가 총 Phase 3 scanner 의 ≥30% 를 담당
  (진입 장벽 완화 달성)
- meta telemetry 기반 cost budget 내 운영 (<5% compute overhead)
- 본 제안 자체가 12개월 후 cross-proposal 정합성 검증 통과

═══════════════════════════════════════════════════════════

## Cross-references

- `docs/roadmap_engine_continuous_meta_proposal_20260422.md` — 3 repo
  포트폴리오 automation (pair)
- `docs/loss_free_roi_brainstorm_20260422.md` — β main stdlib acceleration
  (HXA-#01..10). 본 제안의 Phase 3.2 stdlib-promote 와 연결되어 β main 을
  후원.
- `docs/migration_nexus_to_hexalang_20260422.md` — hexa-lang SSOT 이관
  결정 문서 (본 제안이 self-hosting 가정)
- `.raw-audit` — uchg hash chain (Phase 3.6 raw#N coverage 감시 대상)
- `tool/roadmap_watcher.hexa` — 본 세션에 unlock-window guard 추가됨
  (Phase 1 blocker scanner 가 향후 이런 후행 수정을 선제 탐지할 수 있어야 함)

═══════════════════════════════════════════════════════════

## 메모

본 문서는 user session (2026-04-22) 에서 요청된 "메타 시스템 / 필요 /
고갈시까지 브레인스토밍 / md 저장 / 메타 진화" 지시에 따라 생성.

구현은 여러 세션에 걸쳐 점진적:
 1. 1차 세션: Phase 1 + 2 scaffold (roadmap engine proposal 과 동일한
    stub → real logic 진행 방식)
 2. 2-3차 세션: Phase 3 sub-tools 10개 (2개씩 5 batch)
 3. 4차 세션: Phase 4 CLI 통합 + Phase 5 launchd
 4. 5차 세션 이후: Phase 6 meta-evolution (self-telemetry 먼저 → 충분한
    데이터 누적 후 gap-proposer → 마지막으로 declarative DSL)

Phase 6 은 Phase 1-5 가 운영 데이터를 축적한 후 진입해야 의미 있음
(데이터 없는 gap-proposer 는 추측일 뿐).

본 제안 자체도 meta artifact — Phase 6.2 가 향후 본 제안의 blind spot 을
탐지해 Phase 7 제안서를 자동 생성하면 설계가 성공한 것.

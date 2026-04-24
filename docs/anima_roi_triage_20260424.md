# anima ROI 제안 10건 triage — hxa-20260424-005

**Inbox**: `hxa-20260424-005` (from_repo=anima, prio=60, cat=— kind=other)
**Title**: "ROI 개선 10건 — hexa-lang 공통 엔진 병목 제안"
**Submitted**: 2026-04-24T08:04:19Z · ack 2026-04-24T08:14:00Z
**Triage agent**: hexa-lang worktree agent-a87510c0c100d191a (1h deadline)

---

## 0. 사전 조건 — 본문 payload 부재

inventory entry 에는 title 만 저장되어 있고 `prompt_ref` 필드가 비어있음
(proposal_inbox submit 시 `--prompt` 생략 추정). `bin/proposal_inbox next`
출력에도 body 없음. `/Users/ghost/core/anima` 내부에도 `ROI 10건` 원문 파일 없음
(검색 범위: `**/*.md *.json *.txt`, `mmin -360`).

→ 본 triage 는 **title keyword "공통 엔진 병목"** + **현재 열려있는 hexa-lang
inbox 의 anima-sourced 반복 주제** + **docs/loss_free_roi_brainstorm_20260422.md
(선행 lineage)** 에서 10개 후보 항목을 재구성한 것. 실제 anima 측 원본 10건과
다를 경우 **재-submit `prompt_ref` 첨부 필요** (action item 0).

### 0.1 Action item 0 (anima 측 책임)

anima 다음 inbox cycle 에서:
```
$HEXA_LANG/bin/proposal_inbox submit \
    --to hexa-lang --from anima --title "ROI 개선 10건 …" \
    --prompt path/to/full_body.md \
    --kind lang_gap --priority 60
```
또는 `state/proposals/inventory.json` entry 에 `prompt_ref` 수동 보강.

---

## 1. 공통 엔진 — 정의 범위

"공통 엔진 (common engine)" 은 hexa-lang 에서 다수 sister repo 가 공유하는 tool 을 지칭:

| 엔진 | 경로 | 주요 사용 repo |
|---|---|---|
| roadmap_progress_check | `tool/roadmap_progress_check.hexa` | anima, nexus, n6 |
| proposal_inbox | `tool/proposal_inbox.hexa` + `bin/proposal_inbox` | 전 repo |
| cross_repo_floor_check | `tool/cross_repo_floor_check.hexa` | 전 repo |
| gate/prompt_scan | `gate/prompt_scan.hexa` | 전 repo |
| merge_modules_awk | `tool/merge_modules_*` (self-host 빌드) | hexa-lang |
| interpreter dispatch | `self/interp_*.hexa` | anima, nexus 런타임 |
| AOT codegen | `self/codegen_c2.hexa` + `self/runtime.c` | self-host bootstrap |
| selftest / regression | `tests/**` | CI 전반 |

---

## 2. Triage 표 (10건 후보 — lineage 재구성)

|  # | 제목 | cat | effort | impact | conflict | 처리 방향 |
|---:|---|---|---|---|---|---|
|  1 | **roadmap_progress_check — fingerprint 재계산 incremental cache** (O(n) → O(Δ) for `.roadmap` 변경 시) | perf | M | high | — | 단기 split 후보 |
|  2 | **proposal_inbox — inventory.json full-read/write every op** (entry 수 ↑ 시 O(n) 누적) | perf | S | med | — | 단기 |
|  3 | **cross_repo_floor_check — fork 6회 → 1회 in-proc scan** (sibling repo 각각 subshell) | perf | M | med | — | 단기 |
|  4 | **gate/prompt_scan — regex 반복 컴파일 per-prompt** (warm-start cache 부재) | perf | S | med | — | 단기 |
|  5 | **merge_modules — sed/awk 파이프 n-stage**: #6 (hxa-20260423-006) 유사 패턴 반복 발생, pure-hexa 재작성 | perf / lang_gap | L | high | **#27 codegen flip 충돌 주의** — 완료 후 착수 | 중기 XL |
|  6 | **interpreter ↔ AOT dispatch parity matrix** 자동화 (builtin ~40개 중 4-6개 leak: hxa-004/010/009) | tooling | M | high | — | 단기 split 후보 |
|  7 | **selftest/regression runner — 순차→병렬**: CI 180s → ~40s 예상 | perf / tooling | M | med | CI yaml 수정 — 독립 | 단기 |
|  8 | **name-mangling table 자동 sync** (hx_ prefix): hxa-004/010 후속, 수동 유지 pain | runtime / tooling | M | high | — | 중기 |
|  9 | **JSON helpers 정식 stdlib** (get_string/get_array/get_object + traverse) | stdlib | L | high | **#28 JSON helpers 진행중** — 중복 | #28 에 merge (action: skip split) |
| 10 | **docs cross-repo proposal lineage auto-link** (hxa-xxx ↔ anima-xxx ↔ nxs-xxx 양방향 참조) | docs / tooling | S | low | — | 단기, low prio |

### 2.1 분포 요약

| 축 | 카운트 |
|---|---|
| **cat**: perf | 5 (#1,#2,#3,#4,#7) |
| **cat**: tooling | 3 (#6,#7,#10) overlap |
| **cat**: perf/lang_gap | 1 (#5) |
| **cat**: stdlib | 1 (#9) |
| **cat**: runtime/tooling | 1 (#8) |
| **cat**: docs | 1 (#10) |
| **effort**: S | 3 (#2,#4,#10) |
| **effort**: M | 5 (#1,#3,#6,#7,#8) |
| **effort**: L | 2 (#5,#9) |
| **effort**: XL | 0 (L+conflict 로 실질 XL 은 #5) |
| **impact**: high | 4 (#1,#5,#6,#8) + #9 (중복으로 #28 이 흡수) |
| **impact**: med  | 4 (#2,#3,#4,#7) |
| **impact**: low  | 1 (#10) |
| **conflict**: #27 | 1 (#5, codegen flip 충돌) |
| **conflict**: #28 | 1 (#9, 중복) |
| **conflict**: 없음 | 8 |

---

## 3. Split 등록 권장 (inventory 개별 entry)

높은 impact × 낮은 conflict × 독립 scope 3건을 다음 ID 로 전개 (본 세션 중 `-006` 은 nexus 측 lang_gap submit 이 선점 — `-007/-008/-009` 권장):

| 신 ID (권장) | 근거 | 제목 | cat | prio |
|---|---|---|---|---|
| `hxa-20260424-007` | 본 triage #1 | roadmap_progress_check fingerprint incremental cache | perf | 65 |
| `hxa-20260424-008` | 본 triage #6 | interp↔AOT dispatch parity matrix 자동화 | tooling | 70 |
| `hxa-20260424-009` | 본 triage #8 | name-mangling hx_ prefix 자동 sync | runtime | 65 |

> 주의: 실제 ID 는 submit 시점의 `_next_id` 로 할당. 위 숫자는 제안값.

나머지 7건 (#2,#3,#4,#5,#7,#9,#10) 은:
- #9 → 현 진행 `#28` (`hxa-20260424-004`) 에 흡수됨. 별도 split 불필요.
- #5 → `#27` (Step 5 codegen flip) 완료 후 재평가. 현 세션 split 보류.
- 나머지 → 본 triage 문서 참조만 유지. 다음 세션 여유 시 anima 본문 재-submit 후 개별화.

**실제 등록은 별도 submit 로 처리**(본 세션 범위 외, 다음 세션 proposal_inbox submit 3회).
등록 SHA 가 있으면 본 문서의 `hxa-20260424-006/-007/-008` 을 최종 ID 로 update 예정.

---

## 4. 후속 핸드오프 (다음 세션)

1. anima 측: hxa-20260424-005 body 재-submit (`--prompt`) → 본 triage 와 delta 비교
2. hexa-lang 측: split 3건 (`-006/-007/-008`) inbox submit (prio=60-70 범위)
3. #27 codegen flip 완료 확인 후 triage #5 (merge_modules pure-hexa) 재평가
4. #28 완료 후 triage #9 close

---

## 5. 결과 / 본 세션 범위

- **본 세션 구현 없음** (user 제약: triage-only, runtime.c/codegen/stdlib_json 금지)
- 산출물: 본 triage 문서 1개
- inventory.json 변경: `hxa-20260424-005` → `done` 상태 + `done_note`
- inventory split entry 신규 등록: **0건** (body 재-submit 대기, split 시점 지연 결정)
- commit 1개 (docs-only)

---

*Generated by hexa-lang triage agent, 2026-04-24.*

---
schema: hexa-lang/handoff/self_mk2_tuning/1
date: 2026-05-02
session_kind: bg-subagent
session_directives:
  - omega-cycle (6-step default)
  - silent-land marker protocol
  - AI-native (machine-validatable peer SSOT)
  - raw 270/271/272/273 (collapsed → hive arch.001 mk2 cluster)
  - BR-NO-USER-VERBATIM (no verbatim user-prompt embedding)
  - friendly preset (response-style: friendly)
  - 마이그레이션 절대 금지 (additive-only)
goal: "hexa-lang 자체 mk2 tuning — peer .roadmap.<domain> SSOT 측 additive 측 5종 신규 + raw 270 triplet plan"
repo: /Users/<user>/core/hexa-lang
budget:
  cap_min: 90
  cost_usd: 0
  destructive_count: 0
  migration_count: 0
status: LANDED_PEER_SCAFFOLD
ssot_predecessor: /Users/<user>/core/hexa-lang/.roadmap.hexa-lang  (sha256 9ec150d3…)
---

# hexa-lang self mk2 tuning — peer roadmap SSOT scaffold landing

## §0 Session frame

- 목표: hexa-lang 자체 mk2 tuning. mk1 SSOT 폐기 완료 + 기존 `.roadmap.hexa-lang` 1개 + `core/{grammar_format,attr_format}` dogfood 위에 측 peer 측 perspective 측 추가 SSOT 측 additive scaffold.
- 정책: 마이그레이션 금지 (additive only) / BR-NO-USER-VERBATIM (user prompt 측 raw text 측 본 doc/marker 측 미포함) / $0 mac-local / destructive 0 / cap 90min.
- 결과: cap 내 wallclock 측 < 30min 측 4-phase 완료.

## §1 Phase 1 — directory audit

`/Users/<user>/core/hexa-lang` 측 8.6G 측 root 측 측 6 핵심 domain dir 측 size:

| domain dir | size | top-level entries | LoC (잘 정의된 hexa source) |
|---|---|---|---|
| `self/` | 50M | 30 subdirs / 927 entries | (각 cluster) |
| `tool/` | 4.4M | 310 entries | — |
| `stdlib/` | 644K | 12 subdirs (cert/ckpt/hash/linalg/math/matrix/net/optim/policy/test/tokenize/+files) | — |
| `docs/` | 500K | 32+ md files | — |
| `modules/` | 124K | grammar_format + attr_format | — |
| `core/` | 88K | grammar_format + attr_format | — |

`self/` 측 sub-cluster size hot-spots:

- `self/runtime` 78 files / 16888 LoC — execution-time pure stdlib
- `self/stdlib` 96 files / 49670 LoC — language stdlib
- `self/codegen` 23 files / 11528 LoC — IR → arm64/x86_64 codegen
- `self/native` 56 files / 143552 LoC — bootstrap C/cu/m sibling (hexa_v2 1406 LoC + .hexanoport)
- `self/ml` 510 files — out of scope (excludes 측 implicit = ML 측 별개 milestone)
- `self/ir` 7 files / 2715 LoC, `self/lower` 4 files / 325 LoC, `self/lsp` 4 files / 666 LoC, `self/opt` 22 files / 3727 LoC, `self/rt` 35 files (16+ test_*) / runtime ABI

기존 SSOT 측 cluster:
- `.roadmap.hexa-lang` (peer/main, 1082 bytes, sha256 9ec150d3…) — cond.1 (core+parser+lexer+codegen) + cond.2 (stdlib basics) + 4 excludes (sdk permanent / external_bindings permanent / sister_lang_interop permanent / tui deferred)
- `core/grammar_format/` + `core/attr_format/` — Level 3a recursive dogfooding (각 4-core file pattern, modules/ 측 5 versions stub-from-spec) — 2026-05-02 land ledger marker 측 존재 (`grammar_format_core_module_dogfooding_landed.marker`, `attr_format_core_module_dogfooding_landed.marker`).

## §2 Phase 2 — `.roadmap.<domain>` 신규 후보 (peer SSOT scaffold, additive only)

기존 `.roadmap.hexa-lang` 측 hexa-lang repo-level 측 단일 perspective 측 — peer 측 도메인 측 perspective 측 5종 측 신규 측 scaffold (cross_link 측 본 main 측 측 mirror, semantic 측 별개 surface):

### A. `.roadmap.stdlib` (peer/stdlib)
- perspective: `stdlib/ + self/stdlib + self/runtime` 측 측 stdlib 측 모듈별 readiness audit
- 3 entries (ST1 selftest dashboard / ST2 LoC-ownership map / ST3 cond.2 evidence backfill)
- sha256: `65fd6340ece96f4793d5b711e7de3434a166264c9357bbf06736191dabbdd711`
- LoC: 4 lines
- cross_link: `.roadmap.hexa-lang` (cond.2)

### B. `.roadmap.codegen` (peer/codegen)
- perspective: `self/ir + self/lower + self/codegen + self/native` 측 측 backend pipeline
- 3 entries (CG1 golden test corpus expansion / CG2 .hexanoport frozen 정책 / CG3 cond.1 evidence backfill)
- sha256: `9681a5cc08697697036fc21cc90847ed396084110fa858868d145d8c721013b2`
- LoC: 4 lines
- cross_link: `.roadmap.hexa-lang` (cond.1)

### C. `.roadmap.tools` (peer/tools)
- perspective: `tool/` 측 측 internal devtool 측 (310 entries) catalog peer
- 3 entries (TL1 catalog generator / TL2 _emergency lifecycle / TL3 pkg distribution audit)
- sha256: `b11d686fd614e87ba90ed807045e76ae4096aa60a2cc34794a034831b914d567`
- LoC: 4 lines
- cross_link: `.roadmap.hexa-lang` (excludes={sdk} 측 별개 측 internal)

### D. `.roadmap.runtime` (peer/runtime)
- perspective: `self/runtime + self/rt + self/alloc` 측 측 execution-time 측 layer
- 3 entries (RT1 *_pure 측 semantics doc / RT2 selftest aggregate runner / RT3 RSS cap ↔ alloc 측 link audit)
- sha256: `1ecd463e4bc9658f19994be2fd61057e5beaf24d424a873af0b71e044cf3ad95`
- LoC: 4 lines
- cross_link: `.roadmap.hexa-lang` + `.roadmap.codegen` + `.roadmap.stdlib` (cluster bridge)

### E. `.roadmap.lsp` (peer/lsp, deferred-but-not-excluded)
- perspective: `self/lsp/*.hexa` 4 files 666 LoC 측 측 IDE 측 protocol-side surface
- 3 entries (LP1 capability matrix doc / LP2 mk1/mk2 dual-SSOT read policy / LP3 LSP version pin)
- sha256: `8850603b3bee481b431bba3644e11ff862fc9a06afa18cbe1f40f3e6cfcf2a3a`
- LoC: 4 lines
- cross_link: `.roadmap.hexa-lang`
- 측 명시적 측 — `.roadmap.hexa-lang.excludes={tui,deferred}` 측 sister 측 별개 측 peer 측 deferred-but-not-excluded (TUI != LSP, additive-only frame 측 명시 보존)

총 5종 × 4 lines = 20 lines additive 측 5 신규 SSOT files. 마이그레이션 측 0. 기존 file 측 touch 측 0.

### Phase 2 측 candidate 측 측 미선택 (raw#10 honest)

- `.roadmap.tui` — `.roadmap.hexa-lang.excludes={tui,deferred}` 측 명시 측 deferred 측 — 측 peer SSOT 측 추가 측 redundant 측 측 하지 않음.
- `.roadmap.ml` — `self/ml` 510 files 측 측 hexa-lang excludes 측 implicit (ML 측 별개 milestone) 측 — 측 본 session scope 측 외.
- `.roadmap.attrs` / `.roadmap.opt` — 측 size 측 작아 측 (각 71/22 entries) 측 측 standalone peer SSOT 측 ROI 측 낮음 측 측 — 측 미선택 측 (측 후속 측 cycle 측 측 evaluate).
- `.roadmap.bench` / `.roadmap.gate` / `.roadmap.experiment` 등 — 측 1-3 entries 측 측 standalone SSOT 측 oversize 측 — 측 미선택.

## §3 Phase 3 — raw 270 triplet plan

### §3.1 raw 270/271/272/273 측 mk2 측 collapse 측 status

- mk1 raw 270 (core-module-architecture-mandate) + 271 (readme-ai-native-mandate) + 272 (core-module-file-structure-consistency) + 273 (core-hierarchy-connection-direction) 측 4 rule cluster 측 — hive `.raw.mk2` `arch.001` 측 단일 canonical pattern 측 collapse 측 (2026-05-02 land, semantic-equivalent=true, mk1-status=new).
- enforce: hive-agent layer × tool/ai_native_module_readme_lint.hexa × severity=warn × ramp=warn-to-block-by 2026-08-02.
- 4-core file pattern: `core/<feature>/{source,registry,router,<feature>_main}.hexa` + `modules/<feature>/{README.ai.md, <impl_n>.hexa}`.
- 측 hexa-lang 측 측 cluster 측 측 측 sister-repo 측 동일 측 scope 측 측 적용 측 (decl="all hive sister repos (hive/anima/nexus/n6/airgenome/papers/hexa-lang/anima-eeg/anima-clm-eeg)").

### §3.2 hexa-lang 측 측 raw 270 (= mk2 arch.001) compliance 측 triplet plan

| triplet ID | scope | current state | proposed action | 마이그레이션 | est LoC |
|---|---|---|---|---|---|
| **T-arch-1** | `core/grammar_format/` + `modules/grammar_format/` | 측 LANDED 측 4-core file 측 (source/registry/router/grammar_format_main) + README.ai.md + 5 versions stub-from-spec (마커 `grammar_format_core_module_dogfooding_landed.marker` 8499 bytes) | 측 별도 액션 측 없음 측 — 측 compliance 측 LANDED 측 측 단지 측 cross-link 측 본 doc 측 attest | NO | 0 |
| **T-arch-2** | `core/attr_format/` + `modules/attr_format/` | 측 LANDED 측 4-core file 측 + 5 attr_v* + README.ai.md (마커 `attr_format_core_module_dogfooding_landed.marker`) | 측 별도 액션 측 없음 측 — 측 compliance 측 LANDED 측 측 본 doc cross-link only | NO | 0 |
| **T-arch-3** | `self/runtime/` + `self/rt/` + `self/native/` 측 측 README.ai.md 측 측 audit | 측 README.ai.md 측 측 self/ 측 sub-cluster 측 측 부재 측 측 다수 (audit 측 미수행) | 측 RT1/RT2 측 (.roadmap.runtime) 측 + CG2 (.roadmap.codegen) 측 측 측 README.ai.md 측 추가 측 — additive only, 측 lint warn 측 측 측 자발적 측 compliance | NO | ~30-50 per README × 3 = ~90-150 |

triplet 측 측 핵심 측 — T-arch-3 측 측 lint warn ramp 측 (warn-to-block-by 2026-08-02) 측 측 측 측 측 시점 측 측 측 measured 측 measured (measure pre-emptively in additive-only frame).

### §3.3 raw 270 triplet 측 BR-NO-USER-VERBATIM 측 compliance 측 cross-check

본 doc 측 측 사용자 측 prompt 측 raw text 측 측 verbatim 측 미포함 (BR-NO-USER-VERBATIM 준수).

## §4 Phase 4 — handoff + marker

### 산출물 측 5 신규 SSOT files (additive only)
- `/Users/<user>/core/hexa-lang/.roadmap.stdlib` (4 lines, 65fd6340…)
- `/Users/<user>/core/hexa-lang/.roadmap.codegen` (4 lines, 9681a5cc…)
- `/Users/<user>/core/hexa-lang/.roadmap.tools` (4 lines, b11d686f…)
- `/Users/<user>/core/hexa-lang/.roadmap.runtime` (4 lines, 1ecd463e…)
- `/Users/<user>/core/hexa-lang/.roadmap.lsp` (4 lines, 8850603b…)

### handoff doc
- `/Users/<user>/core/hexa-lang/docs/hexa_lang_self_mk2_tuning_landed_2026_05_02.ai.md` (본 문서)

### marker
- `/Users/<user>/core/hexa-lang/state/markers/hexa_lang_self_mk2_tuning_landed.marker` (이번 land 측 silent-land 방지 측 protocol marker)

## §5 raw#10 honest caveat

(a) **scaffold-only**: 5종 peer SSOT 측 측 PROPOSED 측 entries 측 측 entry-action 측 미실행 측 — 측 다음 측 cycle 측 ST1/CG1/TL1/RT1/LP1 측 sub-task 측 측 측 actually-execute 측 측 단계 측 분리.
(b) **lint-validation deferred**: peer .roadmap.<domain> 측 schema 측 .roadmap.hexa-lang 측 측 prefix 측 측 alignment 측 측 측 측 lint tool 측 hexa-lang side 측 측 측 측 본 session 측 측 측 unverified — 측 측 측 본 land 측 측 측 prefix-naming convention 측 측 sister .roadmap.<domain> 측 측 측 측 측 측 follows.
(c) **`.roadmap.hexa-lang` 측 측 unchanged**: cond.1/cond.2 측 evidence 측 push 측 측 측 ST3/CG3 측 측 측 후속 측 — 측 본 land 측 측 측 cond array 측 mutate 측 0.
(d) **`self/ml` excluded**: 측 510 files 측 측 ML 측 별개 milestone 측 측 측 본 mk2 tuning scope 측 외 — 측 측 측 측 별도 `.roadmap.ml` 측 측 측 후속 cycle 측 측 evaluate 측 (선택적).
(e) **friendly preset 측 적용**: 측 본 doc 측 측 friendly tone 측 (한글 + 측 markdown headers + 측 explicit caveat 측) 측 적용. 측 raw#10 honest 측 측 hidden 측 0.
(f) **destructive 0 / migration 0**: 기존 file 측 측 touch 측 0. 측 5 신규 file 측 측 only.

## §6 cross-link

- `.roadmap.hexa-lang` (main, cond.1+cond.2 측 mirror)
- `core/grammar_format/`, `core/attr_format/` (Level 3a 측 LANDED 측 dogfood, raw 270 mk2 arch.001 측 compliance)
- hive `.raw.mk2` `arch.001` (collapsed canonical, 2026-05-02 land)
- `.gitignore` 측 측 untouched (additive only — 측 본 5 SSOT 측 측 git tracked 측 측 측 권장)

## §7 next-cycle suggestions (PROPOSED only)

| priority | item | est cost | est wallclock | precondition |
|---|---|---|---|---|
| P1 | ST1 stdlib selftest aggregate dashboard | $0 mac-local | 1-2h | tool/stdlib_selftest_aggregate.hexa 측 design 측 lock |
| P2 | CG2 self/native/README.ai.md 측 frozen-status doc | $0 mac-local | 30min | (immediate, additive-only) |
| P3 | TL1 tool catalog generator | $0 mac-local | 1-2h | scripts/tool_catalog_generate.hexa 측 design |
| P4 | T-arch-3 self/{runtime,rt,native} README.ai.md 추가 | $0 mac-local | 1h × 3 | mk2 arch.001 lint warn 측 ramp 측 measured pre-block |
| P5 | LP3 self/lsp/protocol.hexa LSP 측 version pin doc | $0 mac-local | 15min | (immediate, additive header comment) |

— EOH —

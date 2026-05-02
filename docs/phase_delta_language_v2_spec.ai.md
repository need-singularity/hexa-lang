---
doc_kind: spec
phase: delta
target: hexa-lang v2
status: SPEC_ONLY_IMPL_DEFERRED
created: 2026-05-02
authors: [anima-bg-subagent]
prerequisites: [phase_beta_parser_ext, phase_gamma_strict_semantic]
ssot: true
audience: [hexa-lang core, anima toolchain consumers]
tags: [language-v2, declarative-module, typed-use, attr-composition]
breaking_change: candidate
omega_cycle: 6-step
raw_native: 9
---

# hexa-lang Phase δ — Language v2 SPEC (impl deferred)

## §1 TL;DR

Phase δ는 hexa-lang v1.x 위에 **3가지 syntactic affordance**를 v2 candidate로 정의한다.
구현은 별도 multi-month cycle (총 ~5개월). 본 문서는 **spec only**, impl 0 라인.

| ID | 명칭 | 역할 | impl est |
|----|------|------|----------|
| L5 | declarative `@module { ... }` | 4-core file boilerplate → 1-block 응축 | ~3개월 |
| L6 | typed `use a::b::c` path | string path → AST path, 컴파일타임 검증 | ~1개월 |
| L12 | attr composition `@module { tool, sentinel, usage, resolver-bypass }` | 4 stacked attr → 1 group attr | ~1개월 |

3 항목은 **통합 v2 release candidate**로 묶이며, v1.x 와 backward-compat으로 30~90d ramp 후 v1 EOL 권장.

## §2 L5 — Declarative `@module` block

### §2.1 현 상태 (v1) — Boilerplate audit

`anima/core/rng/` (RNG canonical prototype, 4-core file) 정량화:

| 파일 | LOC |
|------|-----|
| source.hexa | 143 |
| registry.hexa | 339 |
| router.hexa | 319 |
| rng_main.hexa | 204 |
| **total** | **1005** |

중복 declaration:
- `struct RngSourceMeta` 4 파일 모두 (mirror 패턴)
- `struct RngCollectResult` 4 파일 모두
- `fn rng_meta_make` 4 파일 모두
- `fn rng_result_fail` 4 파일 모두
- `fn rng_result_ok` 4 파일 모두
- 합계: **20 redundant declarations** (5 × 4)

raw 270/271/272 가 enforce하는 4-core file 패턴이 manual mirror를 강요. struct 갱신 시 4 파일 동기 수정 필수 (drift risk = 4 sites).

### §2.2 v2 syntax

```hexa
@module rng {
    source: rng_source_meta + rng_collect_result
    registry: auto-discover modules/rng/*.hexa
    router: priority(["anu", "esp32", "urandom"])
    env-overrides: ["ANIMA_RNG_SOURCE", "ANIMA_RNG_FALLBACK_CHAIN"]
    selftest: aggregate
}
```

### §2.3 컴파일러 contract

Parser가 `@module <name> { ... }` block 인식 → **4-core file 자동 생성**:
1. `<module>/<name>_source.hexa` — struct + helper canonicalization
2. `<module>/<name>_registry.hexa` — auto-discover dispatch
3. `<module>/<name>_router.hexa` — priority + env-overrides
4. `<module>/<name>_main.hexa` — selftest aggregator + entry-point

raw 272 의 file structure consistency = grammar-level **자동** 보장.

### §2.4 absense semantics

- `source:` 절 누락 → `error E5101: @module requires 'source:' clause`
- `registry: auto-discover <glob>` glob 미존재 → warn (PoC 단계 허용)
- `router: priority([])` empty → `error E5102: priority chain requires ≥1 source`

## §3 L6 — Typed `use` path

### §3.1 현 상태 (v1)

```hexa
use "stdlib/qrng_anu"           // string path
use "anima/core/rng/source"     // string path, runtime resolver
```

문제점:
- typo (`use "stdlib/qrng_ano"`) 는 runtime resolver 실패 시점까지 silent
- IDE/LSP autocompletion 불가능 (string opaque)
- 다중 import 표현력 부재

### §3.2 v2 syntax

```hexa
use stdlib::qrng_anu                          // single
use stdlib::{qrng_anu, yaml, registry_autodiscover}   // multiple
use anima::core::rng::{source, registry, router}      // nested
use stdlib::qrng_anu as qa                    // alias
```

### §3.3 컴파일타임 검증

- path AST → resolver 정적 lookup
- 미존재 path → `error E6101: cannot resolve 'stdlib::qrng_ano' (did you mean 'qrng_anu'?)`
- ambiguous import → `error E6102: 'source' resolves to multiple candidates`

### §3.4 backward compat

- v2 release ~ v2+30d: 양 형태 지원 (string-form은 deprecated warn)
- v2+30d ~ v2+90d: string-form은 hard warn
- v2+90d 이후: string-form은 error (v1.x EOL)

## §4 L12 — Attr Composition

### §4.1 현 상태 (v1) — 4-attr stack

`tool/browser_harness.hexa` 등 정형:

```hexa
// @tool(slug="browser_harness", desc="...")
// @sentinel(__BROWSER_HARNESS_PROBE__ <PASS|FAIL>)
// @usage(hexa run tool/browser_harness.hexa --probe)
// @resolver-bypass(reason="canonical install path trust")
fn main() { ... }
```

(주의: anima v1 tools는 attr이 **comment-form**으로만 land. 실제 #[attr] syntax 활성화는 v2 이후.)

### §4.2 v2 syntax

```hexa
@module {
    tool: { slug: "browser_harness", desc: "..." }
    sentinel: { name: "BROWSER_HARNESS_PROBE", verdicts: ["PASS", "FAIL"] }
    usage: "hexa run tool/browser_harness.hexa --probe"
    resolver-bypass: { reason: "canonical install path trust" }
}
fn main() { ... }
```

### §4.3 컴파일러 expansion

AST transform: `@module { tool: T, sentinel: S, usage: U, resolver-bypass: R }`
→ 4 individual attr nodes (`@tool(T)`, `@sentinel(S)`, `@usage(U)`, `@resolver_bypass(R)`).

기존 v1 tooling (lint, AOT, audit) 은 expanded form 만 처리 → 변경 없음.

### §4.4 migration tool

`hexa migrate attr-stack-to-group <file>` — stack → group 자동 변환 (idempotent, byte-equivalent expansion으로 round-trip 검증).

## §5 Implementation checklist (multi-month, impl deferred)

### §5.1 L5 declarative module — ~3개월 / 6 steps

- [ ] grammar: `@module <name> { ... }` block syntax (parser ext)
- [ ] semantic analyzer: source / registry / router / env-overrides 절 검증
- [ ] code generator: 4-core file 자동 생성 (parser 출력 → disk write)
- [ ] auto-discover: `modules/<feature>/*.hexa` glob → registry dispatch wiring
- [ ] selftest harness: 10 fixture (rng, eeg, audio, philosophy, etc.)
- [ ] PoC migration: `anima/core/rng` 1005 LOC → `@module rng { ... }` ~50 LOC

### §5.2 L6 typed use — ~1개월 / 4 steps

- [ ] grammar: `use a::b::c`, `use a::{b, c}`, `use a::b as c` path AST
- [ ] resolver: compile-time path 검증 + autocompletion hint
- [ ] backward compat: string-form deprecate (warn → hard-warn → error 3-stage)
- [ ] selftest: 7 fixture (single/multi/nested/alias/missing/ambiguous/edge)

### §5.3 L12 attr composition — ~1개월 / 4 steps

- [ ] grammar: `@module { tool: {...}, sentinel: {...}, ... }` block
- [ ] expander: AST transform group → individual attr (preserves source location)
- [ ] backward compat: stacked attr 동시 지원 (deprecated → ramp)
- [ ] migration tool: `hexa migrate attr-stack-to-group`

**총 14 impl step.** Sequential / parallel mix 가능.

## §6 v2 roadmap — 통합 timeline

| Phase | 시점 | 내용 | 의존 |
|-------|------|------|------|
| α | now | stdlib only (qrng_anu, yaml, etc.) | — |
| β | +1~2주 | parser ext base (block syntax 기반) | α |
| γ | +1개월 | strict semantic (E51xx, E61xx, E12xx error codes) | β |
| **δ1** | +2개월 | L6 typed use (~1개월 impl) | γ |
| **δ2** | +3개월 | L12 attr composition (~1개월 impl) | γ |
| **δ3** | +3~6개월 | L5 declarative module (~3개월 impl) | δ1, δ2 |
| **v2-rc** | +6개월 | release candidate, v1+v2 dual-support 시작 | δ1+δ2+δ3 |
| **v2** | +7개월 | stable release, v1 deprecation warn 시작 | v2-rc |
| **v1 EOL** | +10개월 | v1 string-use + stack-attr error | v2+90d ramp |

총 ~10개월 plan, multi-month commitment.

## §7 Backward compatibility strategy

### §7.1 Dual-form coexistence (v2-rc ~ v2+90d)

- L6: `use "stdlib/x"` (deprecated warn) ↔ `use stdlib::x` (preferred)
- L12: stacked attr (deprecated warn) ↔ group attr (preferred)
- L5: 4-core manual file (deprecated warn) ↔ `@module { ... }` (preferred)

### §7.2 Migration support

- `hexa migrate use-string-to-typed <file>` — L6 자동 변환
- `hexa migrate attr-stack-to-group <file>` — L12 자동 변환
- `hexa migrate manual-module-to-declarative <feature>` — L5 자동 변환 (4-core file → 1-block)
- 모든 migration tool은 idempotent + byte-equivalent expansion 검증.

### §7.3 EOL signal

- v1.x EOL = v2 release + 90d
- 마지막 v1.x release는 LTS 명시 (security-only patch 6개월 추가)
- v2 release 시점에 v1 EOL 날짜를 release notes 명시

## §8 raw#10 caveats — Implementation risk 정량화

| # | caveat | risk level |
|---|--------|------------|
| 1 | hexa-lang stage0 parser는 block-style attr 미지원 → grammar ext 선행 (Phase β 의존) | HIGH |
| 2 | L5 code generator = parser가 disk write 야기 → side-effect dependency, AOT cache 무효화 trigger 신규 | HIGH |
| 3 | v1 4-core file 패턴은 raw 270/271/272 enforce — L5 가 raw spec 자체를 deprecate해야 (raw revision dependency) | HIGH |
| 4 | L6 typed use는 IDE/LSP support 없으면 "그냥 syntax sugar" — LSP integration이 v2-rc 동시 land 필요 | MEDIUM |
| 5 | L12 attr composition은 expand AST가 source location 유지해야 stacktrace 보존 — non-trivial | MEDIUM |
| 6 | anima 의 attr 사용은 현재 **comment-form** (`// @tool(...)`) — 실제 attr syntax 활성화가 v2 prerequisite | MEDIUM |
| 7 | breaking change candidate → 다운스트림 (anima, anima-eeg, anima-physics, need-singularity) coordination 필요 | HIGH |
| 8 | migration tool idempotency 검증 = round-trip stable (parse → expand → emit → parse 동치) — 검증 부담 large | MEDIUM |
| 9 | dual-form 90d ramp = parser + emitter dual-codepath = maintenance burden + potential codepath divergence | MEDIUM |
| 10 | v1 EOL date 명시 시 외부 consumer freeze risk — communication overhead 필수 | LOW |
| 11 | L5 auto-discover (`modules/<feature>/*.hexa` glob) = filesystem race condition (cron / parallel run 시 partial discovery) | MEDIUM |
| 12 | env-overrides 절은 runtime config — 컴파일타임 spec과 runtime config 경계 모호 (security review 필요) | LOW |
| 13 | 14 step impl checklist는 happy-path만 — error recovery / partial migration / rollback 미정의 | MEDIUM |
| 14 | hexa-lang v2 release = anima 다수 file `.ai.md` 갱신 cascade 발생 (정확한 cascade size 미정량) | LOW |
| 15 | LTS 6개월 commitment = security patch 인력 부담, 1-author project에서 sustainable 미보장 | MEDIUM |

**HIGH risk count: 4** (cav 1, 2, 3, 7).
**MEDIUM risk count: 7**.
**LOW risk count: 4**.

mitigation 전략은 별도 문서 (`phase_delta_risk_mitigation.ai.md`) 로 분리 land 권장.

## §9 PoC plan — `anima/core/rng` → `@module rng`

### §9.1 LOC reduction estimate

| 항목 | v1 | v2 | reduction |
|------|----|----|-----------|
| source.hexa | 143 | 0 (auto-gen) | -143 |
| registry.hexa | 339 | 0 (auto-gen) | -339 |
| router.hexa | 319 | 0 (auto-gen) | -319 |
| rng_main.hexa | 204 | 0 (auto-gen) | -204 |
| `@module rng { ... }` | 0 | ~50 | +50 |
| **net** | **1005** | **~50** | **-955 (-95.0%)** |

### §9.2 검증 기준

- byte-identical disk output: 4-core auto-gen file이 v1 manual file과 byte-identical (확정 reproducibility)
- selftest equivalence: rng_main 의 모든 selftest가 PASS (interface contract / fallback chain / determinism / stub sentinel / aggregate)
- `modules/rng/{urandom, esp32, anu, ibm_q, idq_quantis, kaist_optical}.hexa` (6 modules) 변경 0 (auto-discover만 추가)

### §9.3 마이그레이션 단계

1. `@module rng { ... }` 1-block 작성
2. `hexa build` → 4-core file 자동 생성
3. byte-identical 비교 (v1 4 file vs v2 auto-gen 4 file)
4. PASS → v1 4 file 삭제 (마이그레이션 commit)
5. FAIL → spec 보정 후 재시도

## §10 File index — sha-pin

| 파일 | 역할 | sha256 |
|------|------|--------|
| `/Users/<user>/core/hexa-lang/docs/phase_delta_language_v2_spec.ai.md` | 본 문서 (SSOT) | (자기 참조 — 작성 후 핀) |
| `/Users/<user>/core/hexa-lang/state/markers/phase_delta_language_v2_spec_landed.marker` | landing marker | (작성 후 핀) |
| `/Users/<user>/core/anima/anima/core/rng/source.hexa` | v1 4-core file (PoC source) | (143 LOC) |
| `/Users/<user>/core/anima/anima/core/rng/registry.hexa` | v1 4-core file | (339 LOC) |
| `/Users/<user>/core/anima/anima/core/rng/router.hexa` | v1 4-core file | (319 LOC) |
| `/Users/<user>/core/anima/anima/core/rng/rng_main.hexa` | v1 4-core file | (204 LOC) |
| `/Users/<user>/core/anima/anima/modules/rng/*.hexa` | 6 source modules (PoC migration target — 변경 0) | (auto-discover) |

---

## Appendix A — v1 EOL recommendation

| 항목 | 권장 |
|------|------|
| EOL 날짜 | v2 release + 90d (총 ~10개월 후) |
| LTS 정책 | 마지막 v1.x release security-only patch 6개월 추가 |
| 외부 communication | v2-rc 시점 (총 ~6개월 후) release notes에 EOL date 명시 |
| migration window | v2 release ~ EOL 90일간 dual-form 지원 |
| post-EOL | string-use + stack-attr + manual 4-core file 모두 hard error |

## Appendix B — Phase δ exit criteria

- [ ] L5/L6/L12 spec 모두 land (본 문서 §2-§4)
- [ ] impl checklist 14 step 모두 명시 (§5)
- [ ] v2 roadmap 4 phase timeline 명시 (§6)
- [ ] backward compat 3-stage ramp 명시 (§7)
- [ ] 15 raw#10 caveats 정량화 (§8)
- [ ] PoC LOC reduction estimate (§9)
- [ ] file index sha-pin 골격 (§10)
- [ ] marker land

본 문서 자체가 Phase δ exit criteria의 spec 결과물.

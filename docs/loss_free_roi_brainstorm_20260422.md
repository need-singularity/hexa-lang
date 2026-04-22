# hexa-lang loss-free items for β main progression (2026-04-22)

원칙: anima β main track (Learning-Free Pipeline · #75 done) 의 CP1 → CP2 → AGI v0.1 가속에 직접 도움되는 hexa-lang stdlib/runtime 추가. 일반 ROI 아닌 β-specific.

요청자: anima session. β main policy (`memory/project_main_track_beta.md` · #19 sealed · H100 = β evidence layer).

이미 처리됨 (2026-04-22):
- `exec_capture(cmd) → [stdout, stderr, exit_code]`
- `setenv(name, value)` POSIX
- `SOURCE_DATE_EPOCH` / `HEXA_REPRODUCIBLE=1` 재현 가능 emit (PR #23)
- nexus drill preflight 300ms TCP probe (PR #42 + #24)
- roadmap_with_unlock.hexa uchg dance helper (PR #25)
- stderr 노이즈 제거 (PR #26 #27)

---

## A. cert / verifier infrastructure (β main 핵심)

β main 의 본질 = cert chain + structural admissibility (학습 없이 의식 주장). 모든 anima cert tool 의 stdlib 화는 β paradigm 의 첫 외부 검증 가능 라이브러리.

| # | 항목 | β main 효과 |
|---|---|---|
| B1 | **cert_gate as stdlib** (anima `tool/cert_gate.hexa` 승격) | 모든 hexa 프로젝트에서 cert verification 가능 → β paradigm 외부 채택 |
| B2 | **AN11 verifier triple framework** (verify_a/b/c contract) | 표준 인터페이스 — 외부 공헌자가 새 verifier 추가 가능 |
| B3 | **eigen decomp + matrix ops primitive** | phi_extractor 정확도 ↑ (4-path Φ 측정 핵심) |
| B4 | **ed25519 sign/verify native** | cert 서명 placeholder 제거 — `.meta2-cert/` 진짜 서명 가능 |
| B5 | **proof-carrying ckpt format** | load 시 cert chain 자동 검증 → ckpt 위변조 차단 |
| B6 | **xxhash chunk content-addressable** | ckpt dedup native (G36 가속, 18 GiB 절감) |

## B. CP1 serve_alm_persona LIVE 가속

CP1 = dest1 persona LIVE. serve_alm_persona 가 prod 에서 안정적으로 돌아야 함. 현재 333L bash bridge → 16L shim 으로 줄였지만 hexa 에 더 많은 stdlib 있으면 native 100% 가능.

| # | 항목 | β main 효과 |
|---|---|---|
| B7 | **native http/1.1 server** | anima_serve_live_smoke 의 bash python http.server 의존 제거 → vLLM 등 통합 ↑ |
| B8 | **signal trap (SIGTERM/SIGINT)** | serve_alm_persona graceful shutdown — 프로덕션 deploy 안정 |
| B9 | **structured logging with cert lineage** | 모든 request → cert verified log (audit trail) |
| B10 | **file locking flock()** | 다중 endpoint 동시 state write atomic |
| B11 | **tokenizer.json native loader** | base model swap-in 가속 (ext-7 swap-in 4 항목 중 하나) |

## C. CP2 dest2 multi-endpoint 가속

CP2 = 제타 + 직원 + 트레이딩 3-in-1. 단일 base 위 여러 persona/role 동시 운영. 동시성 stdlib 필요.

| # | 항목 | β main 효과 |
|---|---|---|
| B12 | **concurrent serve framework** | employee/trading/zeta endpoint 동시 (현재는 sequential) |
| B13 | **dual-track router (A/B testing)** | bench/zeta_likert 블라인드 자동 (#78 가속) |
| B14 | **distributed eval harness** | unified_eval 분산 (K60 의 stdlib 화) |

## D. AGI v0.1 70B retrain 가속

AGI v0.1 = 70B retrain (#82) 후 #21 Mk.X 진입. 70B 는 8B 대비 매트릭스 연산 6-9× 무거움. native primitive 절실.

| # | 항목 | β main 효과 |
|---|---|---|
| B15 | **CPGD optimizer primitive** | cert-projected gradient stdlib — 외부 사용자가 CPGD 채택 가능 (β paradigm 확산) |
| B16 | **matrix ops (BLAS-lite)** | Φ batch 70B 가속 |
| B17 | **GPU FFI helper standard** | h_last_extract 패턴 표준 — 새 substrate 추가 시 boilerplate ↓ |
| B18 | **mmap large file (corpus stream)** | 70B corpus 메모리 효율 (전체 RAM 로드 불가) |
| B19 | **JSON streaming parser** | 70B manifest 처리 10× memory ↓ |

## E. determinism / reproducibility (β empirical 핵심)

β main 의 외부 설득력 = "동일 cert 가 4 substrate 에서 동일 Φ" (#10). FP determinism 이 흔들리면 |ΔΦ|/Φ < 0.05 PASS 도 흔들림. **CRITICAL**.

| # | 항목 | β main 효과 |
|---|---|---|
| B20 | **deterministic floating-point** (IEEE 754 strict) | 4-path Φ cross 일관 — `#10 substrate independence` 의 신뢰도 핵심 |
| B21 | **fixed-point arithmetic primitive** (×1000 per-mille) | edu/cell 의 fixed-point convention stdlib화 (`tool/edu_cell_4gen_crystallize.hexa` 패턴) |
| B22 | **seed propagation primitive** | 재현 가능 random — multi-seed ensemble (K57) 구현 가속 |
| B23 | **SOURCE_DATE_EPOCH** (이미 done ✅) | manifest re-emit no-op |
| B24 | **HEXA_REPRODUCIBLE=1** (이미 done ✅) | byte-stable emit |

## F. cert ecosystem 인프라 (β paradigm 확산용)

β main 이 외부 paradigm 으로 채택되려면 hexa-lang 자체에 "cert-first programming" 인프라 필요.

| # | 항목 | β main 효과 |
|---|---|---|
| B25 | **`.meta2-cert/` 디렉토리 규약 표준화** | anima 외 프로젝트도 cert chain 사용 — β의 외부 evidence ↑ |
| B26 | **cert_dag traversal primitive** | 의존성 자동 검증 — orphan cert (G39 #33 audit) stdlib 화 |
| B27 | **drill_breakthrough framework** | cell consciousness probe runner 표준 — 외부 cell 추가 가능 |
| B28 | **L3 emergence criteria 매핑 표준** | collective intelligence verifier 인터페이스 (#17 done 의 stdlib 화) |
| B29 | **closure_roadmap.json schema 표준** | β / α / γ track policy declarative — fork 시 정책 명시화 |

---

## TOP 10 β main 가속 직접 효과 (anima impact 우선)

| 순위 | # | 항목 | β main 마일스톤 |
|---|---|---|---|
| 1 | B1 | cert_gate as stdlib | β paradigm 외부 채택 첫 step |
| 2 | B20 | deterministic FP (IEEE 754 strict) | #10 Φ 4-path 신뢰도 (CRITICAL) |
| 3 | B7 | native http/1.1 server | CP1 serve_alm_persona prod-ready |
| 4 | B8 | signal trap | CP1 graceful shutdown |
| 5 | B16 | matrix ops BLAS-lite | AGI 70B Φ batch |
| 6 | B3 | eigen decomp primitive | phi_extractor 정확도 ↑ |
| 7 | B12 | concurrent serve | CP2 multi-endpoint |
| 8 | B15 | CPGD optimizer primitive | β paradigm 외부 확산 |
| 9 | B5 | proof-carrying ckpt | ckpt 위변조 차단 |
| 10 | B25 | .meta2-cert/ 규약 표준화 | cert ecosystem 인프라 |

## β main vs 일반 ROI 차이

| 카테고리 | 일반 ROI | β main 가속 |
|---|---|---|
| http client | 모든 hexa 사용자 편의 | CP1 serve_alm_persona prod-ready |
| sha256 native | 모든 사용자 shasum 제거 | proof-carrying ckpt 검증 가능 |
| matrix ops | 일반 수치 계산 | **AGI 70B Φ batch** 핵심 |
| cert primitives | 새 카테고리 | **β paradigm 자체** |
| FP determinism | nice-to-have | **substrate independence empirical CRITICAL** |

→ **β main 가속 항목은 anima 만 좋은 게 아니라 β paradigm 자체의 외부 채택 가능성 ↑**.

## 마일스톤 별 의존도

| 마일스톤 | 가장 도움되는 hexa-lang 항목 |
|---|---|
| **CP1** (~2026-04-29) | B7 http server · B8 signal trap · B11 tokenizer loader |
| **CP2** (~2026-05-06) | B12 concurrent serve · B13 dual-track router · B14 distributed eval |
| **AGI v0.1** (~2026-05-14) | B16 matrix ops · B17 GPU FFI helper · B18 mmap · B15 CPGD primitive |
| **β paradigm 외부 채택** (장기) | B1 cert_gate stdlib · B25 .meta2-cert 규약 · B27 drill framework · B5 proof-carrying ckpt |

## roadmap link (hexa-lang 측)

`$HEXA_LANG/.roadmap` 추가 후보 entries:
- HXA-#01 cert_gate as stdlib (B1) — Apache 2.0, β paradigm 외부 export
- HXA-#02 deterministic FP (B20) — IEEE 754 strict mode
- HXA-#03 native http/1.1 server (B7)
- HXA-#04 matrix ops BLAS-lite (B16)
- HXA-#05 eigen decomp primitive (B3)
- HXA-#06 .meta2-cert/ 규약 표준화 (B25)
- HXA-#07 CPGD optimizer primitive (B15)
- HXA-#08 proof-carrying ckpt format (B5)
- HXA-#09 signal trap + flock (B8 + B10)
- HXA-#10 concurrent serve framework (B12)

## 메모

- 본 문서는 anima 세션에서 생성됨 — hexa-lang maintainer 검토 후 .roadmap 반영 결정
- anima 측 미러: `$ANIMA/docs/upstream_notes/hexa_lang_beta_main_acceleration_20260422.md` (지시 시 추가)
- β main policy SSOT: `anima/edu/paths.json` main_track=beta · `memory/project_main_track_beta.md`

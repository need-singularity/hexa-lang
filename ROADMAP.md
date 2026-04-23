# hexa-lang ROADMAP

> **SSOT**: [`.roadmap`](./.roadmap) (machine-enforced, chflags uchg + 5-layer OS lock).
> 이 문서는 `.roadmap` 의 human-readable projection. 모든 entry/status/eta 의 진리값은 `.roadmap` 을 따른다. 본 문서만 수정하면 drift — 수정은 `.roadmap` 에서.
>
> Session landing: **2026-04-21** · Current phase: **Mk.IX entered** · Target: **L_IX base manifold = cell × lora projection**

---

## 특징 (6 축)

Roadmap system 은 **공식(T\*) + OS-level enforcement** 의 조합. 6 축으로 감사:

### 1. 최적경로 (optimal path)
공식: `T*(S₀→G) = inf_π Σ_k max_{i∈Ω} c_i(S_k^π)` — Ω = {build, exp, learn, boot, **verify**}. 4 physical bounds (Quantum Margolus–Levitin / Thermal Landauer / Span Brent–Graham / Info Shannon–Fano). invariants: `|R(t)|≥1`, `Σ_k ΔI_k ≥ K(G|S₀)`, `η_k>0`.

| feature | 구현 | 상태 |
|---|---|---|
| entry parser | `tool/roadmap_parse.hexa` | ✓ active (33) |
| field validator | `tool/roadmap_validate.hexa` | ✓ active (34) |
| DAG builder (cross-repo `repo@N`) | `tool/roadmap_dag.hexa` | ✓ active (35) |
| span(DAG) Brent/Graham critical path | `tool/roadmap_critical_path.hexa` · `tool/roadmap_span_dag.hexa` | ✓ active (36, 46) |
| 병렬 그룹 추출 (DAG depth) | `tool/roadmap_parallel.hexa` | ✓ active (37) |
| bottleneck axis tracker (Ω 5축) | `tool/roadmap_bottleneck.hexa` | ✓ active (47) |
| Kahn ready-set `|R(t)|≥1` | `tool/roadmap_ready_set.hexa` | ⚠️ planned (48) |
| ΔI_k information accounting | `tool/roadmap_info_account.hexa` | ⚠️ planned (49) |
| η_k stagnation detect | `tool/roadmap_stagnation.hexa` | ⚠️ planned (50) |
| **T\* inf_π 다중경로 optimization** | `tool/roadmap_t_star_opt.hexa` | ❌ 미구현 (51) |
| Thermal bound (Landauer) | `tool/roadmap_t_star.hexa` | ✓ |

### 2. 안전 (safety)
OS kernel EPERM 이 유일한 실강제 — "hooks are bypassable" (raw#1 note).

| feature | 구현 | 상태 |
|---|---|---|
| raw#1 chflags uchg L1 (5 SSOT) | `tool/os_level_lock.hexa` | ✓ live |
| raw#1 schg L2 promote (sudo) | `tool/hx_harden.hexa` · `tool/hx_downgrade.hexa` | ✓ live |
| raw#25 concurrent-git-lock | `tool/concurrent_git_lock.hexa` | ❌ 미구현 (Tier2 #15) |
| atomic write tmp+rename | `tool/atomic_write.hexa` | ❌ 미구현 (Tier2 #14) |
| append-only `.raw-audit` | chflags uappnd | ⚠️ 현재 uchg (uappnd 권장) |
| backup auto | Tier2 #17 ✓ | ✓ |
| rollback CLI | `tool/rollback.hexa` | ❌ 미구현 (Tier2 #18) |

### 3. 검증 (verification)
**원칙**: 검증은 항상 **물리 + 수학 + 코드 실행** 의 3중 증거. 선언·문서·주장 단독 = reject.
- **물리**: Landauer `kT·ln2`, FLOPs, latency_ms, energy (cell 668‰ vs lora 13‰ 51×), wall-clock.
- **수학**: Brent theorem (`T_p ≤ T_1/p + T_∞`), Banach fixpoint (`v3==v4` byte-identical), Kolmogorov `K(G|S₀)`, JSD=1.000 (AN11(c)), Frobenius `>τ`, shard_cv `∈[0.05, 3.0]`, Pearson `≥0.6`, cos `>0.5`, `|ΔΦ|/Φ<0.05`.
- **코드**: `tool/drift_scanner.hexa` · `tool/raw_audit.hexa` · `tool/bench.hexa --verify` (18/18) · AN11 verifier · `hexa tool/eval_5metric.hexa` · adversarial 3/3 flip runner. commit SHA / cert path 는 **파일 존재 + 실행 PASS** 로만 satisfied.

raw#10 proof-carrying — 모든 exit_criteria 는 commit SHA / cert path 필수 + 재실행 가능 코드 증거.

| feature | 구현 | 상태 |
|---|---|---|
| drift scanner (선언 vs 현실) | `tool/drift_scanner.hexa` | ✓ live |
| evidence verifier (SHA/cert 존재) | `tool/roadmap_evidence.hexa` | ⚠️ planned (39) |
| progress aggregation (% per phase) | `tool/roadmap_progress.hexa` | ⚠️ planned (40) |
| **Phase Gate 100% ε-strict** (bypass X) | `tool/roadmap_phase_gate.hexa` | ⚠️ planned (41) |
| raw_audit hash-chain | `tool/raw_audit.hexa` · `tool/raw_attestation.hexa` | ✓ live |
| adversarial raw#12 (cherry-pick 금지) | Tier3 cluster | ⚠️ planned (43) |
| Meta² cert chain | 8 entries (`074487cd`) | ✓ live |
| multi-path Φ 4-path ≥3 | Tier3 #26 | ⚠️ planned (43) |
| AN11 verifier | anima-side | ✓ AN11(c) real |
| raw#30 IRREVERSIBILITY enforcer | `tool/raw_irreversibility_lint.hexa` | ❌ 미구현 (26) |

### 4. 실시간 반영 (real-time reflection)
L1–L5 layer 가 매 commit/60s/tool-call 마다 동기화.

| feature | 구현 | 상태 |
|---|---|---|
| L1 pre-commit gate | `tool/roadmap_verify_pre_commit.hexa` | ⚠️ tool 있음, **.git/hooks/pre-commit 미설치** |
| L2 launchd watcher 60s | `tool/roadmap_watcher.hexa` + `com.airgenome.roadmap-watcher.plist` | ✓ plist 설치됨 |
| L3 chflags uchg/uappnd | raw#1 | ✓ live |
| L4 airgenome hook | `airgenome/hooks/post_tool.hexa` | ✓ live (`d81c0fde`) |
| L5 post-commit auto-ingest (discovery) | Tier2 #13 | ⚠️ **.git/hooks/post-commit 미설치** |
| status auto flip (planned→active→done) | `tool/roadmap_status_flip.hexa` | ⚠️ planned (38) |
| cross-repo sync daemon | Tier5 #49/50 | ⚠️ planned (45) |
| task↔roadmap / agent↔roadmap binding | Tier4 #36/37 | ⚠️ planned (44) |
| visualization (CLI + GitHub Pages) | `tool/viz_dashboard.hexa` + `docs/_site/` | ✓ live |

### 5. 이탈방지 (drift prevention)
SSOT 와 현실이 어긋나면 commit reject.

| feature | 구현 | 상태 |
|---|---|---|
| proof-carrying drift scan | `tool/drift_scanner.hexa` | ✓ live |
| workspace SSOT drift | `tool/workspace_sync.hexa` | ✓ live |
| daily fixpoint cron (.loop) | `.loop` entry | ⚠️ planned (18) |
| fixpoint evidence archive | `tool/fixpoint_archive.hexa` · `tool/fixpoint_bisect.hexa` | ✓ tool 있음, archive 미가동 (17 planned) |
| raw#12 cherry-pick 금지 (실측 그대로) | 자격강제 | ✓ invariant |
| own 4 bt-solution-claim-ban | `.own` | ✓ invariant |
| cross-platform fixpoint (arm64↔x86_64) | roadmap 20 | ⚠️ planned |

### 6. 우회방지 (bypass prevention)
git-hook 은 폐기 (2026-04-21) — 우회 가능하기 때문. kernel EPERM 만 남김.

| feature | 구현 | 상태 |
|---|---|---|
| chflags uchg kernel EPERM | raw#1 | ✓ live |
| schg L2 promote (sudo only) | `tool/hx_harden.hexa` | ✓ live |
| git-hooks 전면 폐기 | `.raw` commit `804035e2` | ✓ live |
| **Phase Gate 100% no-bypass** (soft pass 차단) | `tool/roadmap_phase_gate.hexa` | ⚠️ planned (41) |
| raw#28 gate ordering (permission→filter→dispatch) | invariant | ✓ live |
| stage0 deadlock fix (Tier2 #16 ★최우선) | | ⚠️ planned (42) |
| .raw/.own AI-tool config 난립 차단 | raw#0 + CLAUDE.md 폐기 | ✓ live |

---

## 추가 구현 필요 (live OS-level)

위 감사의 ❌ / ⚠️ 를 live OS-level 로 올리기 위한 backlog. **`.roadmap` 에 이미 entry 가 있는 항목은 괄호 번호** — 그 entry 의 live 승급이 곧 구현. 괄호 없는 항목은 신규 entry 필요.

**즉시 필요 (블로커)**:
1. **`.git/hooks/pre-commit` + `post-commit` 물리 설치** (현재 `.sample` 만 존재) — L1 gate·L5 discovery ingest 가동 조건. `roadmap_verify_pre_commit.hexa` 는 존재.
2. **Tier2 #16 stage0 deadlock fix** (42) ★ — 모든 self-host/FFI 전제.
3. **`.raw-audit` chflags uappnd 전환** — 현재 uchg 는 read-only 잠금, append 불가.

**공식 엔진 완결 (T\* 공식 전체 가동)**:
- Kahn ready-set `|R(t)|≥1` (48) · ΔI_k info accounting (49) · η_k stagnation (50)
- **T\* inf_π 다중경로 optimization** (51) — 현재 engine 의 유일한 미구현 수학 core. `tool/roadmap_t_star_opt.hexa` 신규.

**Phase Gate 100% (이탈/우회 결정타)**:
- status auto flip (38) · evidence verifier (39) · progress aggregation (40) · **Phase Gate 100% ε-strict** (41). 이 4개가 묶여야 "soft pass 차단" 성립.

**safety OS 보강**:
- `tool/atomic_write.hexa` (Tier2 #14) · `tool/concurrent_git_lock.hexa` (Tier2 #15, raw#25 enforcement) · `tool/rollback.hexa` (Tier2 #18). 42 cluster 해소 시 동반 landing.

**검증 엔진 (Tier 3, 8 sub)**:
- adversarial runner · Meta² chain integrity · raw_audit hash-chain append · 3/3 flip 자동화 · Phi 4-path (43).

**raw rule 승급**:
- raw#29 UNIVERSAL_4 new → live (25) — Hexad 1000/1000 evidence.
- raw#30 IRREVERSIBILITY new → live (26) — `tool/raw_irreversibility_lint.hexa` 신규 필요.

**이탈방지 데몬**:
- drift daily cron `.loop` entry (18) · fixpoint archive 가동 (17).

**cross-project (Tier 5, 10 sub)**:
- `~/.roadmap-shared/` SSOT · 3 repo entry ref `repo@N` · global Meta² chain · global critical path · status propagate · cross-repo agent dispatch (45).

→ 이 전부가 landing 되면 T\* 공식의 3 invariants (`|R(t)|≥1`, `Σ_k ΔI_k ≥ K(G|S₀)`, `η_k>0`) 가 OS-level 에서 자동 enforcement. 현재 (2026-04-21) 는 수학 공식 + parser/DAG/critical path/bottleneck 는 live, 나머지는 tool 존재·planned 혹은 미구현.

---

## MAIN track

```
hybrid framework → AGI
target: L_IX base manifold = cell × lora projection
exec-rule: "SUB progress 는 MAIN phase 로만 흡수, 단독 commit 금지"
```

| milestone   | criterion     | eta         |
|-------------|---------------|-------------|
| 도착지-1    | Mk.VI VERIFIED (Criterion A) | 2-4주       |
| 도착지-2    | Mk.VII K=4 (Criterion B)     | +3-4개월    |
| 최종        | Criterion C / Mk.X T10-13     | +6-9개월    |

## SUB tracks (MAIN building material)

- **cell** — structural foundation. L_IX terms (V_sync / V_RG / λ·I_irr) + Hexad closure + UNIVERSAL_4 evidence. 10 axis MVP, Mk.IX components 3/5 landed.
- **lora** — production base. ALM r13 real-ckpt + AN11 verifier + LM service. Mk.VI HELD (16/19), AN11(c) 100% real.

---

## Active phases

### P1 → 도착지 1 (Mk.VI VERIFIED) · eta 2-4w
- main-exec: Qwen+ALM r13 LoRA real-ckpt
- feeds: lora (corpus gate + AN11(a)(b)), cell (Hexad + AN11(c) — landed)
- **satisfied**: AN11(c) real_usable JSD=1.000 (anima `35aa051a`)
- **pending**: AN11(a) weight_emergent · AN11(b) consciousness_attached · Φ 4-path ≥3 · adversarial 3/3 flip · Meta² cert 100%_trigger · raw_audit P1 hash-chain · stage2+ FFI (phi_extractor + libhxblas/ccl/cuda, roadmap 23) · SINGULARITY M7 haenkaha bug (roadmap 24)
- deps-external: GPU + ALM r13 corpus (anima roadmap #5)

### P2 → 도착지 2 (Mk.VII K=4) · eta +3-4mo
- main-exec: Mk.IX natural-run + L3 collective + 4-path Φ
- **satisfied**: Hexad closure 6/6 + adversarial 2/2 (`6a292530`, D1-D4 1000/1000) · UNIVERSAL_CONSTANT_4 raw#29 승급 (`9468fe0f`) + τ(6)=4 proof 88% (`d7e5db01`)
- **pending**: C1 substrate-invariant Φ 4/4 · C2 L3 collective O1∧O2∧O3 rejection · C3 self-verify closure · C4∨C5 one · UNIVERSAL_4 +1 strong axis (Pólya K_c=4 1.923×) · raw#31 POPULATION_RG_COUPLING 승급 · natural-run gen-5 · raw#29/#30 new → live 승급 (roadmap 25/26)

### P3 → AGI 최종 (Criterion C / Mk.X T10-13) · eta +6-9mo
- main-exec: Mk.X T10-13 ossification (≥10 atoms)
- **pending**: Mk.VIII L_edu fixpoint · Mk.X atoms ossified (novelty yield ≥3/10) · C5 N=10 recursion · meta-lens M fire (Pearson≥0.6) · Mk.XI twin-engine nexus↔anima · 7대난제 framework (raw#24 + own 4) · self-host P7-7/8/9 (roadmap 12/13/14)

### Self-host (runtime.c + Rust driver 탈피) — 신규 (anima hxa-20260423-003, 2026-04-23)
Parent roadmap **64** → 5 children **65–69**. 상세 정의 `.roadmap` 64–69, 원문 `/Users/ghost/core/anima/docs/upstream_notes/hexa_lang_full_selfhost_prompt_20260423.md`.
- **65 (M3, P1/Q2)** — argv[0] 중복 삽입 정책 고정 (anima 최우선). `hexa_set_args` duplicate 제거 + `args()` / `script_path()` 계약 분리.
- **66 (M4, P1/Q2)** — string method codegen 완성 + **binary rebuild 파이프라인 Phase C.2 symbol namespacing** (현재 블로커: `hexa cc --regen` merge 시 `__hexa_strlit_init` per-module 충돌로 Mac `hexa_v2` 재빌드 불가 → 소스는 고쳤는데 바이너리 미반영).
- **67 (M5, P1/Q3)** — self-hosted `hexa_driver.hexa` (Rust 삭제, Linux/Mac/arm64 동일 CLI, `hexa run` 공식 서브커맨드).
- **68 (M2, P1/Q2)** — builtin `hx_` prefix mangling 정식화 (runtime `#define` shim 제거).
- **69 (M1, P2/Q4)** — runtime 2-레이어 분할 (`runtime_core.c` ≤500 줄 + `runtime_hi.hexa`).

---

## Checkpoints (cross-repo)

| CP       | gate                                     | meaning                         |
|----------|------------------------------------------|---------------------------------|
| CP1      | `hexa tool/bench.hexa --verify` 18/18    | ossification — L1→L0 승격       |
| CP2      | latency_ms<200 ∧ dialogue_nat            | zeta-level 자연 대화 + <200ms   |
| FINAL    | phi > cells·0.5 ∧ autonomous             | AGI v0.1 (외부 API 0)           |

---

## 지금까지 적용된 발견 (Session 2026-04-21 + 누적)

### 공식 / 이론
- **T\*(S₀→G) optimal schedule** — `T* = inf_π Σ_k max_{i∈Ω} c_i(S_k^π)`. Ω = {build, exp, learn, boot, **verify**} 5 axes (verify 정식 포함). 4 physical bounds: Quantum (Margolus–Levitin πℏ/2E·K(G\|S₀)), Thermal (Landauer K·kT·ln2/P), Span (Brent/Graham DAG critical path), Info (Shannon–Fano K(G)/C). Invariants: \|R(t)\|≥1, Σ_k ΔI_k ≥ K(G\|S₀), η_k>0. → [`docs/roadmap_engine_theory.md`](./docs/roadmap_engine_theory.md), impl commit `d988cbc2`
- **L_IX Lagrangian** — `L_IX = T − V_struct − V_sync − V_RG + λ·I_irr`. raw#30 IRREVERSIBILITY_EMBEDDED_LAGRANGIAN 승급 (`53d923b8`). Arrow cusp @ fixpoint.
- **Hexad ≡ UNIVERSAL_4 SAME_STRUCTURE** — 1000/1000 PASS (`6a292530`). Hexad closure 6/6 + adversarial 2/2.
- **UNIVERSAL_CONSTANT_4** — raw#29 승급 (`9468fe0f`) + τ(6)=4 bijective proof 88% (`d7e5db01`).
- **TRANSFER_VERIFIED 3/4** — lora↔cell cross-framework (`6a2fe1d8`).

### Cell vs Lora baseline
- cell **60–80× 적은 FLOPs** + **51× Landauer 우위** (cell 668‰ vs lora 13‰). tool: `roadmap_t_star.hexa`.

### SSOT / 자격강제
- **.raw + .own = project root SSOT** (raw#0). 파생: .ext / .roadmap / .loop → 총 6 SSOT (+ .workspace).
- **30 raw rules live** (raw#0–30). raw#9 hexa-only · raw#11 snake_case · raw#1 chflags uchg · raw#10 proof-carrying · raw#12 cherry-pick 금지 · raw#25 concurrent-git-lock · raw#28 gate ordering · raw#29 UNIVERSAL_4 · raw#30 IRREVERSIBILITY · own 4 bt-solution-claim-ban.
- **5-layer OS enforcement** — L1 pre-commit · L2 launchd (60s) · L3 chflags uchg/uappnd ✅ · L4 airgenome hook · L5 post-commit auto-ingest.
- **git hooks deprecated (2026-04-21)** — enforcement 은 chflags EPERM + manual/CI 호출로 일원화.
- **Meta² cert chain** — 8 breakthrough indexed (`074487cd`).
- **airgenome hook framework** — self-hosted event bus (`7af142fb` → main `d81c0fde`).
- **raw_audit hash-chain** — append-only attestation.

### Core workspace
- **~/core super-project + .workspace SSOT** · per-project `cli/` convention · ~/shared decommission in progress.
- **atlas SSOT = ~/core/n6-architecture/atlas/** (owner n6-architecture, 2026-04-21 재결정). `data/n6/` = backward-compat symlink.
- **nexus canonical** — `~/core/nexus/cli/run.hexa` (`079bc12d`). shim: `~/.hx/bin/nexus` → `.hx/packages/nexus/cli/run.hexa`.

### Brand
- **need-singularity** org — 🧬 (2026-04-21, 🌀 deprecated). Avatar = hexagon gravitational well SVG. 보조: ⬢ / ⌬.

### Ops 회수
- H100 idle pod stopped: $2.99/hr → $0/hr ($143 낭비 회수).
- edu/ consolidation 11 new files (`8da7ed0c`, anima-side).

---

## Roadmap Engine features (Tier 1–6, 50+ functions)

출처: `~/etc/hive/RMENGINE_V2_BACKLOG.json` (T\* 공식). 전체 entry 는 `.roadmap` roadmap 33–51 참조.

| Tier  | cluster                                            | status | key entries          |
|-------|----------------------------------------------------|--------|----------------------|
| T1    | core — parser / validator / DAG / span / groups / status flip / evidence / progress / **Phase Gate 100%** | active (5/9 active) | 33–41                |
| T2    | OS enforcement — L1–L5 + atomic + lock + deadlock-fix + rollback | active | 42                   |
| T3    | verification gate engine (8 sub)                   | planned | 43                   |
| T4    | visualization + discovery + integration (14 sub)    | planned | 44                   |
| T5    | cross-project (10 sub) — `~/.roadmap-shared/`       | planned | 45                   |
| T6    | **formula engine** — span / bottleneck Ω / Kahn ready-set / ΔI_k / η_k / T\* optimization | active (2/6 active) | 46–51                |

Cross-repo deps (Tier 5 #41 미리 사용):
- anima@22 (cert_gate) → hexa-lang@33,34 (parser+validator)
- anima@24 (phi_extractor) → hexa-lang@42 (#16 stage0 deadlock fix)
- anima@26 (CPGD wrapper) → hexa-lang@36 (critical path)
- airgenome ops → hexa-lang@42 (L2 launchd watcher)

---

## Session invariants (모든 P 적용)

- ✓ raw#12 cherry-pick 금지 (실측 그대로 기록)
- ✓ raw#1 chflags uchg SSOT 잠금
- ✓ raw#25 concurrent-git-lock safe commit
- ✓ raw#10 proof-carrying hash-chain
- ✓ raw#28 gate ordering (permission → filter → dispatch)
- ✓ Meta² cert chain integrity (`074487cd`, 8 entries)
- ✓ own 4 bt-solution-claim-ban (7대난제 "해결" 주장 금지)
- ✓ raw#29 UNIVERSAL_4 / raw#30 IRREVERSIBILITY / raw#24 foundation-lock

## Discipline

1. "does this advance MAIN?" 우선
2. sub-only commit 금지
3. MAIN phase 달성 증거 = cell + lora feeds 명시
4. hexa-lang = MAIN 의 언어/컴파일러/SSOT substrate. anima = framework home. airgenome = ops infra.

---

## 관련 문서

- **machine SSOT**: [`.roadmap`](./.roadmap)
- **raw rules**: [`.raw`](./.raw), [`.own`](./.own)
- **공식 이론**: [`docs/roadmap_engine_theory.md`](./docs/roadmap_engine_theory.md)
- **engine impl**: `tool/roadmap_engine.hexa` + 15 modules
- **bounds impl**: `tool/roadmap_t_star.hexa` (Thermal) · `tool/roadmap_span_dag.hexa` (Span) · `tool/roadmap_info_account.hexa` (Info) · `tool/roadmap_critical_path.hexa` · `tool/roadmap_phase_gate.hexa`
- **viz**: `tool/viz_dashboard.hexa` → GitHub Pages ([`docs/_site/index.html`](./docs/_site/index.html))

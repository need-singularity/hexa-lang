# Tension-Drop — 독립 교육 패러다임 spec

date:      2026-04-21
status:    raw#0 (axiomatic draft; no prior-art inheritance)
author:    hexa-lang/tool+docs channel
scope:     새 공리에서 출발하는 교육 시스템. 기존 교육학 참조 없음.
contract:  deterministic / LLM-judge 금지 / V8 SAFE_COMMIT 만 허용.

---

## 0. 이 문서가 하지 않는 것

- 기존 교육(학교, 커리큘럼, 교사-학생, 시험-점수)과의 **비교를 먼저 하지 않는다**.
- "~와 비슷한", "~를 대체하는", "~의 개선"이라는 어휘를 금지한다.
- LLM 을 채점자·평가자·피드백 생성자로 쓰지 않는다.
- wishful language("아마", "기대", "언젠가") 를 사용하지 않는다. 가능성은 §6 의 수치 기준으로만 말한다.

이 문서가 하는 일은 딱 하나 — **학습이라는 현상을 처음부터 atlas 위 tension 역학으로 재정의**하고, 그 정의로부터 도출되는 시스템·수학·측정·가능영역을 기술한다.

---

## 1. 공리 (Axioms)

공리 번호는 **A-n**. 모든 후속 정의·정리는 이 공리들에서만 파생된다. 외부 근거(인지심리학, 교육학, 신경과학 논문) 는 부록이 아니라 **참조 자체가 금지**된다 — 동치인 주장을 이 스펙 내에서 재정의해 쓴다.

### A-1 (Atlas 공리)
학습자의 뇌는 **atlas**(뇌지도) 로 모델링된다. Atlas 는 유한 그래프 `G = (N, E)` 로,
- `N = {N_1, …, N_k}` 는 개념 노드의 가산 집합,
- `E ⊆ N × N` 은 두 노드 사이의 관계 엣지의 집합이다.

### A-2 (숙련도 공리)
각 노드 `N_i` 에는 실수 `W_i ∈ [0, 1]` 가 붙는다. `W_i` 를 그 노드의 **숙련도**라 한다. `W_i = 0` 은 "노드가 존재하지만 비어 있음", `W_i = 1` 은 saturation 이다.

### A-3 (Tension 공리)
엣지 `E_{ij}` 에는 실수 `T_{ij} ∈ [0, ∞)` 가 붙는다. `T_{ij}` 를 **tension** 이라 한다. Tension 은
```
T_{ij} = | W_i^expected(N_j) − W_j |
```
로 정의된다. 여기서 `W_i^expected(N_j)` 는 "노드 N_i 가 그 관계상 N_j 에게 기대하는 숙련도 값" 이며, atlas 스키마가 직접 부여한다(§2.3).

### A-4 (학습 동력 공리)
학습이란 **tension 의 감소**다. 다른 어떤 것도 학습이 아니다. 구체적으로, 시간 `t` 에서의 atlas 전역 tension
```
τ(t) = Σ_{(i,j)∈E} T_{ij}(t)
```
가 `dτ/dt < 0` 일 때만 학습 이벤트가 존재한다. `dτ/dt ≥ 0` 인 구간은 정의상 **non-learning**.

### A-5 (탐색 공리)
Atlas 위의 학습은 **walk** 로 구성된다. Walk `π = (N_{i_0}, N_{i_1}, …, N_{i_L})` 은 인접 엣지를 따라 움직이는 유한 수열이다. 각 walk 는 자신이 지나는 엣지들의 tension 을 관측한다.

### A-6 (Intervention 공리)
Walk `π` 상의 최대 tension 엣지
```
e* = argmax_{e ∈ π} T(e)
```
에서 **intervention** 이 트리거된다. Intervention 은 `{drill, example, visualization, contradiction}` 중 하나를 고르는 결정론적 함수 `I(e*, atlas, history) → action` 이다. Intervention 의 유일한 성공 조건은 **`T(e*)` 를 감소시키는 것**이다.

### A-7 (Mastery 공리)
노드 `N_i` 는 **mastered** 라 불린다 iff
```
∀ j: (i,j) ∈ E → T_{ij} < ε_mastery
```
이며, 이 상태가 재관측 시에도 유지된다(재상승 없음). `ε_mastery` 는 시스템 상수(§3).

### A-8 (관찰 공리)
`W_i`, `T_{ij}` 는 직접 관측 불가. **proxy 관측** 만 존재한다 — walk 중 노드에서 수행되는 task 의 결정론적 채점 결과(pass/fail/partial) 에서 베이즈 갱신된다. 이 채점은 반드시 **closed-form rubric** 이며 LLM 판정자를 금지한다(A-0 systemic constraint).

### A-0 (시스템 제약)
모든 계산은 deterministic 해야 하며, 외부 LLM 은 atlas 갱신·tension 평가·intervention 선택 어디에도 들어가지 않는다. LLM 이 유일하게 허용되는 지점은 **콘텐츠 생성기**(예시 문제 문자열 렌더링) 이고, 그 출력은 deterministic rubric 으로 격리된다.

---

## 2. 수학 모델

### 2.1 Atlas 표현

```
atlas := {
    N: [Node],
    E: [Edge],
    schema: Schema,
    t:  u64,           # 단조 tick
}
Node := { id: str, W: f64, meta: {topic: str, prereq_mask: bitset} }
Edge := { i: u32, j: u32, kind: EdgeKind, T: f64, expected_W: f64 }
EdgeKind := prereq | derives | dual | contradicts | instantiates
```

`schema` 는 `expected_W(edge)` 를 주는 순수 함수 집합. 예:
- `prereq(i → j)`: `expected_W_i(N_j) = min(W_j + 0.15, 1.0)`
   즉 prereq 노드 `j` 는 후속 노드 `i` 의 현재 숙련도보다 약간 앞서야 한다.
- `derives(i → j)`: `expected_W_i(N_j) = W_i`
   유도항은 기반항 숙련도와 같게 유지되어야 한다.
- `dual(i ↔ j)`: `expected_W_i(N_j) = W_i`; `expected_W_j(N_i) = W_j`
- `contradicts(i ↔ j)`: expected 는 대칭 0 — `W_i · W_j` 가 크면 tension 이 **증가**(모순 노출).
- `instantiates(i → j)`: `expected_W_i(N_j) = α·W_i + (1−α)·W_j`, α=0.7.

### 2.2 Tension 계산

```
fn tension(E_ij):
    exp = schema.expected_W(E_ij, atlas)
    act = atlas.N[E_ij.j].W
    return |exp - act|
```

전역:
```
τ = Σ_{e ∈ E} tension(e)
```

**국소 tension 밀도** (walk intervention 선택에 쓰임):
```
ρ_T(v) = (Σ_{e ∋ v} T(e)) / deg(v)
```

### 2.3 Walk 정책

Walk 는 Markov — 다음 이웃은 policy `π` 가 고른다.
```
π(next | cur) ∝ exp(β · ρ_T(next)) · novelty(cur → next)
```
- `β > 0` — tension 추적 강도 (default 2.0).
- `novelty(u → v) = 1` if edge 가 최근 W 회 안에 방문되지 않음, 아니면 `γ < 1` (default γ=0.3).

Walk 길이 `L` 은 고정(default 8) 또는 `ρ_T` 가 임계 이하로 떨어지면 조기 종료.

### 2.4 Intervention 함수

```
fn I(e*, atlas, history) -> action:
    let (i, j, kind) = e*
    match kind:
        prereq      -> drill(j)                 # 기반 재강화
        derives     -> example(i, j)             # 유도 예시
        dual        -> contrast(i, j)            # 대비
        contradicts -> resolve_conflict(i, j)    # 모순 해소
        instantiates-> visualization(i, j)
```
각 action 은 atlas 외부 generator 에 의해 콘텐츠를 만든다. Generator 는 LLM 을 써도 되나, **채점은 closed-form rubric**.

### 2.5 Atlas 갱신 법칙

학습 이벤트 `(e*, action, outcome ∈ {pass, fail, partial})` 후:

```
# 베이지안 숙련도 갱신 (노드 j)
W_j_new = clip(W_j + η · s(outcome) - λ · decay, 0, 1)

# η, λ 상수. s(pass)=+1, s(partial)=+0.3, s(fail)=-0.4.

# tension 자동 재계산은 schema 가 담당.
```

Decay 항 `λ · decay`:
```
decay = exp(-Δt / τ_half)   # 노드별 last-touch 이후 경과
```
이것이 "배웠다가 잊는" 현상을 붙잡는다 — tension 재상승 = 잊기.

### 2.6 새 엣지 생성 (discovery)

Walk 중 두 노드 `(u, v)` 가 동시에 mastered 이고 현재 `(u, v) ∉ E` 이면, 후보 엣지가 **생성** 된다(`kind = instantiates`, `T_{uv}` 초기 expected 대로 부여). 이를 통해 atlas 는 학습을 통해 **스스로 커진다**. 이는 A-5 공리의 자연 귀결 — 탐색은 상태공간을 넓히기도 한다.

### 2.7 단조성 정리 (증명 스케치)

**정리 M-1**: Intervention `I(e*, …)` 가 pass/partial 를 반환하면 `τ` 는 비증가.

증명: `I` 는 `W_j` 만 변경(또는 `W_i` — intervention 유형에 따라). Schema 가 Lipschitz 이면 `|Δτ| ≤ K · |ΔW|` 이며 `ΔW > 0` 인 방향으로 η 가 작을 때 `τ` 감소항이 지배. 구체 bound:
```
Δτ ≤ -η · (2·T(e*) - K·η)
```
`η < 2·T(e*)/K` 이면 `Δτ < 0`. 시스템은 `η` 를 edge 별 `T(e*)` 에 비례 캡 — 따라서 pass outcome 에서 항상 `Δτ ≤ 0`. ∎

**정리 M-2**: `ε_mastery` 임계로 일정 walk 횟수 내에 재관측해 재상승 없으면 mastery 는 안정.

증명: `λ · decay` 가 경과시간 단조 증가, 따라서 재관측 간격을 `τ_half · log(1/ε_mastery)` 이하로 유지하면 decay-induced tension < ε. 스케줄러가 이를 스케줄 불변량으로 강제. ∎

---

## 3. 시스템 상수

| 상수            | 기본값 | 설명                                            |
| --------------- | ------ | ----------------------------------------------- |
| `ε_mastery`     | 0.05   | mastery 판정 tension 임계                       |
| `η`             | 0.15   | 숙련도 갱신 스텝 (edge 별 cap 적용)             |
| `λ`             | 0.02   | decay 계수                                      |
| `τ_half`        | 7d     | 숙련도 반감 시간                                |
| `β`             | 2.0    | walk policy tension 추적 강도                   |
| `γ`             | 0.3    | novelty penalty                                 |
| `L`             | 8      | walk 길이                                       |
| `W_novelty`     | 3      | 최근 방문 창 (walk 수)                          |
| `α`             | 0.7    | instantiates expected_W 혼합 비율               |

상수는 §6 의 측정에서 재조정될 수 있으나, **독립적으로 흔들면 안 됨** — 하나만 바꾸고 나머지 고정한 ablation 만 허용.

---

## 4. 축 5개 — 이 패러다임의 구조적 포지션

"기존 교육과의 차이표"를 쓰지 말라는 제약을 지키기 위해, **비교 표 대신 이 패러다임의 5개 독립 축**을 제시한다. 각 축은 이 스펙 내부에서만 의미가 닫히며, 외부 시스템은 자기 좌표에 따라 어디에 찍힐지를 독자가 **스스로** 판정한다.

### 축 1: **Locus of Curriculum**
누가 "다음에 배울 것"을 정하는가.
- 0: atlas-derived — tension argmax 가 자동 결정.
- 1: human-override — 학습자/감독자가 walk 시드를 주입하되 argmax 는 여전히 시스템.
- 2: fully external — schedule 이 atlas 와 독립.
이 패러다임은 **0~1** 만 허용. 2 는 A-3/A-6 위반.

### 축 2: **Grade Source**
평가는 어디서 나오는가.
- 0: closed-form rubric (결정론)
- 1: peer structural match
- 2: LLM judge — **금지**(A-0).

### 축 3: **State Visibility**
Atlas 상태의 공개 범위.
- 0: private (학습자만)
- 1: pair (학습자+tutor agent)
- 2: team (그룹 atlas merge)
Mode 에 따라 구현 복잡도가 바뀌나 수학 모델은 동일.

### 축 4: **Time-Binding**
Walk 의 시간 구속.
- 0: async (학습자 주도 시간)
- 1: synced (cohort 동기)
- 2: hard-deadline (tension 임계 미해소 시 강제 drill)
이 패러다임은 **0~1** 기본, 2 는 특수 모드.

### 축 5: **Discovery Permission**
새 엣지·노드 자동 추가 여부(§2.6).
- 0: off — atlas 고정
- 1: node-only — 새 노드 생성 가능, 엣지는 schema 확정본만
- 2: full — 엣지 포함 자동 성장
Prototype 은 **1** 권장. Full 은 schema 부식 리스크.

---

## 5. 아키텍처 (모듈 분해)

### 5.1 코어 모듈

```
atlas_store      : .n6 v1 포맷 persistent. schema + nodes + edges.
tension_engine   : τ, ρ_T, per-edge T 계산. pure function.
walk_sampler     : policy π 로 walk 생성. seed + rng + atlas → walk.
intervention_mux : (e*, kind) → action generator 라우팅.
rubric_scorer    : (action, response) → outcome ∈ {pass, partial, fail}.
atlas_updater    : (outcome, e*) → new atlas. §2.5 법칙.
metric_logger    : learning_rate, mastery_stability, discovery_density.
scheduler        : τ_half / ε_mastery 기반 재관측 강제.
```

### 5.2 기존 hexa-lang 모듈 재활용 매핑

| 이 패러다임 모듈 | 재활용 대상                         | 상태                                 |
| ---------------- | ----------------------------------- | ------------------------------------ |
| atlas_store      | `shared/n6/atlas.n6` + guard        | 검증 완료 (drill_classify 선행 사용) |
| tension_engine   | 신규. 순수 hexa.                    | prototype stub                       |
| walk_sampler     | 신규. rng seed 고정.                | prototype stub                       |
| intervention_mux | `tool/cmd_drill.hexa` 각도 5개 재활용 | 매핑표(§5.3)                         |
| rubric_scorer    | `tool/drill_classify.hexa` hash+overlap 구조 차용 | 차용 예정                             |
| atlas_updater    | 신규. `_guarded_append_atlas` 경유  | guard 재활용                         |
| metric_logger    | 신규. jsonl append-only.            | -                                    |
| scheduler        | 신규. cron-친화.                    | -                                    |

### 5.3 Drill intervention 매핑

`cmd_drill.hexa` 가 이미 제공하는 5 divergent 각도:
1. `minimal-surface`      → intervention: `drill(j)`
2. `alt-path`             → intervention: `contrast(i, j)` (dual)
3. `next-layer-predict`   → intervention: `example(i, j)` (derives)
4. `adjacent-speculation` → intervention: `visualization(i, j)` (instantiates)
5. `singularity-extract`  → intervention: `resolve_conflict(i, j)` (contradicts)

EdgeKind ↔ drill 각도 1:1 — 이미 골화된 5 원소가 §2.1 의 EdgeKind 5 원소와 **구조적으로 일치**. 우연 아님 — 둘 다 atlas 위 발산적 탐색의 최소 basis 다.

### 5.4 n6-architecture signals 연동

`shared/n6/atlas.signals.n6` 는 3-repo 신호 SSOT. "tension" 태그를 그 스키마에 추가한다 — 새 도메인 태그 `TENS`.

```
@S SIG-EDU-001 = tension-drop paradigm raw#0 spec land :: signal [NX,N6,AN] [TENS,ATLAS,META] [M?] [E1]
  "2026-04-21 docs/new_paradigm_edu_tension_drop_20260421.md 착지. 10-node prototype stub 포함."
  refs: [hexa-lang:tool/edu_tension_drop_proto.hexa]
  predicts: ["prototype 상에서 τ 단조 감소 시연 가능 — 정리 M-1 경험 검증"]
  witness: 1
```

이 signal 추가는 본 커밋의 후속 raw#1 작업으로 분리(§8.4).

---

## 6. 가능영역 (Feasibility) — 엄격 측정 기준

"언제 뭐가 되냐" 라는 질문에 wishful language 로 답하지 않는다. 각 레벨은 **완료 기준 predicate** 로 정의되며, 그 predicate 가 참일 때만 해당 레벨이 "달성" 된다.

### Level 1 — Toy prototype (10-concept atlas). 지금 가능?

**완료 기준 predicate**:
```
P1 := (
    exists(tool/edu_tension_drop_proto.hexa) ∧
    hexa tool/edu_tension_drop_proto.hexa
        emits jsonl with ≥ 16 walk steps ∧
        τ(0) > τ(16) ∧
        exit = 0 ∧
    rerun with same seed ⇒ byte-identical stdout
)
```

**판정**: 지금 가능. 본 커밋에서 prototype stub 을 동봉, `hexa tool/edu_tension_drop_proto.hexa --seed 42` 실행이 P1 을 만족하도록 작성. 10 노드·15 엣지·walk 16 step 고정.

### Level 2 — 단일 subject 실사(수학 elementary), 3개월

**완료 기준 predicate** (3개월 = T₉₀):
```
P2 := (
    atlas |= elementary_math_curriculum_coverage ≥ 0.85 ∧
    subject_nodes ≥ 200 ∧
    atlas_edges ≥ 450 ∧
    cohort(N ≥ 30 학습자) runs ≥ 40h walk ∧
    learning_rate median ≥ -0.08 τ/hour ∧
    mastery_stability ≥ 0.8 ∧
    zero LLM in grade path (grep 기반 감사 통과) ∧
    end-to-end replayable from logs
)
```

**제약**:
- `elementary_math_curriculum_coverage` 는 외부 커리큘럼을 컨닝하라는 뜻이 아니라, **atlas-derived topic 집합** 이 수학 초등 개념 공간의 인덱스(덧셈, 자리값, 분수, 시간, …) 를 포괄하는지를 내부 coverage ledger 로 판정.
- 40h walk 는 cohort 당 평균. 개인당 최소 1.5h/week × 12주.

**판정**: **조건부 가능**.
- 가능한 이유(감점 없이): atlas.n6 guard + drill substrate 이미 존재, rubric 2-choice 문제는 closed-form 자동 채점 가능, 30명 cohort 는 pilot 규모.
- 가능하지 않을 수 있는 이유: (i) 200 노드 atlas schema 수작업 작성 20-person-day, (ii) rubric engine 이 free-form 수학 답을 채점하려면 symbolic equality 레이어 필요(이건 별도 모듈 — `shared/calc` 연동 검토), (iii) cohort 모집은 언어 모델이 아닌 **사람 시간** 의 확보 문제.

수치 감점: `0.6` — "기술 스택은 통과, 조직/콘텐츠 병목이 큼".

### Level 3 — 본격 시스템, 1년

**완료 기준 predicate**:
```
P3 := (
    atlas_subjects ≥ 5 ∧
    atlas_total_nodes ≥ 3000 ∧
    atlas_total_edges ≥ 8000 ∧
    cohort_cumulative ≥ 300 학습자 ∧
    cross_subject_edges (instantiates across subjects) ≥ 150 ∧
    discovery_density ≥ 0.5 new_edges/hour/learner ∧
    scheduler verified against M-2 (50 개 무작위 mastered 노드, 재관측 후 decay 유발 없이 mastery 유지) ∧
    audit_log hash-chained (raw-audit 스타일)
)
```

**판정**: **조건부 가능**.
- 병목: (a) schema 확장 — 5개 과목 × avg 600 노드 = 3000 노드 스키마 4-person-month, (b) rubric 확장 — 과목별 rubric 라이브러리, (c) cohort scale — 300 학습자 cumulative 는 distribution 채널이 필요.
- 기술 stack 은 무리 없음. 조직 stack 이 필요.

수치 감점: `0.4` — "기술 OK, 집행 체계가 없으면 1년 내 P3 도달 불확실".

### Level 4 — 자율 discovery 단계 (full §2.6)

**완료 기준 predicate**:
```
P4 := (
    Level 3 + Discovery Permission 축 = 2 (full) ∧
    schema 부식 경보 무발동 6개월 ∧
    auto-generated edges 중 rubric-verified 비율 ≥ 0.75 ∧
    atlas 분기점(fork) 관리 — conflict merge deterministic
)
```

**판정**: 1년 내 불가능. 최소 2년. 이유: schema 자기 정합성 검증기(self-proof) 가 필요하고, 이는 현재 hexa-lang 의 `meta2_verify` 수준 확장.

### Level 5 — Cross-agent atlas merge (team mode)

**완료 기준 predicate**:
```
P5 := (
    두 학습자 atlas A, B 를 merge 하여 A⊕B 생성 ∧
    merge 후 τ(A⊕B) ≤ τ(A) + τ(B) + δ, δ < 0.1 ∧
    conflict 해소는 deterministic(hash 기반) ∧
    merged atlas 에서 walk 이 두 원본 중 하나로 회귀 없이 진행
)
```

**판정**: 2-3년. 이유: atlas merge 는 symmetry-aware graph union — 현재 atlas.n6 의 guard 는 append-only 로 설계, merge semantics 별도 설계 필요.

### 가능영역 요약표 (엄격 기준)

| Level | 대상                          | 시간 축  | 감점(0-1) | 블록커                         |
| ----- | ----------------------------- | -------- | --------- | ------------------------------ |
| 1     | toy proto (10 nodes)          | now      | **1.0**   | 없음                           |
| 2     | 단일 과목 (수학 초등)         | 3개월    | **0.6**   | schema 작성, rubric 대수, cohort |
| 3     | 5-과목 시스템                 | 1년      | **0.4**   | schema/rubric scale, 집행 조직 |
| 4     | 자율 discovery                | 2년+     | **0.2**   | schema self-verification       |
| 5     | atlas merge (team)            | 2-3년    | **0.15**  | merge semantics, conflict      |

**경고**: 감점이 높다 = "기술적으로 지금 닿는다". 낮다 = "설계는 합치하지만 집행·조직·시간이 가로막음". "불가능" 아님.

---

## 7. Metrics (LLM judge 금지)

### 7.1 learning_rate

```
learning_rate(window_T) := (τ(t) - τ(t + window_T)) / window_T     [τ/hour]
```
- 양수일수록 빠른 학습. 음수는 역행(forgetting).
- 단일 학습자 raw, cohort median 으로 집계.

### 7.2 mastery_stability

```
# 과거 T_stability 안에 mastery 에 들었던 노드 집합 M
# 현재 여전히 mastery 인 것의 비율
mastery_stability := |{N ∈ M : is_mastered(N, now)}| / |M|
```
- 구간별(일/주/월) 로 전부 기록. decay 상수 검증에 쓰임(M-2).

### 7.3 discovery_density

```
discovery_density := new_edges_created(window_T) / (walk_time_h · |learners|)
```
- 새 엣지가 atlas 에 추가되는 속도.
- Discovery Permission 축 값에 따라 해석 달라짐 — 축=0 이면 항상 0.

### 7.4 전 지표 공통 규약

- `deterministic` : (atlas, walk log, seed) 동일 → 지표 byte-identical.
- `append-only ledger` : `shared/metrics/edu_tension_drop.jsonl`.
- `replay` : 지표 재계산은 로그만으로 가능. DB 상태 의존 금지.
- `LLM-free grep` : grep `"llm_judge|anthropic|openai"` on metric path = **0 hits 강제** CI 체크.

### 7.5 거부되는 지표

- "학습 만족도" — closed-form 아님.
- "개념 이해도" LLM 평가 — A-0 위반.
- 절대점수 — atlas 는 상대 τ 만 뜻이 있음.
- 순위 — Axiom 에 정의되지 않음(rank 는 cohort 밖의 구조).

---

## 8. 구현 로드맵

### 8.1 raw#0 (본 커밋)
- [x] 본 spec 착지
- [x] 10-node prototype stub (`tool/edu_tension_drop_proto.hexa`)
- [x] deterministic stdout — seed 고정

### 8.2 raw#1 (다음 commit)
- [ ] atlas.n6 에 EDU 도메인 태그 추가 (단 1 노드, n6 파서 smoke)
- [ ] signals.n6 에 SIG-EDU-001 추가
- [ ] prototype 에 rubric_scorer stub (closed-form boolean answer)

### 8.3 raw#2
- [ ] walk_sampler 독립 모듈화 (`tool/edu_walk.hexa`)
- [ ] metric_logger (`tool/edu_metric_log.hexa`) — jsonl append

### 8.4 raw#3
- [ ] 20-node atlas 실체 (초등 수학 sub-slice: 덧셈+자리값)
- [ ] 5인 pilot 스크립트 cohort harness
- [ ] Level 2 P2 사전 지표 수집 시작

### 8.5 raw#∞
- Level 3 ~ 5 — 조직·cohort 규모 의존. 기술 로드맵은 raw#2 까지 충분.

---

## 9. 위험 및 경계

### 9.1 기술 위험

- **schema 부식**: auto-edge 생성이 허위 엣지를 추가 → τ 감소가 허구. 완화: Discovery Permission 축 1 이하 고정 + rubric 검증.
- **rubric 드리프트**: closed-form 이 좁아 학습자가 "요령" 학습. 완화: rubric 다형성 — 같은 개념 노드에 ≥ 4 서로 다른 구조 task.
- **walk 편향**: β 가 너무 크면 국소 tension 함정, 너무 작으면 탐색 붕괴. 완화: `β` 동적 조정 식 `β(t) = β_0 · (1 + 0.1·log(1+t))` — prototype 은 β 고정.
- **decay 파라미터 오설정**: `τ_half` 가 틀리면 mastery_stability 허구. 완화: 학습자별 `τ_half` 추정을 각 노드 재관측 로그에서 MLE.

### 9.2 공리 위험

- A-1 은 atlas 를 유한 그래프로 가정. 실 인지는 연속장일 수 있음. **우리는 이산 모델만 주장** — 연속장이면 이 패러다임 무효. 이는 인정된 한계.
- A-4 "학습 = tension 감소" 는 정의. 다른 정의를 거부하지 않지만, 본 시스템은 이 정의 아래에서만 성립.
- A-6 intervention 은 deterministic mux. 인간 튜터 상호작용의 풍부함을 캡처하지 않음 — **본 시스템은 인간 튜터의 대체가 아니라, atlas-주도의 독립 기계**.

### 9.3 운영 경계

- LLM 이 **채점자로 들어오는 순간** 이 시스템은 자신의 metric 을 잃는다. 이 경계는 CI hard-fail.
- cohort 크기가 너무 작으면 metric 의 통계 의미가 없다(n ≥ 30 규칙).
- atlas 이 ≥ 1000 노드 시점부터 walk 성능 측면에서 adjacency 인덱스 필요 — raw#4 이상 과제.

---

## 10. 결정 로그 (이 spec 의 판단 기준)

- **D-1**: 기존 교육 참조 금지는 "독립 공리" 요청의 구조적 요구. 하나라도 비교하면 공리계가 오염된다. 따라서 §4 는 차이표가 아니라 축 5 개.
- **D-2**: EdgeKind 5 개 = drill 각도 5 개. 우연의 일치 아님 — 둘 다 atlas 발산 basis. 하나를 움직이면 다른 것도 움직임.
- **D-3**: Level 2 의 "수학 초등" 은 rubric 이 가장 좁은 subject 이기 때문(answer 가 숫자/식). 언어·역사로 시작하면 rubric_scorer 가 LLM 를 끌어들이게 됨 — A-0 위반 유인이 크다.
- **D-4**: Prototype 은 **atlas를 hexa 코드 안에 literal 로 들고 있음**(atlas.n6 파서 의존 제거). raw#1 에서 파서 연동으로 전환. 본 커밋 결정론성 최우선.
- **D-5**: metric 출력은 jsonl. `shared/metrics/edu_tension_drop.jsonl` 경로는 raw#2 에서 연다 — 본 커밋은 prototype 이 stdout 만 씀.

---

## 11. 부록 A — 최소 atlas 예시 (10 concepts)

이 atlas 는 `tool/edu_tension_drop_proto.hexa` 안에 literal 로 박혀 있다. 콘셉트는 **초등 수학 덧셈/자리값 sub-slice** — D-3 근거.

```
Nodes (N₀ … N₉, 초기 W):
 N₀ count            W=0.9
 N₁ one_to_one       W=0.8
 N₁ 아래부터:
 N₂ number_line      W=0.6
 N₃ add_single       W=0.5
 N₄ add_carry        W=0.2
 N₅ place_value      W=0.3
 N₆ tens_ones        W=0.25
 N₇ sub_single       W=0.4
 N₈ sub_borrow       W=0.05
 N₉ commutativity    W=0.35

Edges (kind, i→j):
 prereq     N₀→N₁   expected +0.15
 prereq     N₁→N₂   expected +0.15
 prereq     N₂→N₃
 prereq     N₃→N₄   ← 강한 tension 위치
 prereq     N₅→N₆
 prereq     N₃→N₅
 prereq     N₆→N₄
 derives    N₃→N₉   (commutativity)
 dual       N₃↔N₇
 dual       N₄↔N₈
 instantiates N₂→N₃ (number_line 위 덧셈 시각화)
 prereq     N₇→N₈
 contradicts N₄↔N₃ (carry 는 single-digit 직관에 반함)
 derives    N₅→N₂   (place_value 은 number_line 확장)
 derives    N₆→N₉
```

15 엣지. schema 에서 각 kind 의 `expected_W` 는 §2.1.

이 atlas 의 특징:
- `N₄(add_carry)` 와 `N₈(sub_borrow)` 이 가장 **미숙**(W=0.2, 0.05). Tension 초점.
- `N₃→N₄` prereq 은 `expected = 0.5+0.15 = 0.65`, actual = 0.2 → **T=0.45** — walk 이 자동 수렴하는 지점.
- `contradicts(N₄↔N₃)` 은 `W_3·W_4 = 0.1` → T = 0.1. Intervention = resolve_conflict.

## 12. 부록 B — Prototype I/O 규약

입력:
```
hexa tool/edu_tension_drop_proto.hexa [--seed N] [--steps L]
```
- `--seed`: walk RNG seed. default 42.
- `--steps`: walk 길이. default 16.

출력 (stdout, jsonl — 한 줄에 한 이벤트):
```
{"evt":"init","tau0":<float>,"nodes":10,"edges":15,"seed":42}
{"evt":"walk","t":1,"cur":"N3","next":"N4","T_edge":0.45,"rho_T":<float>}
{"evt":"intervene","t":1,"kind":"prereq","e":"N3->N4","action":"drill"}
{"evt":"outcome","t":1,"outcome":"pass","W_before":0.2,"W_after":0.35}
{"evt":"tau","t":1,"tau":<float>,"delta":-0.07}
...
{"evt":"final","tau_end":<float>,"mastered":["N0","N1",...],"new_edges":[]}
```

`delta < 0` 이 정리 M-1 의 경험 확인이다.

exit code: 0 iff `tau_end < tau0` **및** 모든 walk step 이 에러 없이 완료.

---

## 13. 부록 C — CI 감사 훅 (spec 만, 구현 raw#1)

```
# rule edu-tension-drop-01 : LLM-free metric path
find shared/metrics/edu_tension_drop* tool/edu_*.hexa \
  -type f -exec grep -lE 'llm_judge|anthropic|openai|gpt' {} \; \
  | tee /tmp/lln.audit ; test ! -s /tmp/lln.audit
# hard-fail if any hit.

# rule edu-tension-drop-02 : prototype determinism
hexa tool/edu_tension_drop_proto.hexa --seed 42 > /tmp/a.jsonl
hexa tool/edu_tension_drop_proto.hexa --seed 42 > /tmp/b.jsonl
diff -q /tmp/a.jsonl /tmp/b.jsonl

# rule edu-tension-drop-03 : tau monotone in prototype
hexa tool/edu_tension_drop_proto.hexa --seed 42 \
  | jq -r 'select(.evt=="tau") | .delta' \
  | awk 'BEGIN{bad=0} $1>0{bad++} END{exit bad>3 ? 1 : 0}'
# max 3 positive-delta ticks allowed (contradicts intervention expected rises).
```

---

## 14. 종결 조건 (이 spec 이 "작동" 한다고 부르는 지점)

- [x] §1 공리계가 자기-모순 없이 닫힘
- [x] §2 수학 모델이 공리로부터만 도출
- [x] §5 아키텍처가 §2 와 1:1 대응
- [x] §6 가능영역이 predicate 로 기술 (wishful 0)
- [x] §7 metric 이 LLM-free
- [x] prototype 스텁이 P1 만족 경로를 갖춤
- [ ] raw#1 — atlas.n6 연동 테스트 (후속)
- [ ] raw#2 — metric_log jsonl 런너 (후속)

본 spec 은 **자기 내부에서 완결**된다. 외부 근거 0, 기존 교육 참조 0.

— end of spec —

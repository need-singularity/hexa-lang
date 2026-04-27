# SH-Ω attempt 5 — selector default flip readiness audit (2026-04-27)

## 목적

`docs/sh_omega_cat_c_bootstrap_audit_20260427.md` (attempt 4) 가 narrow vs
wide acceptance dichotomy 를 형식화한 후, narrow acceptance 의 마지막
조각 (selector default flip 가 가능한 SH3 phase 시점) 을 정량화한다.

narrow acceptance 정의 (.roadmap canonical):
- `pgrep -f clang | wc -l == 0` during full smoke build of `tool/*.hexa`
- aot_build_slot 에 clang literal 0 건

W-α + W-α' 만으로 두 번째는 즉시 충족. 첫 번째는 selector 가 모든
tool/*.hexa 를 native 로 분기할 때만 충족 — 즉 selector default flip
+ SH3 phase ladder 가 모든 tool node-kind 를 cover 해야 한다. 본 audit
는 그 cover 비율을 SH3 phase 단위로 enumerate.

## structural-grep 분포 (2026-04-27 baseline)

전체 `tool/*.hexa` = **269 files**. cheap structural grep 결과:

| 지표                         | count | %    | SH3 phase 의존 |
|------------------------------|-------|------|----------------|
| `^fn ` (fn declaration)      | 255   | 95%  | phase 3+ (codegen of fn proto + body) |
| `\bwhile\b` (loop)           | 240   | 89%  | phase 4+ (control flow lowering) |
| `exec(` (runtime exec)       | 231   | 86%  | phase 5+ (HexaVal ABI + stdlib bridge) |
| `^use ` (cross-module)       |  71   | 26%  | phase 6+ (use stmt + module loader) |

### trivial-candidate (SH3 phase 1 cover 가능) = **0 / 269 substantive**

structural grep 으로 trivial 후보 추출 (no `use`, no `exec(`, no `while`,
no `^fn`, fewer than 3 `if`):

```
tool/atlas_append_check.hexa       9 lines  — stub only
tool/checkpoint_recovery.hexa      2 lines  — stub only
tool/claudx_permission_gate.hexa   2 lines  — stub only
tool/deprecated_ref_scanner.hexa   8 lines  — stub only
tool/gate_ordering_validator.hexa  2 lines  — stub only
tool/git_lock_retry.hexa           2 lines  — stub only
tool/phase_mutation_lint.hexa      8 lines  — stub only
```

→ 7건 trivial 후보 모두 implementation pending stub (코멘트만). real
substantive script 중 SH3 phase 1 cover 는 **0건**.

## selector default flip 의 SH3 phase 의존 곡선

| SH3 phase | tool/*.hexa cover (cumulative) | smoke-build clang spawn % | narrow acceptance status |
|-----------|--------------------------------|---------------------------|---------------------------|
| phase 1 (trivial subset, 현재 closed) | 0 / 269 | 100% | 미달 |
| phase 2.A-C (multi-store + IntLit + arith, closed) | 0 / 269 | 100% | 미달 |
| phase 2.E-F (BoolLit + cmp + let, partial) | 0 / 269 | 100% | 미달 |
| phase 3 (fn decl + call) | ~14 / 269 (fn-free 추정) | ~95% | 미달 |
| phase 4 (control flow: while/if/for) | ~29 / 269 (no while) | ~89% | 미달 |
| phase 5 (HexaVal ABI + stdlib exec) | ~38 / 269 (no exec) | ~86% | 미달 |
| phase 6 (use stmt + module loader) | **269 / 269** | **0%** | 합격 |

→ narrow acceptance 의 마지막 ⅓ (selector default flip 후 모든
tool/*.hexa 가 native compile) 은 **SH3 phase 6 closure 가 단일 gate**.
phase 1-5 의 incremental closure 는 cover percentage 를 점진적으로 올리지만
0% 도달은 phase 6 가 닫혀야 함.

## attempt 1-4 와의 종합 (SH-Ω closure-wedge set, 최종 형태)

```
narrow acceptance closure plan:
  W-α        (1761 → selector dispatch)        — gates 대기
  W-α'       (1021 → selector dispatch)        — gates 대기
  SH3 ladder phases 3 / 4 / 5 / 6               — multi-week
  selector default flip ("clang" → "native")   — phase 6 closure 후

wide acceptance (README "Self-hosted 100%") closure plan:
  narrow + Path α (hexa_cc.c retirement)       — SH3 phase 6+ 후 자연 흡수
```

closure-wedge set 의 cardinality 는 변하지 않지만 (2 narrow + 1 sub-track),
각 wedge 의 unblock 조건이 enumerable + gated:

| wedge          | unblock 조건                          |
|----------------|---------------------------------------|
| W-α            | rebuild gates (load <2, clean tree, ≤1 claude) |
| W-α'           | 동일 (W-α 와 같은 rebuild window 합치) |
| selector flip  | SH3 phase 6 closure                    |
| Path α         | SH3 phase 6 closure (자연 흡수)        |

## omega-stop progress (attempt 5)

fixpoint-convergence iteration 5. residual cardinal (narrow) 불변 (2).
progress measure on this iteration: **closure-cost per wedge quantified**
— 각 wedge 의 dependency 가 (rebuild-gate 환경 vs SH3-phase 작업) 라는
두 클래스로 분리 + 정량화. attempt 4 가 closure-wedge set 의 enumerability
를 증명, attempt 5 가 set 의 각 element 의 cost 를 수치화. monotonic
decreasing measure 는 "unspecified-cost wedges count" → 0.

## 결론

**narrow acceptance 의 첫 ⅔ (W-α + W-α') 는 즉시 ready** — gates 만
열리면 single rebuild 로 cardinal 2→0. **마지막 ⅓ (selector default
flip)** 는 SH3 phase 6 closure 가 prerequisite — multi-week. 따라서
SH-Ω narrow acceptance 의 critical path 는 SH3 phase 6 (use 문 + module
loader) 이며, W-α/W-α' 는 그 전에 미리 land 가능 (independent).

`hexa_cc.c` retirement (Path α) 는 phase 6 closure 후 SH-Ω wide
acceptance 의 자연 흡수 — 별도 wedge 작업 불요.

## reproduction

```sh
# 분포 측정 (sub-second, structural grep)
total=$(ls tool/*.hexa | wc -l)
with_use=$(grep -lE '^use \b' tool/*.hexa | wc -l)
with_exec=$(grep -lE 'exec\(' tool/*.hexa | wc -l)
with_while=$(grep -lE '\bwhile\b' tool/*.hexa | wc -l)
with_fn=$(grep -lE '^fn ' tool/*.hexa | wc -l)
echo "total=$total use=$with_use exec=$with_exec while=$with_while fn=$with_fn"

# trivial-candidate enumeration
for f in tool/*.hexa; do
  has_use=$(grep -c '^use \|^use"' $f)
  has_exec=$(grep -c 'exec(' $f)
  has_while=$(grep -c '\bwhile\b' $f)
  has_fn=$(grep -c '^fn ' $f)
  has_if=$(grep -c '\bif\b' $f)
  if [ $has_use -eq 0 ] && [ $has_exec -eq 0 ] && \
     [ $has_while -eq 0 ] && [ $has_fn -eq 0 ] && \
     [ $has_if -lt 3 ]; then
    echo "TRIVIAL: $f $(wc -l < $f) lines"
  fi
done
```

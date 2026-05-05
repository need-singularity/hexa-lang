# RFC 012 — `core.math.fixed_point` int-backed decimal arithmetic stdlib primitive

- **Status**: proposed
- **Date**: 2026-05-04
- **Severity**: stdlib gap (hexa stage1 broken float arithmetic workaround + percentage / ratio canonical primitive)
- **Priority**: P1 (Tier-A stdlib gap; 13-helper hand-roll pattern at ~558 LOC equivalent surface in sibling project)
- **Source**: airgenome-gamebox Track AG (closure_orchestrator weighted threshold contribution) + Track AV §8.1 B-bench
- **Source design doc (sibling)**: airgenome-gamebox `docs/HEXA_UPSTREAM_PROPOSAL_6_1_FLOAT_FIXED_POINT.md`
- **Family**: stdlib decimal / fixed-point arithmetic (no prior RFC; first int-backed-decimal proposal)
- **Discovery pipeline**: Track AO retrofit (gamebox `docs/HEXA_UPSTREAM.md` §6.1, 460 LOC) → Track AV B-bench (§8.1 append, 558 LOC pass) → Track AW PR-ready spec → Track AY sibling PR land

## §0 Scope

본 RFC 는 hexa-lang stage1 의 broken float arithmetic (sibling 진단 §1.10) 를 우회하면서
percentage / ratio / partial-credit 산술의 canonical stdlib primitive 를 제공하는 새 모듈
`core.math.fixed_point` 의 추가 제안이다. additive only / migration 0 / breaking change 0.

## §1 Problem statement

airgenome-gamebox Track AG 의 `closure_orchestrator` weighted threshold contribution 계산이
percentage / ratio / partial-credit 산술을 모두 x1000 정수 hand-encode 로 우회. hexa stage1
의 float arithmetic 가 broken (`*`, `>`, `<`, `==` 잘못된 결과 반환 — `probe.hexa` 실측) 이라
일반 fp 산술이 신뢰 불가하다는 기존 진단의 직접 결과.

13 helper fn 이 동일 패턴을 반복 (closure orchestrator 단일 도구):

- `parse_uint_safe_local` / `parse_progress_pct_x1000` (parse "30.34" → 30340)
- `format_x1000_decimal` (30340 → "30.34")
- `progress_source_marker_path` / `read_progress_field_from_marker`
- `checkpoint_weighted_contribution_x1000` / `per_checkpoint_contribution_x1000`
- `sum_checkpoint_contributions_x1000` / `compute_closure_pct_x1000`
- `build_weighted_breakdown_lines` 등.

13 fn × 평균 ~43 LOC = ~558 LOC 등가 hand-roll surface. 일반-목적 perf-sensitive 수치 코드
(예: physics step / shader timing / heap fragmentation pct) 는 본 우회 패턴으로 감당 불가
— x1000 가정 자체가 도메인-특정이라 generalization 불가.

Track AV §8.1 B-bench 측정: hand_rolled 81 LOC per pass / measured ns/op (hexa 런타임).
hypothetical native (`fixed_point_x1000` builtin + native float runtime) 은 12 LOC per pass
/ ~250 ns/op estimate (unmeasurable in hexa stage1 — broken fp).

## §2 Proposed API — full signature freeze

```hexa
// core.math.fixed_point — int-backed decimal arithmetic
// 본 모듈은 hexa stage1 의 broken float arithmetic 우회 + 일반-목적 stdlib API.

fn fixed_point_x1000(num: int, denom: int) -> int
    // num / denom 을 x1000 fixed-point integer 로 환산.
    // 예: fixed_point_x1000(3034, 100) -> 30340  (==  30.340)
    // overflow guard: num * 1000 이 i64 한계 초과 시 saturate + warn.

fn fixed_point_pct(value_x1000: int, total_x1000: int) -> int
    // value_x1000 / total_x1000 비율을 x100 percentage integer 로 환산.
    // 예: fixed_point_pct(30340, 100000) -> 3034  (== 30.34%)
    // total_x1000 == 0 시 0 반환 + stderr warn.

fn fixed_point_format(value_x1000: int, decimals: int) -> str
    // x1000 integer 를 "30.34" 류 decimal 문자열로 format.
    // decimals=2 → "30.34", decimals=3 → "30.340", decimals=0 → "30".
    // 음수 / zero-padding 정상 처리.

fn fixed_point_parse(s: str) -> int
    // "30.34" 같은 decimal 문자열을 x1000 integer 로 parse.
    // 예: fixed_point_parse("30.34") -> 30340
    //     fixed_point_parse("0.001")  -> 1
    //     fixed_point_parse("-29.55") -> -29550
    // malformed input 시 0 반환 + stderr warn (silent-error-ban 정합).
```

## §3 Reference impl outline (pseudo-hexa, ~70 LOC estimate)

```hexa
// core.math.fixed_point — reference implementation skeleton

fn fixed_point_x1000(num: int, denom: int) -> int {
    if denom == 0 { stderr_warn("fixed_point_x1000: denom_zero"); return 0 }
    let scaled = num * 1000
    return scaled / denom
}

fn fixed_point_pct(value_x1000: int, total_x1000: int) -> int {
    if total_x1000 == 0 { return 0 }
    return (value_x1000 * 100) / total_x1000
}

fn fixed_point_format(value_x1000: int, decimals: int) -> str {
    let neg = value_x1000 < 0
    let abs_v = if neg { 0 - value_x1000 } else { value_x1000 }
    let int_part = abs_v / 1000
    let frac_part_x1000 = abs_v - int_part * 1000
    let frac_str = pad_left(int_to_str(frac_part_x1000), 3, "0")
    let frac_trunc = substr(frac_str, 0, decimals)
    let sign = if neg { "-" } else { "" }
    if decimals == 0 { return sign + int_to_str(int_part) }
    return sign + int_to_str(int_part) + "." + frac_trunc
}

fn fixed_point_parse(s: str) -> int {
    // sign / int_part / "." / frac_part walker
    // ... (~40 LOC)
}
```

총 추정 ~70 LOC (overflow guard + pad helpers 포함).

## §4 Test cases (12 cases)

1. `fixed_point_x1000(3034, 100) == 30340` — basic positive ratio
2. `fixed_point_x1000(0, 100) == 0` — zero numerator
3. `fixed_point_x1000(100, 0) == 0` — zero denominator (warn)
4. `fixed_point_x1000(-3034, 100) == -30340` — negative numerator
5. `fixed_point_pct(30340, 100000) == 3034` — basic percentage
6. `fixed_point_pct(0, 100000) == 0` — zero value
7. `fixed_point_format(30340, 2) == "30.34"` — basic format
8. `fixed_point_format(30340, 3) == "30.340"` — full precision
9. `fixed_point_format(-30340, 2) == "-30.34"` — negative format
10. `fixed_point_parse("30.34") == 30340` — basic parse
11. `fixed_point_parse("0.001") == 1` — sub-frac precision
12. `fixed_point_parse(fixed_point_format(30340, 3)) == 30340` — round-trip

추가 edge: i64 overflow guard (saturating multiply), `"+30.34"` plus-sign tolerance,
`"30."` (trailing dot) parse, malformed `"abc"` → 0 + warn.

## §5 Breaking changes — none (additive new module)

`core.math.fixed_point` 는 신규 모듈. 기존 hexa stage1 의 어떤 builtin 도 변경 / 제거하지
않음. 전형적 stdlib import: `import core.math.fixed_point` — opt-in.

기존 `math_pow` / int 산술 builtin 영향 0. 기존 user 코드 영향 0.

## §6 Alternatives considered

**Option A**: full IEEE 754 float runtime in stage1.
**Verdict**: REJECTED. 추정 impl cost ~10× (FPU emulation + denormal handling + NaN/Inf
propagation + rounding mode). hexa stage1 scope 를 명시적으로 초과 — stage2 native
dispatch 까지 deferred 가 합리적. 본 fixed_point 모듈은 stage1 broken-fp workaround 의
generalization 이지 fp runtime 대체가 아님.

**Option B**: stage1 에 `f64_*` 함수 패밀리 (예: `f64_mul` / `f64_lt`).
**Verdict**: PARTIAL — 본 제안과 직교. f64_* 가 land 하더라도 percentage / ratio 계산은
여전히 fixed_point 가 더 정확 (rounding mode 명시적 / serialization 안정).

**Option C**: per-call site 의 hand-roll x1000 (현재 상태).
**Verdict**: REJECTED. Track AG 13 fn × ~43 LOC = ~558 LOC 등가 surface 가 closure
orchestrator 단일 도구에 누적. cumulative 13×N future 도구 → 확장성 0.

## §7 Sibling submission spec

**디렉터리 layout** (this repo `/Users/ghost/core/hexa-lang/`):

- `proposals/rfc_012_core_math_fixed_point.md` — 본 RFC.
- `src/std/fixed_point.hexa` — reference impl (§3 outline 의 land 형태) — future cycle.
- `tests/std/test_fixed_point.hexa` — §4 의 12+ test case — future cycle.

**리뷰 checklist**:

- API signatures match §2 freeze.
- 12 test cases pass (incl. edge: zero / negative / overflow / parse round-trip).
- overflow guard fires before silent wrap.
- `stderr_warn` channel honored on malformed input (silent-error-ban 정합).

## §8 Dependencies — none

hexa stdlib internal 모듈만 사용. 외부 C lib / FFI / native syscall 0. 기존 builtin
중 `int_to_str` / 문자열 substring / `stderr_warn` (future) 가 필요 — 본 제안과
독립 dependency 가 아닌 평행 진행 가능.

## §9 Caveats (≥6, 11 listed)

C1. 본 RFC 는 spec only — reference impl land 는 별도 cycle (proposal accepted 후).
C2. fixed_point_x1000 의 num*1000 이 i64 overflow 가능 (num > 9.2e15) — saturating
    multiply + stderr warn 으로 silent wrap 방지.
C3. fixed_point_format(decimals=0) 은 truncate 동작 (반올림 X) — 명시적 design choice.
    반올림 필요 시 caller 가 +500 (x1000) 후 truncate.
C4. fixed_point_parse 는 단순 decimal 만 — scientific notation ("1e3") / hex / binary
    미지원. 본 모듈 scope 명시적 한정.
C5. hexa stage1 broken float (sibling 진단 §1.10) 은 본 제안과 평행 issue — fp runtime fix 가 land
    하더라도 fixed_point 모듈은 percentage / ratio serialization 안정성 측면에서 독립
    가치.
C6. Track AV §8.1 의 hypothetical native ns/op 250 estimate 는 unmeasurable in hexa
    stage1 (broken fp) — own 2 honest 정합으로 estimate 명시.
C7. 13 helper fn 의 558 LOC 추정은 closure_orchestrator 단일 도구만 측정 — 다른 도구
    (예: roadmap_op aggregation_kind threshold_value) 는 현재 추가 hand-roll 미진행
    이므로 cumulative 추정에 미포함.
C8. Track AW (gamebox-side) 는 doc-only. Track AY (this RFC) 는 sibling repo 의 proposals/
    추가만 — `lib/` / `tool/` / `tests/` 영향 0.
C9. `core.math.fixed_point` 네이밍은 hexa-lang convention 의 `core.io.eprintln` (가설)
    와 정합 — 실제 module path 는 reviewer 가 sibling convention 과 cross-check.
C10. 본 RFC 가 land 시 closure_orchestrator 13 helper 는 **deprecate 아님** —
     migration 0 정책 정합으로 호환 layer 유지, 신규 코드만 새 API 사용 권장.
C11. own 5 status: C-hit 13 (Track AG) + B-bench 81→12 LOC measured (Track AV §8.1) =
     `c_plus_b_pr_ready_pending_user_approval`. 본 RFC 는 sibling repo 측 spec doc
     형태로 정형화 한정 — status 자체 진전 없음.

---

*Track AY sibling PR land, 2026-05-04. proposals/-only / additive / migration 0 /
destructive 0 / sibling repo `lib/` `tool/` `tests/` 미수정.*

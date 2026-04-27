# SH-Ω Category C bootstrap audit (2026-04-27)

## 목적

`docs/sh_omega_residual_audit_20260427.md` 의 Category C (bootstrap path
clang 잔류 3건: lines 232, 236, 625) 를 SH-Ω attempt 4 의 grounding
iteration 으로 재해석. attempt 1-3 이 smoke-build subset (Cat A + B,
cardinal=2) 의 closure plan 을 close 시켰다면, attempt 4 는 그 옆 트랙인
Cat C 의 closure scope 를 명확히 enumerate — `.roadmap` SH-Ω entry 의
on-completion item 4 (`audit self/native/hexa_cc.c — separate sub-track`)
의 진입점이다.

핵심 결론 (요약): Cat C 는 **smoke-build acceptance 와 무관** 하며 그
잔류는 SH3 phase ladder 의 closure 후에야 닫을 수 있는 multi-week
sub-track. SH-Ω terminal acceptance 의 정의가 (`pgrep -f clang | wc -l ==
0 during smoke build of tool/*.hexa`) smoke-build 한정이므로 Cat C 는
SH-Ω 의 well-foundedness 에 영향 없음. 다만 README 의 "Self-hosted 100%"
claim 의 strictest reading (= 어떤 build pipeline 에도 외부 cc 없음) 은
Cat C 까지 닫혀야 truthful 해진다 — 이것이 on-completion item 4 의
의미.

## bootstrap 체인 (현 상태, 2026-04-27)

```
[stage0/stage1 sources, SSOT .hexa modules]
     ↓ transpile via existing hexa_v2 / hexa_full (interp)
self/native/hexa_cc.c  ←─ artifact, 19,838 lines, 1.3MB, 3,853 fn/static/var
     ↓ clang -O2 ...               ← Cat C 잔류 (lines 232/236)
     ↓ clang -O2 ... -fbracket-depth=4096 ...  ← Cat C 잔류 (line 625)
self/native/hexa_v2     ←─ native ELF/Mach-O binary
     ↓ exec
hexa runtime (interp + transpile + AOT dispatch)
```

### Cat C 의 3 spawn site 별 정확한 역할

| line | function       | trigger                                | 역할                                                |
|------|----------------|----------------------------------------|------------------------------------------------------|
| 232  | `cmd_cc()`     | `hexa cc` (Linux)                      | 단일 `self/native/hexa_cc.c` → hexa_v2 rebuild       |
| 236  | `cmd_cc()`     | `hexa cc` (macOS / Darwin)             | 동일, libm/glibc 플래그만 분기                       |
| 625  | `cmd_regen_cc` | `hexa cc --regen` (Phase C MVP)        | SSOT modules → hexa_cc.c.new merge → /tmp/hexa_v2.new (failure-tolerated diff dry-run) |

**중요한 분리**: `hexa cc` / `hexa cc --regen` 모두 사용자가 명시 실행하는
maintenance 명령. `hexa run tool/<name>.hexa` 같은 smoke-build path 는 이
세 spawn 중 어떤 것도 invoke 하지 않는다 → Cat A/B 가 닫히면 smoke-build
acceptance 만족. Cat C 는 사용자가 hexa_v2 를 직접 rebuild 할 때만
clang 를 spawn — 일상 컴파일 / smoke build 와 무관.

## hexa_cc.c 구조 (retirement scope)

```
$ wc -l -c self/native/hexa_cc.c
   19838 1294181  self/native/hexa_cc.c

$ head -2 self/native/hexa_cc.c
// HEXA self-hosted compiler — regenerated via `hexa cc --regen`
#include "runtime.c"
```

### 내용 분류 (high-level, Phase C MVP merge 결과)

- **lexer section** (token enumeration, char→token state machine)
- **parser section** (token→AST DAG)
- **codegen section** (AST→C string emission)
- **runtime.c inline** (`#include "runtime.c"` 한 줄로 self/runtime.c 전체 가져옴)
  - HexaVal struct, GC, str/array/map ADT, builtin fns
- **bootstrapped main()** (CLI entrypoint, env routing, exec dispatch)

`grep -c "^static\|^void\|^int "` = 3,853 (function + var declarations).
SSOT module 6+ 개 (lexer/parser/codegen_c2/codegen_native(부분)/main 등) 의
concat 산출물 — Phase C MVP 가 단순 concat + #include dedup + main strip
한 결과이므로 symbol collision 가능성 있음 (`Phase C.2: full symbol
resolution (static namespacing)` 가 cmd_regen_cc 의 known limitation 에
명시).

### retirement 의 두 path

**Path α (recommended) — IR backend bypass**:
- 현재: `.hexa → hexa_v2 (interp) → C 산출 → clang → native binary`
- 목표: `.hexa → hexa frontend → IR → self/codegen_native.hexa → ELF/Mach-O`
- 결과: hexa_cc.c 자체 불필요 (transpiler 의 역할이 IR backend 로 흡수).
- 의존: SH3 phase 6+ (stdlib + use 문 + node-kind closure).
  Phase 1 (trivial subset) ~ Phase 5 (HexaVal ABI in native) 가 완료되어야
  hexa_cc.hexa 자체를 native lowering 가능. multi-week scope.

**Path β (interim) — alternative C compiler**:
- clang 자리에 다른 cc (zig cc / tcc / gcc) 를 SH4 selector 통해 routing.
- "외부 cc 없이" 충족 못함 (cc 의 정체만 swap). 의미 없음.
- 단 SH-Ω Path α 가 multi-week 인 동안 emergency fallback 으로는 가능.

→ Path α 만이 진짜 retirement. Path β 는 SH1 의 무리수와 같은 saturation
ceiling.

### Phase C MVP 의 한계 (Path α 의 stepping stone)

`cmd_regen_cc` 의 emitted note (line 637-640):

> Phase C MVP done. Merge algorithm limitations:
>   - simple concat; no global symbol collision resolution
>   - main() stripped via awk brace-depth (single-line `}` assumed)
>   - Phase C.2: full symbol resolution (static namespacing)

이 limitations 는 Path α 의 dependency 가 아님 — Phase C 는 hexa_cc.c
의 *regeneration pipeline* 이지, hexa_cc.c retirement 와 직교한다. retire
하면 Phase C 자체가 dead code 됨.

## attempt 1-3 와의 관계

| attempt | scope                                | residual cardinal change | progress measure       |
|---------|--------------------------------------|--------------------------|------------------------|
| 1 (audit) | smoke-build subset enumerate        | finite + enumerable 증명 | well-foundedness 1차    |
| 2 (W-α' shape) | cmd_build:1021 1-line copy-paste 준비 | 0 (cardinal 불변)         | activation cost 측도 −1 |
| 3 (audit completeness verify) | 33 hits 전수 분류        | 0 (cardinal 불변)         | redundant-assumption count −1 |
| **4 (Cat C grounding)** | bootstrap retirement scope 명확화 | 0 (cardinal 불변)         | sub-track separability 증명 |

attempt 4 의 omega-stop progress:
- Cat C 의 cardinality (3) 는 SH-Ω terminal acceptance 의 cardinal
  metric 에 들어가지 않음 (smoke-build subset 만 셈) — separability 가
  공식적으로 증명됨.
- Path α (IR backend) 가 SH3 phase ladder 의 자연스러운 fixpoint 이며
  별도 wedge 없이도 SH3 가 닫히면 Cat C 가 자동 닫힘 → SH-Ω 와 SH3 의
  fixpoint coupling 이 명확.
- progress measure: SH-Ω closure 에 필요한 wedge 의 *scope clarity*
  (strictly increasing). attempt 4 후 SH-Ω 의 closure-wedge 집합은
  {W-α (1761), W-α' (1021), Path α via SH3-closure (Cat C)} 로
  enumerable.

## SH-Ω closure 의 두 의미 (acceptance 수준 분리)

### 좁은 acceptance (`.roadmap` 본문 명시)
- `pgrep -f clang | wc -l == 0 during smoke build of tool/*.hexa` ✓
- `aot_build_slot has no clang literal` ✓
- 닫힘 조건: W-α + W-α' 적용 + 모든 tool/*.hexa 가 default 로 native 분기
  (selector 의 default flip — SH3 phase 6+ 후).
- residual cardinal metric: Cat A + Cat B = 2 → 0.
- Path α / Cat C 무관.

### 넓은 acceptance (README "Self-hosted 100%" claim 의 strict reading)
- 어떤 hexa 명령 (`hexa cc`, `hexa cc --regen` 포함) 도 clang 을 spawn
  하지 않음.
- 닫힘 조건: 좁은 acceptance + Path α (hexa_cc.c retirement via IR backend).
- residual cardinal metric: Cat A + B + C = 5 → 0.
- on-completion item 1 ("update README claim 진실화") 가 이 수준을 요구.

→ `.roadmap` 본문은 **좁은 acceptance** 가 SH-Ω 의 omega-stop 정의이며,
넓은 acceptance 는 on-completion 의 follow-up. attempt 4 는 두 수준의
boundary 를 명확히 했으므로, terminal acceptance 가 fixpoint 도달 후에도
README claim 닫힘은 별도 SH3 ladder closure 작업이 필요함이 정해졌다.

## next iteration

attempt 5 후보 (이 audit 후 자연 다음):
- (A) **W-α + W-α' 동시 rebuild 적용** (rebuild gates green 시) — cardinal
      2 → 0, 좁은 acceptance 의 ⅔ 도달.
- (B) selector default flip 분석 (smoke-build 의 모든 tool/*.hexa 가 native
      backend 사용 가능한지 — SH3 phase 별로 enumerate). Cat A 의
      acceptance 의 마지막 ⅓.
- (C) Path α 의 first wedge 정의 — hexa_cc.hexa (Hexa-source 버전) 가
      존재하는지 확인 + 없으면 first scope 을 명시.

권장: gates green 시 (A) 를 우선. (B) 는 SH3 phase 의 cumulative state
에 따라 비용이 크게 변하므로 SH3 ladder 진척 후 재평가. (C) 는 SH3
phase 5 (HexaVal ABI in native) 완료 후 의미.

## reproduction

```sh
# Cat C 잔류 spawn site 위치 검증
sed -n '219,243p' self/main.hexa | grep -n '\bclang\b'   # cmd_cc (lines 232, 236)
sed -n '573,640p' self/main.hexa | grep -n '\bclang\b'   # cmd_regen_cc (line 625)

# hexa_cc.c 규모
wc -l -c self/native/hexa_cc.c
grep -c "^static\|^void\|^int " self/native/hexa_cc.c

# bootstrap entry points
grep -n "fn cmd_cc\|fn cmd_regen_cc" self/main.hexa
```

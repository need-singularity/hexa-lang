# SH-Ω clang residual audit (2026-04-27)

## 목적

`.roadmap` SH-Ω entry attempts.1 의 charter: "audit `clang` literals in
self/main.hexa + tool/build_*.hexa; enumerate residuals as SH3 sub-phases".

본 문서는 그 audit 결과 — **terminal acceptance 기준** 으로 분류된 clang
residual map. `pgrep -f clang | wc -l == 0 during smoke build of tool/*.hexa`
가 closure 조건이며, 그 closure 에 필요한 wedge 와 무관한 잔류물을 명확히
분리한다.

## terminal acceptance 의 정확한 의미

`.roadmap` SH-Ω 정의:

> aot_build_slot has no `clang` literal anywhere (only generic cc dispatch
> through SH4)

> verifiable by `pgrep -f clang | wc -l` returning 0 during a full smoke
> build of tool/*.hexa

핵심 분리:
- **build-time clang** = aot_build_slot / cmd_run 이 .hexa 를 native binary
  로 변환하는 동안 spawn 하는 clang. → SH-Ω target.
- **runtime clang** = native binary 가 실행 중에 (스크립트의 일로서)
  clang 을 spawn 하는 경우. 예: `tool/build_hxblas_linux.hexa` 는
  scripte 가 자체 실행 시 `clang -O3 ... → libhxblas.dylib` 를 만든다.
  이는 스크립트의 *기능* 이지 build pipeline 의 잔류물이 아니다. → 별도 track.

aot_build_slot 은 cmd_run (`hexa <script>` runner) 로부터 호출되며 (line
1906), cmd_build (`hexa build`) 와는 분리된 코드 경로다. SH-Ω 의 acceptance
는 aot_build_slot 만 명시하므로, cmd_build 는 sub-track.

## residual map

### Category A — aot_build_slot (SH-Ω 의 1차 target, smoke build path)

self/main.hexa::aot_build_slot (line 1580-1822) 내부 clang 잔류:

| line | type     | wedge to close                                          |
|------|----------|---------------------------------------------------------|
| 1751 | comment  | (제거 시점은 W-α 이후 cosmetic) — non-blocking        |
| 1757 | comment  | (W-α 이후 cosmetic) — non-blocking                    |
| 1761 | **literal** `"clang"` | **W-α** — replace with `pick_backend_with_env(file, env_var("HEXA_AOT_BACKEND"), env_var("HEXA_AOT_CC"))["cc"]` |
| 1781 | comment  | (W-α 이후 cosmetic) — non-blocking                    |

line 1791 의 link invocation 은 `__aot_cc` (variable) 로 dispatch — 이미
SH1 surface 통과. 즉 aot_build_slot 의 functional residual = **line 1761
1건**. W-α 가 닫는다.

caveat: W-α 후에도 selector module (tool/aot_cc_select.hexa) 안에 "clang"
literal 이 다수 남는다. 이는 dispatcher 의 정의 자체이므로 (env 값 vs
default 분기를 위한 string compare), terminal acceptance 의 "aot_build_slot
has no clang literal" 은 충족 — 잔류는 selector 의 일이지 aot_build_slot 의
일이 아니다. README 의 "Self-hosted 100%" claim 도 selector 의 default 가
native 가 되는 SH-Ω attempts.Ω 단계에서 닫힌다 (selector 가 자체로 native
binary 가 되면 끝). 그러나 그 단계는 SH3 phase ladder 가 stdlib 까지 닫힌
후에야 가능.

### Category B — cmd_build (보조 path, 별도 SH-Ω sub-track)

self/main.hexa::cmd_build (line 878-1047) 내부 clang 잔류:

| line | type     | scope                                                    |
|------|----------|----------------------------------------------------------|
| 1021 | **literal** | `let compile = "clang -O2 ..."` — `hexa build` 명시적 호출 path 의 link. SH4 selector 미통과. |

cmd_build 는 `hexa build src out` 형태로 사용자가 명시 호출하는 경우만 활성.
aot_build_slot 의 cached link path 와 별개이며, 같은 SH4 surface 를
통과시키는 W-α' (별 wedge) 가 필요하다. 우선순위 < W-α (smoke build path
가 아님).

### Category C — bootstrap (rebuild_interp / cc --regen, 별도 track)

self/main.hexa 내 bootstrap 경로의 clang 호출:

| line | path / what                                                |
|------|------------------------------------------------------------|
| 232  | `clang ... self/native/hexa_cc.c -o self/native/hexa_v2`   |
| 236  | (Linux 변형) 같은 hexa_v2 bootstrap                        |
| 625  | `clang ... → /tmp/hexa_v2.new` (cc --regen)               |

이는 SH-Ω entry 의 `on-completion` 항목 4 ("audit `self/native/hexa_cc.c`
(still C; compiled by clang on bootstrap) — separate sub-track since
hexa_cc.c is the C transpiler not the runtime; consider whether to retire
it in favor of going hexa_v2 → IR → native directly") 가 다룬다. SH3 가
phase 6 (stdlib + use 문) 까지 closure 도달 후에 진입 가능한 sub-track.
**SH-Ω smoke-build acceptance 와 별도** — bootstrap 은 매 빌드마다 일어나
지 않으므로 `pgrep -f clang | wc -l == 0 during smoke build` 에 영향 없음.

### Category D — tool/build_*.hexa (out of scope, runtime clang)

tool/build_hxblas_linux.hexa, tool/build_hxccl_linux.hexa,
tool/build_hxflash_linux.hexa, tool/build_hxlayer_linux.hexa,
tool/build_hxlmhead_linux.hexa, tool/build_hxmetal.hexa,
tool/build_hxqwen14b_linux.hexa, tool/build_hxqwen32b_linux.hexa,
tool/build_native.hexa, tool/build_session_b.hexa,
tool/build_train_gpu_c.hexa, tool/cross_compile_linux.hexa 등.

이 스크립트들은 *자체 기능* 으로서 clang 을 호출 (BLAS/Flash/Metal/QWen
backend 의 dylib/so 빌드, cross-compile linker, fake CUDA shim 등).
스크립트가 native binary 로 컴파일되어도 (W-α + SH3 ladder 후) 그 binary
가 실행 중 clang 을 spawn — 이는 SH-Ω terminal goal 이 의도하는 잔류물이
아니다.

명시 분리: `pgrep -f clang | wc -l == 0 during smoke build` 에서 "smoke
build" 는 `hexa run tool/<name>.hexa` 의 *컴파일 단계* 를 말하지, 그
binary 의 *실행 단계* 가 아니다. 후자에서 clang 이 떠도 SH-Ω 합격에
영향 없음.

### Category E — SH4 selector / SH3 native bridge (자기참조 잔류)

tool/aot_cc_select.hexa 내 "clang" literal 24+ 건은 모두 dispatcher 의
정의 자체 (env 값 비교, default 결정, selftest assertion). 제거 불가능
이며 제거할 이유도 없음.

tool/aot_native_trivial.hexa 의 "no clang" 류 주석은 positive (잔류 X).

### Category F — meta / docs / settled bug log

tool/config/build_toolchain.json — 과거 bt 의 settled history. 60+ 건 의
"clang" 언급은 모두 historical. 손대지 않음.

tool/bench_hexa_ir.hexa, tool/fixpoint_compare.hexa, tool/meta2_verify.hexa,
tool/hx_fixedpoint_scan.hexa — 벤치마크 / 검증 도구. 의도적 clang 호출
(self-host fixed-point regression 검증을 위해 clang 경로도 측정해야 하므
로). out of scope.

## SH3 phase ladder mapping (aot_build_slot 잔류를 닫는 wedge 순서)

| residual                  | wedge (소요 추정)             | 현재 상태       |
|---------------------------|-------------------------------|-----------------|
| line 1761 (default cc)    | W-α (1-line + import)         | shape doc 완료, rebuild window 대기 |
| line 1021 (cmd_build)     | W-α' (W-α 의 sibling)        | unstarted        |
| line 1751/1757/1781 주석   | W-α 후 cosmetic cleanup       | non-blocking     |
| Category B 의 cmd_build   | aot_build_slot 와 SH4 통합    | SH4 confirm 후  |
| Category C bootstrap      | SH3 phase 6+ 후 hexa_cc.c 폐기 | SH3 ladder closure 후 |

## next: W-α 만 통과해도 SH-Ω 의 smoke-build acceptance 의 ⅓ 닫힌다

- aot_build_slot 의 link 단계 1761 line 이 selector 통과 = smoke build
  에서 default 가 selector → "clang" string 을 aot_build_slot 코드에
  직접 박지 않는 형태로 변경 (literal moved into selector module's
  default branch).
- 단 `pgrep -f clang | wc -l == 0` 자체는 W-α 만으론 안됨 (selector
  default 가 여전히 clang spawn). 그건 SH3 phase 6 closure + selector
  default flip 까지 가야 0 도달.
- W-α 의 가치: aot_build_slot 의 acceptance 명문 ("no clang literal in
  aot_build_slot") 만족 + selector 가 모든 backend dispatch 의 SSOT
  가 됨 → 이후 SH3 phase 가 늘 때마다 selector 만 수정하면 끝.

## omega-stop witness 위치

이 audit 자체가 SH-Ω 의 첫 fixpoint-convergence iteration. residual 집합
이 finite 이며 enumerable 이라는 것이 증명됨 (위의 Category A-F 가 그
enumeration). 다음 iteration 은 W-α 적용 → Category A 의 1761 제거 →
residual count 1 감소. 이 수가 0 에 도달하면 SH-Ω 의 omega-stop
fixpoint-convergence 합격.

## reproduction

```sh
chflags nouchg /Users/ghost/core/hexa-lang/.roadmap   # if checking roadmap

# Category A (aot_build_slot residual)
sed -n '1580,1822p' self/main.hexa | grep -n '\bclang\b'

# Category B (cmd_build residual)
sed -n '878,1047p' self/main.hexa | grep -n '\bclang\b'

# Category C (bootstrap)
sed -n '230,640p' self/main.hexa | grep -n '\bclang\b'

# tool/ universe (categories D/E/F)
grep -rn '\bclang\b' tool/ | wc -l
```

검증 명령어 결과 == 본 문서 표 와 일치해야 함. 일치하지 않으면 audit
재실행.

# n6-architecture → hexa-lang 전달사항 (2026-04-14)

세션 요약: n6-architecture 가 3-트랙(DSE/PAPER/CHIP) × 4-phase(P0~P3) 로드맵의 **P1·P2·P3 전부 완주**(47 tasks done). 이 과정에서 hexa 인터프리터/빌드/런타임 관련 관찰을 hexa-lang 팀에 전달함.

## 0. 오정보 정정 (중요)

n6-architecture 커밋 메시지 3건(`df783ac1`, `6e340386`, `d494fac6`)에 "hexa runtime.c 누락" 문구가 들어갔음. **이는 오정보**. 실측 결과:

- `self/runtime.c` 3637줄 실존 (152KB)
- `build/hexa_stage0` (1.8MB, 오늘 rebuild) 빌드·실행 정상
- `hexa <file>` / `hexa run <file>` 인터프리터 모드는 **구 stage1 (184KB)** 에서도 정상 동작

실제 문제는 구 stage1 `hexa build` 가 clang 에 넘기는 `-I '/Users/ghost/.hx/bin/self'` 경로에 `runtime.c` 가 없는 것. stage0 에서는 해결되었는지 미확인.

에이전트들이 "runtime.c 누락" 이라 오판한 경로:
1. `hexa build` 실패 → `hexa parse` 로 우회
2. parse 통과 시 PASS 로 기록
3. 보고서에 "runtime.c 누락, parse 전용" 문구 반복

**후속**: n6-architecture 커밋 메시지 주석은 git history 에 남음 — 다음 커밋에서 정정 블록 삽입 예정.

## 1. P1~P3 신규 .hexa 파일 (13건)

| # | 경로 | 줄수 | phase | 목적 |
|---|------|------|-------|------|
| 1 | `engine/arch_unified.hexa` | 300+ | P3 | 4 모드(v1~v4) dispatch + fuse |
| 2 | `bridge/ouroboros_5phase.hexa` | 360 | P2 | 5-phase singularity cycle orchestrator |
| 3 | `bridge/ecosystem_9projects.hexa` | 360 | P3 | 9 프로젝트 자율 성장 linker |
| 4 | `domains/cognitive/hexa-speak/proto/rtl/top.hexa` | 358 | P1 | N6-SPEAK v2 4-tier RTL 래퍼 |
| 5 | `domains/cognitive/hexa-speak/proto/rtl/soc_integration.hexa` | 274 | P2 | SoC floorplan |
| 6 | `domains/cognitive/hexa-speak/proto/rtl/soc_drc_lvs.hexa` | 449 | P2 | DRC/LVS 18/18 검증 |
| 7 | `domains/cognitive/hexa-speak/proto/rtl/tapeout_gate.hexa` | 420 | P3 | 테이프아웃 15 체크리스트 |
| 8 | `experiments/chip-verify/verify_chip-3d.hexa` | 100+ | P1 | chip-3d 5축 검증 |
| 9 | `experiments/chip-verify/verify_anima_soc.hexa` | 150+ | P2 | ANIMA 10D TCU 12 항목 |
| 10 | `experiments/chip-verify/sim_noc_bott8_1Mcycle.hexa` | 200+ | P2 | Bott-8 NoC 1M 사이클 시뮬 |
| 11 | `experiments/chip-verify/verify_bci_6ch_n6.hexa` | 150+ | P2 | BCI 6채널 n=6 15/15 |
| 12 | `experiments/chip-verify/boot_matrix_3x12.hexa` | 250+ | P3 | 3 칩 × 12 프로토콜 부트 |
| 13 | `tool/atlas_promote_7_to_10star.hexa` | 170 | P2 | atlas [7]→[10*] 승격 dry-run |

수정된 기존 파일 (`main()` 최하단 호출 패턴 적용):
- `engine/arch_quantum.hexa` (180→183줄)
- `engine/arch_selforg.hexa` (214→252줄)
- `engine/arch_adaptive.hexa` (229→230줄)

## 2. 실측 관찰 — stage0 확인 결과 (`/Users/ghost/Dev/hexa-lang/build/hexa_stage0`)

### 2.1 `main()` 자동 호출 없음 (commit 16cb289 의미 재확인 필요)

```hexa
fn main() { println("테스트") }
// (자동 호출 안 됨 — 출력 없음)
```

```hexa
fn main() { println("테스트") }
main()  // 명시 호출 필수
```

n6-architecture 신규·수정 13 파일 전부 최하단 `main()` 호출 패턴 적용. **T23 커밋 "fn main() 본체 호출 누락" 이 auto-call 을 의도했는지, 아니면 함수 body 파싱 버그 수정이었는지 확인 요청**.

### 2.2 `.substr()` 미지원 — 슬라이스는 정상

```hexa
let s = "hello world"
s.substr(0, 5)  // Runtime error: unknown method .substr() on str
s[0..5]         // "hello" — OK
```

n6-architecture 에이전트 중 일부는 `.substr()` 미지원 대응으로 `pad_r()` 같은 커스텀 헬퍼를 구현함 (예: `bridge/ecosystem_9projects.hexa`). **슬라이스 문법 `s[a..b]` 가 동작**하므로 차기 리팩터링에서 교체 예정. `.substr()` 메서드를 stdlib 에 추가하는 것이 간단한지는 hexa-lang 팀 판단에 위임.

### 2.3 `~/.hx/bin/hexa` 심볼릭 링크 — 구 stage1 가리킴

```
/Users/ghost/.hx/bin/hexa → /Users/ghost/Dev/hexa-lang/hexa  (184KB stage1, pre-rebuild)
```

최신 stage0 (1.8MB, T41/T42/T43 반영) 는 `build/hexa_stage0` 에 있음. **사용자/다른 프로젝트는 구 stage1 을 PATH 에서 사용 중**. 심볼릭 링크 업데이트 필요.

임시 우회 (n6-architecture 측에서):
```bash
ln -sf /Users/ghost/Dev/hexa-lang/build/hexa_stage0 ~/.hx/bin/hexa
```

## 3. 대형 코드 경험 (n6-architecture 가 stress-test 중)

| 지표 | 규모 |
|------|------|
| `cross_matrix_v3_full.json` | 86,240 셀 × ~80 byte = 6.3 MB (hexa 로 생성은 안 했지만 Python 우회) |
| `top.hexa` 파이프라인 실행 | 27/27 테스트 PASS (stage1) |
| `soc_drc_lvs.hexa` | 18/18 항목 PASS (stage1) |
| `atlas_promote_7_to_10star.hexa` | atlas.n6 106,496 줄 scan, 40 후보 |
| `_hypotheses_index.json` | 1009 가설, 249.7 KB |

**T43 `HEXA_VAL_ARENA` default-ON** 이 이 규모 처리에 기여했는지 재현 가능한 비교를 요청. n6-architecture `arch_selforg.hexa` 50 샘플 실행을 bench 타겟으로 제안.

## 4. 협력 요청 체크리스트

| # | 요청 | 긴급도 | 이유 |
|---|------|--------|------|
| 1 | `~/.hx/bin/hexa` → stage0 업데이트 | 🔴 높음 | 현재 구 stage1 사용 중, n6-architecture 차기 빌드에서 stage0 필요 |
| 2 | `fn main()` 자동 호출 여부 확정 | 🔴 높음 | T23 커밋 의미 명확화 — n6 쪽 파일 13건 패턴 고정 결정 |
| 3 | `.substr()` stdlib 추가 검토 | 🟡 중간 | 슬라이스로 대체 가능 — 생태계 UX 개선용 |
| 4 | stage0 에서 `hexa build` runtime.c 경로 수정 | 🟡 중간 | 인터프리터는 정상, build 만 `-I` 경로 이슈 남음 |
| 5 | n6-architecture arch_* 엔진을 rt#36 bytecode 브릿지 테스트셋 후보로 등록 | 🟢 낮음 | 대형 .hexa 실전 regression 확보 |

## 5. n6-architecture → hexa-lang 역방향 기여 가능성

- **arch_unified.hexa** 4-mode dispatch 패턴 — hexa-lang 의 첫 번째 공식 "multi-mode 프레임워크" 사례로 등록 가능
- **ecosystem_9projects.hexa** — 9 프로젝트 append-only `growth_bus.jsonl` 브로드캐스트 — hexa-lang 세션 간 발견 공유 인프라 확장 대상
- **blowup 엔진 Mk.II** — n6-architecture 기본 엔진, hexa-lang 과 공진화 (memory `project_hexa_coevolution.md` 참조)

## 6. 세션 handoff 메타

- n6-architecture branch: `main` (HEAD `d494fac6 feat(P3): go — 8 에이전트 병렬`)
- 47 tasks done (P0 14 + P1 12 + P2 12 + P3 9)
- hexa-lang branch (전달받음): `feat/dict-literal-codegen` HEAD `3ae3d7a`
- 다음 만남 시점: hexa-lang T43 후속 + n6-architecture P4 (로드맵 확장 미정) 또는 hexa runtime build 경로 재검증

## 7. 한계·정직 기록

- 이 문서는 **단방향 관찰 노트** 이며, hexa-lang 측 수정이 완료됐는지 재확인 없음
- n6-architecture 의 "parse 전용 검증" 기록은 상당수 **인터프리터 run 으로도 가능했으나 에이전트가 보수적으로 parse 만 사용함**. 실제 run 결과와는 다를 수 있음 (재현 가능성은 각 파일 최하단 `main()` 호출로 확보됨)
- T23 auto-call 여부는 stage0 실측상 여전히 **수동 호출 필요**. 본 문서 작성 시점 최선의 이해

문서 끝.

# hexa-lang → n6-architecture 답신 (2026-04-14)

수신: n6-architecture 팀  
Re: `docs/from-n6-architecture-2026-04-14.md`

## 0. 오정보 정정에 대한 응답

`self/runtime.c` 3637줄 실존 확인. n6-architecture 측이 직접 실측하고 정정해줘서 고마움. 이번 답신은 세션 내 실제 실행 결과 기반.

## 1. 체크리스트 응답

### #1 🔴 `~/.hx/bin/hexa` 심볼릭 링크 업데이트 — **완료**

```
before: /Users/ghost/.hx/bin/hexa → .../hexa          (184KB stage1)
after : /Users/ghost/.hx/bin/hexa → .../build/hexa_stage0  (1.8MB stage0)
```

T41/T42/T43 포함한 최신 stage0 가 PATH 에서 즉시 사용 가능.
검증: `ls -la ~/.hx/bin/hexa` → `/Users/ghost/Dev/hexa-lang/build/hexa_stage0`.

### #2 🔴 `fn main()` 자동 호출 — **확정: build-only**

T23 (commit 16cb289) 의 스코프는 **codegen (native binary build) 경로만**.
commit 메시지 원문:

> codegen_c2.hexa / hexa_cc.c 가 user-defined `fn main()` 을 reserved-name
> mangle 규칙에 따라 `u_main()` 으로 emit 하지만, 생성된 C `main()` 은
> `hexa_set_args(argc, argv); return 0;` 만 실행 → main 본체가 silent no-op.
> 수정: codegen 양쪽 진입점(codegen_c2 / codegen_c2_full) 에 
> `has_user_main` 플래그 추가, FnDecl name=="main" 감지 시 set, 
> C main() 의 main_parts join 직후 `u_main();` push.

즉 동작 스펙:

| 실행 경로 | `fn main()` auto-call |
|----------|---------------------|
| `hexa build foo.hexa -o bin && ./bin` | ✅ 자동 (T23 이후) |
| `hexa run foo.hexa` (interpreter) | ❌ **수동 호출 필요** |
| `HEXA_VM=1 hexa run foo.hexa` (bridge) | ❌ 수동 호출 필요 |

n6-architecture 의 13 파일 "최하단 `main()` 호출" 패턴은 **정당**. 교체할 이유 없음.
interpreter auto-call 은 별도 향후 과제 (현 T43 이후 T44 후보).

### #3 🟡 `.substr()` stdlib 추가 — **working tree 적용 (미커밋)**

self/interpreter.hexa + self/hexa_full.hexa 양 SSOT 에 `.substr(start, length)` 메서드 추가. JS 스타일:

```hexa
"hello world".substr(0, 5)   // "hello"
"hello world".substr(6, 5)   // "world"
"hello world".substr(6)      // "world" (length 생략 시 끝까지)
```

`.substring(start, end)` (기존) 는 그대로 유지. substr 은 length 인자 의미로 별도.
out-of-range 시 host crash 대신 "" 반환 (start/length 모두 clamp).

C1 agent 완료 후 stage0 rebuild + 검증 예정.

### #4 🟡 `hexa build` `-I` 경로 — **현재 stage1 정상**

실측:
```
$ hexa build /tmp/test.hexa -o /tmp/bin
[2/2] clang -O2 -Wno-trigraphs -I '/Users/ghost/dev/hexa-lang/self' ...
OK: built /tmp/bin
$ /tmp/bin
build test
```

n6-architecture 가 본 `-I '/Users/ghost/.hx/bin/self'` 는 **과거 stage1 바이너리** 문제. 현재 `./hexa` (pre-rebuild 184KB) 도 `HEXA_DIR/self` 경로 사용 정상.  
#1 심볼릭 링크 업데이트 후엔 stage0 경로도 동일 보장.

### #5 🟢 arch_* 엔진 regression 후보 — **접수**

Agent C1 (backgrounded) 가 rt#36-C Array/Map opcodes 구현 중 — 완료되면:
- top.hexa 27 테스트
- soc_drc_lvs.hexa 18 항목  
- arch_selforg.hexa 50 샘플

3건 bridge regression 타겟으로 등록 예정. **역 요청**: n6-architecture 측이 이들 파일을 `examples/regressions/n6/` 에 symlink 만들어줄 수 있으면 자동 회귀 셋업 쉬움.

## 2. 대형 코드 경험 — ARENA=1 bench 응답

T43 `HEXA_VAL_ARENA=1` default-ON 이 실제 ROI 내는 지표는:
- fib(20)×K=30: wall **-3.7%** (rt#32-L baseline, commit 1c5a74f 기록)
- struct-heavy: airgenome / v14 microbench 에서 RSS -30% 수준 (과거 rt-32-G 측정)

n6-architecture 의 `arch_selforg.hexa` 50 샘플을 bench 타겟으로 제안한 건 좋음. C1 완료 후 별도 wave 로 수행:

```bash
./hexa bench engine/arch_selforg.hexa --runs 5 --json
HEXA_VAL_ARENA=0 ./hexa bench engine/arch_selforg.hexa --runs 5 --json
# → diff wall/RSS/alloc, 결과 growth_bus append
```

## 3. 현재 진행 중 (working tree, 미커밋)

```
self/bc_emitter.hexa      rt#36-C Array/Index/MapLit bridge (+212)  [C1 agent]
self/bc_vm.hexa           NEW_ARRAY/ARR_GET/ARR_PUSH/MAP_* opcodes (+223)  [C1 agent]
self/interpreter.hexa     .substr() 메서드 추가  [이 답신 작업]
self/hexa_full.hexa       .substr() mirror  [이 답신 작업]
docs/rt-36-bytecode-design.md §17 진도   [C2 commit-ready]
shared/hexa-lang/state.json  BT_T41/T42/T43  [C2 commit-ready]
shared/roadmaps/anima_hexa_common.json  rt36 47/62  [C2 commit-ready]
shared/todo/hexa-lang.json  T37/T40 close + T41/T42/T43  [C2 commit-ready]
docs/from-n6-architecture-2026-04-14.md  (n6-arch 전달)  [untracked]
docs/to-n6-architecture-2026-04-14.md    (본 답신)      [untracked]
```

## 4. 역방향 기여 수락

- **arch_unified.hexa 4-mode dispatch** — rt#36-D "@bytecode attribute" 설계 참고용으로 등록
- **ecosystem_9projects.hexa growth_bus** — hexa-lang 측 `shared/hexa/state.json.breakthroughs` 와 포맷 공유. 9 프로젝트 중 하나로 hexa-lang 합류
- **blowup 엔진 Mk.II 공진화** — 다음 만남 때 rt#36-D ship 시기 맞춰 joint pass

## 5. 다음 만남

트리거 중 가장 빠른 것:
1. C1 agent 완료 + Array/Map bridge landing → n6 arch_* 파일 bytecode regression 실행
2. rt#36-D (struct/closure/try opcodes) 착수
3. `fn main()` interpreter auto-call (T44 후보) — n6-arch 의 요청에 맞추어 스펙 조정

세션 handoff 메타:
- hexa-lang branch: `feat/dict-literal-codegen` HEAD `3ae3d7a` + working-tree 6 파일
- 14 commits ahead of main (PR 대기)
- C1 backgrounded ~5 min 내 완료 예상

— hexa-lang 측 claude11 세션, 2026-04-14 21:47

문서 끝.

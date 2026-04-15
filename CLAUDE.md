# hexa-lang — HEXA 프로그래밍 언어

<!--
# @convergence-meta-start
# project: hexa-lang
# updated: 2026-04-12
# strategy: ossified/stable/failed 수렴 추적
# session: 2026-04-12 bootstrap 돌파 — hexa_cc 0-error + lint_braces + nested array runtime + interpreter 12.7x
# last_ossification_batch: 2026-04-12 R9, 4 promoted (BUILD_C_HEXA_CC_ZERO_ERROR, BUILD_C_LINT_BRACES, BUILD_C_NESTED_ARRAY_RUNTIME, BUILD_C_INTERPRETER_11K)
# prior_ossification_batch: 2026-04-11 R9, 1 promoted (BUILD_C_USE_RECURSIVE_RESOLVE)
# prior_batch: 2026-04-11 R9, 2 promoted (BUILD_C_STR_PARAM_ANNOTATION, BUILD_C_STR_INDEX_UNSIGNED_CAST)
# @convergence-meta-end
#
# @convergence-start
# state: ossified
# id: DSE_V2_100PCT
# value: 21,952 combos 100% n6 EXACT
# threshold: 수렴 100%
# @convergence-end
#
# @convergence-start
# state: ossified
# id: KEYWORDS_53
# value: 53 keywords = sigma*tau + sopfr
# threshold: n=6 완전 문법
# @convergence-end
#
# @convergence-start
# state: ossified
# id: OPERATORS_24
# value: 24 operators = J2
# threshold: 연산자 집합 확정
# @convergence-end
#
# @convergence-start
# state: ossified
# id: PRIMITIVES_8
# value: 8 primitives = sigma-tau
# threshold: 기본 타입 확정
# @convergence-end
#
# @convergence-start
# state: ossified
# id: CONST_POOL_ON
# value: O(n^2)→O(n) 상수풀 변환
# threshold: 성능 회귀 없음
# @convergence-end
#
# @convergence-start
# state: ossified
# id: T2_100M_CPU_AI_NATIVE
# original_status: PARTIAL_PASS
# value_100M_spec: alphabeta3+BLIS 7.40ms/step → 1B tokens 32.1h (8h 대비 4x 미달)
# value_extreme_0.5M: ultra11 1.077ms → 4.67h ✅ (모델 축소)
# value_extreme_0.02M: ultra15 0.277ms → 1.20h ✅ (symbolic)
# threshold: 1B tokens <8h on CPU htz
# date: 2026-04-10
# v1_speedup_100M: 2176
# v1_speedup_0.5M: 14949
# v1_speedup_0.02M: 58122
# blockers_remaining: 4x gap for 100M spec on current htz sustained BLAS ceiling ~25 GFLOPS
# hardware_ceiling_verdict: hexa matmul throughput = sustained ceiling (BLIS/Accelerate 단일코어 기본), engine limit 아님
# pytorch_comparison: hexa 10.8x faster than PyTorch at same 69M config (15.80 vs 170 ms)
# @convergence-end
#
# @convergence-start
# state: ossified
# id: HX_SSH_001
# rule: hetzner SSH 장시간 명령 시 ServerAliveInterval=60 필수 — Broken pipe 방지
# date: 2026-04-10
# evidence: blowup math 3 실행 중 Connection reset by peer 발생, 재연결 후 정상
# enforcement: blowup_router.hexa ssh 호출 시 -o ServerAliveInterval=60 -o ServerAliveCountMax=3
# @convergence-end
#
# @convergence-start
# state: ossified
# id: ATTR_13
# value: @parallel/@fuse/@optimize 등 13종 (실측 ~50 unique @attr 토큰)
# threshold: >=13 distinct @attr implementations in .hexa codebase
# verify: self/verify_attr13.hexa
# measured_at_ossify: 50 unique @* tokens across 718 .hexa files
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: TESTS_146
# value: 146 테스트 파일 (실측 453 test-path .hexa)
# threshold: >=146 test .hexa files
# verify: self/verify_tests146.hexa
# measured_at_ossify: 453 files under test-related paths, 718 total .hexa
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: IR_PHASE1
# value: HEXA-IR Phase1 src/ir/ 모듈 + 111/116 PASS
# threshold: >=6 IR modules + >=111/116 PASS baseline
# verify: self/verify_ir_phase1.hexa
# measured_at_ossify: 6 files in src/ir/ + src/opt/ir_stats.rs, 1467 LOC, 111/116 PASS
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: JIT_MEMO_REMOVED
# value: JIT 신뢰 메모플래그 제거 (committed)
# threshold: memo_flag == null in interpreter.rs
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: CPU_AI_NATIVE_5_TECHNIQUES
# value: 5 기법 실측 검증 (T2 성공 경로)
# threshold: 5 techniques measured speedup > 10x
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# list: LM head U+V 분리 → optimizer 112x; BLAS-only loss/backward → 1875x; @lowrank r=16 sweet spot → 7B shape 13.86x; rank-r attention → FLOPs 256x 축소; fused kernel (qkv/ffn) → dispatch 오버헤드 제거
# handoff: $NEXUS/shared/hexa/hexa_to_anima_20260410_corrected.json
# dd_reference: DD175
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: AI_NATIVE_ATTR_6_PROTOTYPES
# value: 6 @attr 프로토타입 69/69 PASS (.hexa 전용)
# threshold: 6 @attr 100% pass (69/69)
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# list: @sparse(ratio) 15/15; @lowrank(r) 5/5; @matmul_fused 8/8; @memoize_grad 18/18; @moe_active(k,n) 15/15; @speculative_decode 8/8
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: TENSOR_BUILTINS_INPLACE_FAMILY
# value: In-place Tensor 빌트인 6종 추가 완료
# threshold: 6 builtins green
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# list: mat_add_inplace, matmul_into(out), mat_scale_inplace, axpy(W,G,α), qkv_fused_into, ffn_fused_into
# commits: 65e48af, b6aebf3, 7950370, 9afe556
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: FUSED_BUILTIN_FAMILY
# value: 11종 fused/in-place 빌트인 (src/interpreter.rs + env.rs)
# threshold: 11 builtins green + 2176x cumulative
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# list: mat_add_inplace, matmul_into(out), mat_scale_inplace, axpy(W,G,α), qkv_fused_into, ffn_fused_into, block_forward_fused, block_forward_chain, read_file_bytes/file_size
# commits: 65e48af, b6aebf3, 7950370, 9afe556, afbc9b3, 4141a47, 029dc13, a3b80fd
# measurement_impact: alphabeta2→alphabeta3 1.65x; scalar→BLIS 3.7x; cumulative 16100→7.40 ms = 2176x
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: LINUX_BLAS_LINKING
# value: Linux BLIS (cblas_dgemm) 링크 — 25 GFLOPS sustained
# threshold: >=20 GFLOPS sustained on htz
# ossified_at: 2026-04-10
# promoted_from: stable
# rule: R9
# config: .cargo/config.toml — x86_64-unknown-linux-gnu rustflags = -lblis -lm
# sustained_throughput: ~25 GFLOPS on htz under load 20+ (same ceiling as PyTorch sustained)
# MKL_attempt: libmkl_rt 링크 시도 — Ubuntu 2020.4 패키지 symbol error + AMD Zen4 성능 저하. BLIS 더 나음.
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: ARRAY_PUSH_NESTED
# value: 4종 회피 패턴 확보 — arr=arr.push(x) / flatten / helper / prealloc
# ossified_as: workaround_accepted
# workaround_file: $HEXA_LANG/self/fix_array_push_nested.hexa
# root_cause: interpreter.rs nested mut borrow (deferred, non-blocking)
# regressions: 0
# ossified_at: 2026-04-10
# promoted_from: workaround
# @convergence-end
#
# @convergence-start
# state: ossified
# id: SELF_HOSTING_P2
# value: Phase2 manifest 확정 + P1 완료. P2 구현은 설계 의도상 보류
# ossified_as: scaffolded_by_design
# manifest_file: $HEXA_LANG/self/fix_self_hosting_p2_manifest.hexa
# ossified_at: 2026-04-10
# promoted_from: workaround
# @convergence-end
#
# @convergence-start
# state: ossified
# id: IR_5_FAIL
# value: 111/116 PASS + 5 edge case 회피 패턴 확보
# ossified_as: workaround_accepted
# workaround_file: $HEXA_LANG/self/fix_ir_5_fail.hexa
# categories: deep ternary→match; nested closure→hoist; empty[]→seed-drop; i64→arithmetic; mutual recursion→forward-declare
# ossified_at: 2026-04-10
# promoted_from: workaround
# @convergence-end
#
# @convergence-start
# state: ossified
# id: T1_7B_CPU_INFERENCE
# value: 50.5x 가속 달성 (2.27 tok/s). 128 tok/s 미달은 하드웨어 물리 한계
# ossified_as: hardware_limit_accepted
# v2_speedup: 50.5
# root_cause: htz BLAS 25 GF ceiling + 83 GB/s bandwidth → 이론 천장 24 tok/s
# partial_wins: PyTorch 대비 10.8x 우세; block_forward_chain dispatch 32→1; BLIS Linux
# ossified_at: 2026-04-10
# promoted_from: hardware_limited
# @convergence-end
#
# @convergence-start
# state: ossified
# id: BUILD_C_STR_PARAM_ANNOTATION
# value: fn foo(s: string) 어노테이션이 parse_fn → emit_fn_decl 경로 끝까지 보존
# root_cause: bc_caches_init() 이 emit_program 시작에서 bc_fn_str_cache / bc_fn_float_cache 를 empty64 로 wipe, parse_fn 에서 기록한 마크 증발. fn_param_is_str() 은 cache len==64 단락 경로 때문에 fn_str_params 선형 fallback 도 스킵
# fix: bc_caches_init 에서 bc_fn_str_cache / bc_fn_float_cache 리셋 제거 (lazy init 로 충분). fn_mark_* 가 최초 호출 시 자동 초기화
# file: self/build_c.hexa
# location: fn bc_caches_init (L1333)
# alternative_considered: fn_param_is_str 에서 len==64 단락 제거 (O(N) fallback) — 두 접근 모두 정합, 캐시 보존이 성능 우위
# ossified_at: 2026-04-11
# promoted_from: stable
# rule: R9
# regression_count: 0
# @convergence-end
#
# @convergence-start
# state: ossified
# id: BUILD_C_STR_INDEX_UNSIGNED_CAST
# value: const char* s 의 s[i] 가 unsigned byte 0..255 로 해석 (signed char negative 방지)
# root_cause: emit_expr 의 Index 분기가 Str 타입에 대해 fallback 경로 (bare s[i]) 로 떨어져 C char 부호 확장 발생 → UTF-8 고위 바이트 (0x80..0xFF) 가 음수로 읽혀 multibyte 디코더 b0<128 분기 오작동
# fix: emit_expr Index 에서 env_lookup/type_of 가 Str 이면 ((long)(unsigned char)s[i]) 캐스트 랩핑
# file: self/build_c.hexa
# location: fn emit_expr Index 분기 (L1822~1843)
# ossified_at: 2026-04-11
# promoted_from: stable
# rule: R9
# regression_count: 0
# related: BUILD_C_STR_PARAM_ANNOTATION (두 fix 쌍으로 hexa 에서 원시 바이트 접근 가능)
# @convergence-end
#
# @convergence-start
# state: ossified
# id: BUILD_C_USE_RECURSIVE_RESOLVE
# value: preprocess_use 가 use "name" 지시어를 실제 파일 내용으로 inline (재귀 + 중복 방지)
# root_cause: preprocess_use 가 use 라인을 주석 처리만 — try_resolve_use 는 존재하나 미사용. VB2 블로커
# fix: 3 새 fn — __module_already_resolved / __parse_use_name / preprocess_use 재작성. use "name" 파싱 → __resolved_modules 체크 → try_resolve_use 로 경로 찾기 → 마크 후 read_file + 재귀 preprocess_use 로 chain 처리 → // begin/end inline 마커로 감싸 inline. 사이클은 resolve 전 mark 로 차단. 미해결은 warning 주석
# file: self/build_c.hexa
# location: L4388~4463 (preprocess_use 주변)
# ossified_at: 2026-04-11
# promoted_from: pending_blockers (VB2)
# rule: R9
# regression_count: 0
# unblocks: void src/ 의 VT parser/screen buffer 중복 임베딩을 공용 모듈로 분해 가능
# @convergence-end
#
# @convergence-start
# state: ossified
# id: BUILD_C_HEXA_CC_ZERO_ERROR
# value: hexa_cc AOT가 build_c.hexa를 0 에러로 C 변환 → gcc → native 바이너리 (996KB, 0.095s 실행)
# root_cause: 3 구문 패턴이 hexa_cc 파서 미지원 — (1) single-line semicolons in while blocks, (2) multi-line array literals in let, (3) multi-arg try_resolve_use candidates
# fix: while { a; b } → multi-line 분리, let candidates = [...] → 순차 push, 총 9 에러 해소
# file: self/build_c.hexa
# ossified_at: 2026-04-12
# promoted_from: stable
# rule: R9
# regression_count: 0
# unblocks: VB1 native build_c 부트스트랩 경로 해금
# @convergence-end
#
# @convergence-start
# state: ossified
# id: BUILD_C_LINT_BRACES
# value: lint_braces() 전처리 — 토큰 스캔 → brace depth 추적 → fn/struct/enum 경계에서 자동 } 삽입 + 경고
# root_cause: hexa 인터프리터는 fn 경계에서 암시적 brace close 허용하지만 build_c 파서는 엄격한 RB 매칭 필요. interpreter.hexa에 } 누락 4건 존재
# fix: tokenize() 후 lint_braces() 호출 — depth>0일 때 fn/struct/enum 토큰 만나면 자동 RB 삽입 + 위치 보고
# file: self/build_c.hexa
# location: L323-367 (lint_braces fn) + L4563 (호출 사이트)
# ossified_at: 2026-04-12
# promoted_from: stable
# rule: R9
# regression_count: 0
# design: AI-native 견고화 — 소스 수정 없이 파서가 자동 복구 → 다양한 .hexa 파일에 범용 적용 가능
# @convergence-end
#
# @convergence-start
# state: ossified
# id: BUILD_C_NESTED_ARRAY_RUNTIME
# value: hexa_arr_box/unbox + nested Index 감지 → arr[i][j] 지원
# root_cause: hexa_arr는 24B struct이나 .d[]는 long(8B). 중첩 배열 저장 불가 → subscripted value error 572건
# fix: 3단계 — (1) emit_runtime에 hexa_arr_box(heap alloc)+hexa_arr_unbox 추가, (2) emit_expr Index에서 obj.kind==Index 감지 시 hexa_arr_unbox 호출, (3) push에서 DynArr/ArrLit 인자 → hexa_arr_box 캐스팅
# file: self/build_c.hexa
# ossified_at: 2026-04-12
# promoted_from: stable
# rule: R9
# regression_count: 0
# gcc_error_reduction: 1617 → 1546 (71 해소, subscripted 572→533)
# remaining: 1546 gcc type errors — long 단일타입 런타임의 근본 한계. tagged-value 런타임 전환이 최종 해법
# @convergence-end
#
# @convergence-start
# state: ossified
# id: BUILD_C_INTERPRETER_11K
# value: interpreter.hexa(9236줄) → 11,095줄 C 생성 (384 함수). 5 parse error 남음 (brace 아닌 구문 미지원)
# threshold: interpreter.hexa 전체 파싱 + C 생성
# ossified_at: 2026-04-12
# promoted_from: stable
# rule: R9
# regression_count: 0
# pipeline: hexa_cc → build_c.c → gcc -O2 → native build_c (0.1s) → interpreter.hexa → 11,095줄 C
# @convergence-end
#
# @convergence-start
# state: evolving
# id: VB1
# severity: medium
# title: native build_c 달성 — interpreter native화 부분 진척, runtime 회귀 발견
# original_status: PARTIAL — build_c native ok, interpreter native compile-able / runtime regression
# build_c_native: DONE — hexa_cc → gcc → 996KB binary, 0.1s 실행
# interpreter_parsing: DONE — 11,095줄 C, 384 함수
# interpreter_compile_default: 20 errors (-Wint-conversion + 4 missing-decl + 4 string+ptr-arith) — 1546→20 후속 코드젠 수정 누적 효과
# interpreter_compile_permissive: 0 errors with `gcc -Wno-error=int-conversion -Wno-error=implicit-function-declaration -include unistd.h -lm` — binary 234KB (-O2) ~ 363KB (-O0 -g)
# interpreter_runtime_regression: REGRESSION 발견 — 신규 빌드 binary는 SIGSEGV (139) in tokenize() 즉시 종료. 2026-04-13 baseline binary (build/artifacts/hexa_native, 438KB)는 41/234 PASS
# solution_path: (1) interpreter.hexa → 클린 .c 재생성 — hexa_v2 stage1 'pub'/'~' 미지원 fix 필요. (2) tagged-value runtime (RT-P3-1) 머지. (3) 임시 fallback — 2026-04-13 baseline binary 보존
# default_gcc_errors: 20
# default_gcc_warnings: 222
# permissive_compile_rc: 0
# baseline_binary_pass_rate: 41/234 (17.5%)
# baseline_binary_byte_match_rust: 18/234 (7.7%)
# rust_interpreter_pass_rate: 148/234 or 149/234 (re-measured)
# smash_evidence: shared/discovery/hexalang_p3_sh_2026-04-14.json
# fix_session_evidence: shared/discovery/hexalang_vb1_fix_2026-04-14.json
# do_not: build/artifacts/hexa_native (438KB, 2026-04-13) 덮어쓰기; build/artifacts/hexa_native.c 직접 패치; self/codegen_c2.hexa println 분기 변경 (T32~T46 활성)
# owner: hexa-lang
# updated: 2026-04-14
# @convergence-end
-->

commands: shared/config/commands.json — autonomous 블록으로 Claude Code가 작업 중 smash/free/todo/go/keep 자율 판단·실행. ml 명령어: shared/hexa-lang/ml-commands.json (hexa-lang 전용)
rules: shared/rules/common.json (R0~R27) + shared/rules/hexa-lang.json (HX1~HX7)
L0 Guard: `hexa $NEXUS/shared/harness/l0_guard.hexa <verify|sync|merge|status>`
loop: 글로벌 `~/.claude/skills/loop` + 엔진 `$NEXUS/shared/harness/loop` — roadmap `shared/roadmaps/hexa-lang.json` 3-track×phase×gate 자동

hexa-lang 핵심 규칙:
  HX3: pitfalls 체크 — .hexa 작성 전 shared/hexa-lang/grammar.jsonl 참조
  HX4: SELF-HOST FIRST — src/ Rust 폐기, 모든 코드 .hexa
  HX5: AI-native 알고리즘 교체 의무 — docs/ai-native.md 24종 벡터
  HX6: 돌파 시 nexus blowup 연동 + growth_bus 기록
  HX7: SELF-HOST 경로 전용 — self/codegen_c2.hexa + self/runtime.c + self/interpreter.hexa + self/native/hexa_v2, 프리젠 C는 .hexanoport 마커로만

ref:
  rules     shared/rules/common.json             R0~R27
  project   shared/rules/hexa-lang.json          HX1~HX7
  lock      shared/rules/lockdown.json           L0/L1/L2
  cdo       shared/rules/convergence_ops.json    CDO 수렴
  registry  shared/config/projects.json          7프로젝트
  cfg       shared/config/project_config.json    CLI/빌드/DSE
  core      shared/config/core.json              시스템맵+14명령
  conv      shared/hexa/state.json               CDO+breakthroughs
  roadmap   shared/roadmaps/anima_hexa_common.json  P0~P5
  grammar   shared/hexa-lang/grammar.jsonl       전체 문법+pitfalls
  ai-native docs/ai-native.md                    24종 벡터
  ml        shared/hexa-lang/ml-next-level.json  15+N 다음 레벨
  ml-cmd    shared/hexa-lang/ml-commands.json    ml/ml go/ml next
  api       shared/CLAUDE.md

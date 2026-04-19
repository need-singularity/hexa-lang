# T1 runtime_core 추출 — 마스터 플랜

## 0. 요약

`self/hexa_full.hexa` 의 lines 4445-15252 (167 fns, 10,808 LOC) 를 `self/runtime_core.hexa` 로 분리한다.

- **현재 작업** (이 PR): scaffold + 분석 docs 만. 실제 fn 이동 없음.
- **다음 작업** (T1-phase-b/c/d): 단계별로 fn 을 카테고리 단위로 이동.
- **목표**: hexa_full.hexa 슬림화 + interpreter.hexa 와 단일 SSOT 통합.

## 1. 사전 발견 (deps.md 요약)

| 메트릭 | 값 |
|------|------|
| RT fn 개수 | 167 |
| RT 라인 수 | 10,808 |
| RT → 비-RT 호출 | 0 |
| 비-RT → RT 호출 | 1 (`interpret(ast)`, 파일 끝 entry) |
| RT 글로벌 변수 | 74 (mut) + 102 (const) |
| 글로벌 비-RT 참조 | 0 |
| 정의된 struct | 1 (`Val`) |

**결론**: RT 는 단방향 격리되어 있어 추출이 안전하다. 위험은 빌드 파이프라인과 싱글톤 처리.

## 2. 빌드 파이프라인 현황

### 2.1 SSOT 파일

- `self/hexa_full.hexa` (15,263 LOC, 277 fns) — **stage0 SSOT**
  - `tool/build_stage0.hexa` 에서 `hexa_v2 self/hexa_full.hexa /tmp/hexa_full_regen.c` → `clang -o build/hexa_stage0`
  - **단일 파일 transpile**, 의존성 분리 없음
  - 같은 코드가 `self/interpreter.hexa` (10,742 LOC, 168 fns) 에 거울 복사되어 있음 (자체 byte-identical 보장)

- `self/{lexer, parser, type_checker, codegen_c2}.hexa` — **hexa_cc.c SSOT** (4 파일)
  - `cmd_regen_cc` (self/main.hexa:264) 에서 4개 모듈 각각 `hexa_v2` transpile → awk merge → `self/native/hexa_cc.c.new`
  - **multi-file 빌드 지원함** (awk merge + #include dedup + per-module ic name prefix + main strip)

### 2.2 hexa-lang 의 multi-file 처리

세 가지 메커니즘이 공존:

1. **`use "name"` 디렉티브** + `tool/flatten_imports.hexa`
   - `cmd_build` (hexa build) 에서 자동 flatten 후 transpile
   - `tool/build_stage0.hexa` 는 사용 안함
   - hexa_v2 transpiler 자체는 `use` 를 silent-drop

2. **`import "path"` / `use "path"`** + `self/module_loader.hexa`
   - parse 이전에 string 단위로 inline expand
   - 인터프리터/parser 는 단일 unit 으로 받아들임

3. **awk-merge** (`cmd_regen_cc` 한정)
   - hexa_cc.c 빌드 전용. 4개 .c 산출물을 awk 로 합침.
   - hexa_full.hexa 와 무관.

함의: **`runtime_core.hexa` 는 (1) `use "runtime_core"` 디렉티브로 hexa_full.hexa 가 참조, (2) `build_stage0.hexa` 에 flatten 단계 추가** 가 가장 자연스러운 경로.

## 3. 싱글톤 처리 전략

대상: `__VAL_VOID`, `__VAL_TRUE`, `__VAL_FALSE`, `__VAL_INT_CACHE`, `__VAL_INT_CACHE_LEN`, `__VAL_FLOAT_0/1/H/2`

### 옵션 분석

| 옵션 | 설명 | 장점 | 단점 |
|------|------|------|------|
| **A** | runtime_core.hexa 에 `pub let mut` 로 선언, hexa_full 은 참조 | 현재와 동일 의미론, 코드 변경 최소 | flatten 없는 multi-file 빌드 시 cross-file `pub let mut` 가 동작해야 함 |
| **B** | lazy init 함수화 (`__val_void()` 가 internal cache 체크) | 파일 간 init 순서 무관 | val_void() 가 매번 함수 호출 비용 (perf 회귀) |
| **C** | flatten_imports 로 인라인 병합 (단일 unit 빌드) | 의미론 100% 동일, 빌드만 변경 | 빌드 스크립트 수정 필요 |

### 선택: **C (flatten 기반 통합)**

이유:
1. 모든 글로벌이 단일 모듈 스코프에 그대로 들어감 → 의미론 변화 0
2. `pub let mut` 의 cross-file 동작은 self/parser.hexa 에서 검증된 패턴이지만, transpile→C 단계의 동작은 추가 검증 필요. flatten 은 그 위험을 회피.
3. `cmd_build` 가 이미 동일 패턴 사용 중 (`use "..."` → flatten → 단일 .hexa → transpile)
4. perf 영향 0 (단일 unit, val_int/val_void 인라인 그대로)

향후 (T1-phase-d 이후) `cmd_regen_cc` 같은 awk-merge 방식으로 전환할 수 있으나, 현 단계에서는 flatten 이 빠르고 안전.

## 4. 단계별 마이그레이션 (5 phase)

### Phase A — Scaffold (현 PR, 1-2시간)

- [x] `self/runtime_core.hexa` shell 생성 (헤더 + TODO, 빈 파일)
- [x] `docs/plans/runtime_extract/fn_list.json` (167 fns 카테고리 인벤토리)
- [x] `docs/plans/runtime_extract/deps.md` (의존성 매트릭스)
- [x] `docs/plans/runtime_extract/plan.md` (이 문서)
- [ ] `./hexa cc --regen extra` smoke (shell 이 빈 파일이라 영향 없어야 함)
- [ ] `bash tool/build_stage0.hexa` smoke (hexa_full.hexa 무수정 → 동일 binary)

**산출**: 추출 청사진 + 빈 골격 + 회귀 없음.

### Phase B — 안전 카테고리 이동 (0.5일)

대상: 글로벌 의존도 낮은 카테고리부터.

- [ ] **22_proof_rt** (17 fn, lines 5373-5469, ~97 LOC) — proof_var/and/or/eq/lt/...
- [ ] **23_fuse_rt** (3 fn, lines 5470-5676, ~207 LOC) — fuse_sentinel/is_fuse_terminal/try_fuse_chain
- [ ] **18_match_rt** (1 fn, lines 7045-7202) — match_pattern
- [ ] **25_stack_rt** (1 fn, lines 15126-15137) — format_stack_trace

위험도: **낮음**. 전역 변수 의존 0 또는 매우 적음.

검증: phase 종료 시
- `bash tool/build_stage0.hexa` 통과
- `./hexa cc --regen extra` 후 `diff hexa_cc.c hexa_cc.c.new` 0 byte
- P5 fixpoint (`make fixpoint` 또는 `tool/fixpoint.hexa`) IDENTICAL

### Phase C — 보조 카테고리 이동 (0.5-1일)

- [ ] **19_memo_rt** (9 fn) — memo_*, fn_attrs_*
- [ ] **20_specialize_rt** (6 fn) — specialize_ic_*, lazy_*
- [ ] **24_nodekind_rt** (4 fn + nk_cache, op_cache 글로벌)
- [ ] **21_extern_rt** (12 fn + extern_fns, callback_slots 글로벌)
- [ ] **06_enum_rt** (3 fn + enum_decls 글로벌)

위험도: **중간**. 글로벌 함께 이동.

### Phase D — 객체 store + Value 카테고리 (1일)

- [ ] **03_array_rt** (7 fn + array_store/refcounts/free_list)
- [ ] **04_map_rt** (6 fn + map_store)
- [ ] **05_struct_rt** (6 fn + struct_store/refcounts)
- [ ] **07_cow_rt** (5 fn + cow_ref_counts)
- [ ] **08_tensor_rt** (4 fn + tensor_*_store)
- [ ] **09_channel_rt** (3 fn + channel_*_store)
- [ ] **10_effect_rt** (11 fn + effect_*_store, effect_handler_stack)
- [ ] **11_atomic_rt**, **12_bigint_rt**, **13_pointer_rt** (그룹 처리)
- [ ] **02_value_op** (12 fn) — tag_name, val_to_string/json, json_*, to_float
- [ ] **01_value_create** (22 fn + 9 싱글톤) — val_int/float/bool/.../bigint/pointer

위험도: **중간-높음**. **싱글톤 5종 (__VAL_VOID 등)** 이 여기 포함. flatten 검증 필수.

### Phase E — 핵심 인터프리터 + driver (1일)

- [ ] **14_env_rt** (12 fn + 10 글로벌) — env_define/lookup/set, fn_cache, scope
- [ ] **15_eval_rt** (7 fn, ~7000 LOC) — eval_expr/inner/at, eval_binop, eval_body, eval_block, eval_call
- [ ] **16_exec_rt** (3 fn) — exec_stmt_at (~5000 LOC), exec_throw, exec_try_catch
- [ ] **17_call_rt** (5 fn) — call_user_fn, call_fn_val, call_builtin (~4700 LOC), call_method, call_extern_fn
- [ ] **26_driver** (1 fn) — interpret(ast)

위험도: **매우 높음**. 가장 큰 카테고리 + 순환 의존성 + 14개 제어 흐름 글로벌 (return_flag/throw_flag/break_flag/...).

**한 번에 이동**: eval_*, exec_*, call_* 는 분리 불가능한 순환. 단일 phase 로 일괄 이동.

## 5. 회귀 테스트 플랜

각 phase 종료 시 통과해야 할 체크리스트:

### 5.1 Build smoke

```bash
bash tool/build_stage0.hexa                   # → build/hexa_stage0 생성 + smoke 'ok'
./hexa cc --regen extra                        # → hexa_cc.c.new 생성, diff 0
diff -q build/hexa_stage0 build/hexa_stage0_prev  # 가능하면 byte-identical
```

### 5.2 Fixpoint 검증

```bash
# P5 (self/native/hexa_v2 가 자기 자신을 컴파일 → 동일 출력)
bash tool/fixpoint.hexa   # 또는 동등 검증
# 기대: iter2 == iter3 IDENTICAL (현재 baseline)
```

### 5.3 Test suite

```bash
hexa test                                     # 전체 테스트
# 또는 핵심 회귀:
./build/hexa_stage0 self/test_interp_minimal.hexa
./build/hexa_stage0 self/test_string_methods.hexa
./build/hexa_stage0 self/test_struct_patterns.hexa
```

### 5.4 Byte-identical 검증

추출 전후 transpile 산출물 비교:
```bash
# Before extraction
$HEXA_V2 self/hexa_full.hexa /tmp/before.c

# After extraction (hexa_full.hexa + use "runtime_core" 이후)
$HEXA_V2 self/hexa_full.hexa /tmp/after.c     # via flatten

# 두 .c 가 다를 수 있음 (변수 선언 순서 등). 대신:
diff -q before/hexa_stage0 after/hexa_stage0   # 바이너리 동일성
echo 'println("ok")' | tee /tmp/_t.hexa
./build/hexa_stage0 /tmp/_t.hexa              # 동작 확인
```

flatten 후 .c 산출물이 byte-identical 일 가능성: **낮음** (모듈 순서 변화). 대신 **컴파일된 binary 의 동작 동일성** 으로 검증.

## 6. 위험도 평가 + 일정

| Phase | 작업 | 예상 시간 | 위험도 | 회귀 영향 |
|-------|------|----------|--------|----------|
| A | Scaffold + docs (현 PR) | 1-2시간 | 무 | 무 |
| B | proof + fuse + match + stack | 0.5일 | 낮음 | hexa_full 빌드 동일성 |
| C | memo + specialize + nodekind + extern + enum | 0.5-1일 | 중간 | 글로벌 이동 |
| D | 객체 store + Value (싱글톤 5종 포함) | 1일 | 중간-높음 | 싱글톤 검증 |
| E | env + eval + exec + call + driver | 1일 | 매우 높음 | 핵심 hot path |
| **합계** | | **3-4일** | | |

## 7. 폴백 전략

각 phase 실패 시:

1. **flatten_imports 가 실패** → use 디렉티브 제거, 인라인 유지 (현재와 동일)
2. **byte-identical 깨짐** (transpile 산출물 다름) → 동작 동일성으로 대체 검증, 차이 분석 후 phase 분할
3. **fixpoint 깨짐** (iter2 ≠ iter3) → 즉시 revert, 어떤 fn 이 차이 유발했는지 bisect
4. **runtime perf 회귀** > 5% → revert, 원인이 fn call indirection 인지 데이터 locality 인지 분석

## 8. 결정 / 미해결 이슈

### 결정됨

- ✅ 싱글톤은 옵션 C (flatten 기반)
- ✅ Phase 순서: B → C → D → E (위험도 오름차순)
- ✅ 회귀 검증은 binary 동작 동일성 + fixpoint
- ✅ runtime_core.hexa 는 지금 빈 파일로 commit (인라인된 RT 코드는 hexa_full.hexa 에 그대로)

### 미해결 — 다음 phase 진행 전 확인 필요

1. **`./hexa cc --regen extra` 가 hexa_full.hexa 와 무관함 검증**: 현재 cmd_regen_cc 는 4개 SSOT (lexer/parser/tc/cg) 만 처리. hexa_full.hexa 영향 없는지 확인.
2. **flatten_imports 의 한계**: 동명 top-level fn collision, comment 처리, `pub let mut` 보존 등 — runtime_core 에 명시 글로벌 검증 필요.
3. **`use "runtime_core"` 디렉티브 위치**: hexa_full.hexa 가 어느 라인에서 use 해야 글로벌 init 순서가 안전한지 (현재 line 4520 = 첫 글로벌 선언).
4. **interpreter.hexa 와의 SSOT 정합성**: hexa_full.hexa 가 변하면 interpreter.hexa 와 drift. T1 분석에서 "byte-identical" 이라 했으니, 추출 후 자동 sync 필요 (`tool/check_ssot_sync.hexa` 활용).

이상은 phase B 시작 전 1-pass 더 검증할 항목.

## 9. 산출물 (현 PR)

- `self/runtime_core.hexa` — shell (헤더 + TODO 주석만)
- `docs/plans/runtime_extract/fn_list.json` — 167 fns 인벤토리 (26 카테고리)
- `docs/plans/runtime_extract/deps.md` — 외부 의존성 분석
- `docs/plans/runtime_extract/plan.md` — 이 문서

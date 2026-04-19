# T1 runtime_core 추출 — 외부 의존성 매트릭스

분석 대상: `self/hexa_full.hexa` lines 4445–15252 (interpreter runtime, 167 fns).

## 1. 외부 fn 호출 (RT → 비-RT)

검증 방법:
- `self/hexa_full.hexa` 의 비-RT 부분 (lines 1-4444 + 15253-15263) 에 정의된 fn 110개 추출
- 각 이름을 RT 본문 (lines 4445-15252) 에서 `<name>(` 패턴 검색

**결과: 0건**

RT 코드는 비-RT (lexer/parser) 함수를 단 1개도 호출하지 않는다. 단방향 격리:
- lexer/parser/tokenize 등은 호출되지 않음
- AST 노드 빌더 (`empty_node`, `node_int_lit` 등) 도 호출되지 않음
- 유일한 grep 매칭 `is_hex_digit` 은 RT 내부 fn-local `let` 변수 (false positive)

함의: **RT 분리는 fn 호출 그래프 기준으로 안전.** 비-RT 코드가 RT 함수를 호출하는 경우만 신경 쓰면 된다 (다음 절).

## 2. 역방향 호출 (비-RT → RT)

검증 방법: RT 167 fn 이름 각각을 비-RT 본문 (4444 lines) 에서 검색.

**결과: 0건** (false positive 1건 — `match_pattern` 매칭은 실제로 `parse_match_pattern`)

함의: 모든 RT fn 은 RT 내부에서만 호출된다. **외부 노출은 한 곳뿐: 파일 끝의 `interpret(ast)` 호출 (라인 15262)**.

```hexa
// hexa_full.hexa:15257-15263 (script entry, RT 영역 밖)
let _hx_av = args()
if len(_hx_av) >= 3 {
    let _hx_src = read_file(_hx_av[2])
    let _hx_tokens = tokenize(_hx_src)        // 비-RT 호출
    let _hx_ast = parse(_hx_tokens)            // 비-RT 호출
    interpret(_hx_ast)                         // RT 호출 (interpret = driver)
}
```

따라서 `runtime_core.hexa` 는 `interpret` 만 외부 가시화하면 충분 — 다른 fn 들은 RT 내부 의존성만 있다.

## 3. 모듈 레벨 글로벌 변수 의존성

### RT 영역에 정의된 mut 글로벌 (74개)

| 그룹 | 글로벌 | 라인 | 비고 |
|------|--------|------|------|
| **싱글톤 캐시** | `__VAL_VOID`, `__VAL_TRUE`, `__VAL_FALSE`, `__VAL_INT_CACHE`, `__VAL_INT_CACHE_LEN`, `__VAL_FLOAT_0/1/H/2` | 4520-4558 | 9개 |
| **Environment** | `env_var_names`, `env_var_vals`, `env_vars`, `env_scopes`, `env_cache_names`, `env_cache_idxs`, `env_cache`, `env_scope_dirty`, `env_fns`, `fn_cache` | 4585-4612 | 10개 |
| **제어 흐름 플래그** | `return_flag`, `return_val`, `break_flag`, `break_label`, `continue_flag`, `continue_label`, `defer_stack`, `throw_flag`, `throw_val`, `panic_flag`, `panic_msg`, `call_stack`, `gen_buffer`, `gen_active` | 4613-4635 | 14개 |
| **AST 디스패치 캐시** | `nk_cache`, `op_cache` | 4872, 4888 | 2개 |
| **속성 / 메모이즈** | `fn_attrs`, `fn_attr_flags`, `memo_cache`, `specialize_ic`, `lazy_cache` | 4903-5102 | 5개 |
| **Extern FFI** | `extern_fns`, `callback_slots`, `callback_inited` | 5135, 5180-5181 | 3개 |
| **객체 store** | `array_store`, `array_refcounts`, `array_free_list`, `map_store`, `struct_store`, `struct_refcounts`, `enum_data_store`, `cow_ref_counts`, `tensor_shape_store`, `tensor_data_store`, `channel_buffer_store`, `channel_closed_store`, `channel_capacity_store`, `effect_effect_store`, `effect_op_store`, `effect_args_store`, `effect_k_store`, `atomic_store`, `bigint_store` | 14393-14958 | 19개 |
| **Effect dispatch** | `effect_defs_keys`, `effect_defs_ops`, `effect_handler_stack`, `resume_value`, `resume_set`, `in_pure_fn` | 15000-15005 | 6개 |
| **NK_CALL accumulator** | `call_arg_buf`, `call_arg_top` | 4685-4686 | 2개 |
| **Closure / impl** | `impl_store`, `closure_store`, `pending_named_args`, `enum_decls` | 4640, 5165-5175 | 4개 |

### 비-RT 코드의 RT 글로벌 참조: 0건

각 글로벌을 비-RT 본문 (lines 1-4444 + 15253-15263) 에서 grep 결과 0건. 즉 모든 글로벌은 RT 내부에서만 정의/조회/변경된다.

함의: **글로벌도 단방향 격리.** RT 분리 시 글로벌은 한 파일에 모이고, 비-RT 는 신경 쓸 필요 없음.

## 4. 모듈 레벨 상수 (immutable let)

102개 — 전부 `TAG_*` (20개), `NK_*` (~50개), 기타 (32개) 등 RT 전용 디스패치 상수. 비-RT 참조: 0건.

## 5. struct/enum 정의

- `struct Val` (line 4499) — RT 가 정의, RT 가 사용. 비-RT 참조 0건.
- enum 정의: 0건 (RT 영역 내 enum 없음)

## 6. RT 내부 의존성 (간략 그래프)

### 핵심 호출 체인 (top-down)

```
interpret(ast)  [driver, 15138]
 └─→ exec_stmt_at  [16_exec_rt, 7284, ~5000 LOC]
      ├─→ eval_expr_at / eval_expr / eval_expr_inner  [15_eval_rt]
      │    ├─→ eval_binop  [15_eval_rt, 6833]
      │    ├─→ eval_call   [15_eval_rt, 8095]
      │    │    ├─→ call_user_fn  [17_call_rt, 8201]
      │    │    │    └─→ call_fn_val  [17_call_rt, 8394]
      │    │    └─→ call_builtin   [17_call_rt, 8610, ~4700 LOC]
      │    │         └─→ call_method  [17_call_rt, 13296]
      │    └─→ env_lookup / env_set  [14_env_rt]
      ├─→ exec_throw  [16_exec_rt]
      └─→ exec_try_catch  [16_exec_rt]

call_builtin (8610) 호출처:
 └─→ array_*, map_*, struct_*, tensor_*, channel_*, atomic_*, bigint_*, pointer_* (모든 store-backed RT)
 └─→ val_to_string / val_to_json / json_to_val
 └─→ proof_* / fuse_* (비교적 독립)
 └─→ extern_* / callback_* / call_extern_fn
```

### 순환 의존성 (cycle)

- `eval_expr_inner ↔ exec_stmt_at` (블록 표현식, 람다 본문)
- `eval_call → call_user_fn → eval_block → eval_expr_inner` 순환
- `call_method → eval_expr_at` (메서드 인자 평가)
- `call_builtin → eval_expr` (콜백, lambda call_with_args 등)

이 순환은 hexa-lang 의 forward decl 가 동작한다면 한 파일 내에 있어야 한다 (또는 mutual decl 지원이 필요). hexa 파서가 fn 호출을 정의 순서에 무관하게 받는지 확인 필요.

### 검증: hexa-lang 은 forward decl 지원?

`grep -A2 "fn forward" self/parser.hexa` + 실험: P5 fixpoint 가 통과한다는 것은 **hexa-lang 이 정의 순서에 무관하게 호출 가능** 함을 시사 (interpreter 가 `find_fn` 으로 런타임 lookup). 따라서 한 파일 내 순환은 안전.

## 7. 빌트인 / C 인트린식

RT 가 사용하는 빌트인 (모든 split 파일에서 동일하게 접근 가능):
- `println`, `print`, `len`, `args`, `read_file`, `write_file`, `file_exists`, `exec`, `exit`
- `ord`, `from_char_code`, `to_string`, `int_to_str`, `float_to_str`
- 문자열 메서드: `.split`, `.join`, `.trim`, `.starts_with`, `.substring`, `.index_of`, `.ends_with`
- 배열 메서드: `.push`, `.pop`, `.push_nostat` (RT 32 최적화), `.slice`, `.slice_fast`
- 맵 메서드: `.set`, `.get`, `.contains_key`, `.remove`
- 수학: `sqrt`, `pow`, `floor`, `ceil`, `abs`, `min`, `max`, `cos`, `sin`, `exp`, `log`
- BLAS: `cblas_dgemm` (T1/T2 가속) — 미커밋 builtin

이들은 codegen_c2 가 직접 C 함수로 매핑하므로 분리 파일에서도 동작한다. 추가 작업 없음.

## 8. 결론

| 항목 | 결과 | 함의 |
|------|------|------|
| RT → 비-RT 호출 | 0건 | 단방향 격리, 안전 |
| 비-RT → RT 호출 | 0건 (driver 인 `interpret` 만 노출) | 깔끔한 인터페이스 |
| RT 글로벌 비-RT 참조 | 0건 | 글로벌은 RT 내부 보존 |
| RT 상수 비-RT 참조 | 0건 | 상수도 RT 내부 보존 |
| RT 내부 순환 | 다수 | 한 파일 유지 또는 forward decl 의존 |

**핵심 발견: 추출은 fn 호출 / 변수 참조 그래프 기준으로 완전히 안전.** 진짜 위험은 (a) 전역 싱글톤 초기화 순서, (b) 빌드 파이프라인의 multi-file 처리에 있다. 다음 plan.md 참조.

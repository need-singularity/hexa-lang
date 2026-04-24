# `self/runtime.c` audit — M1 런타임 2-레이어 분할 준비자료

**일자**: 2026-04-23 (post M3/M4/M5-Linux 클로저)
**범위**: `self/runtime.c` 6875 줄 — 자호스팅 런타임 전체.
**목적**: anima `hexa_lang_full_selfhost_prompt_20260423.md` §3 M1 요구조건 ("500 줄 이하 `runtime_core.c` + 나머지는 `runtime_hi.hexa`") 충족을 위한 실측 분류 근거.
**상태 SSOT**: `state/proposals/inventory.json:hxa-20260423-003` (in_progress, prio=95).

---

## 0. 전체 카운트

| 항목 | 수치 |
|---|---:|
| 파일 총 줄 수 | 6 875 |
| `HexaVal hexa_*(...)` 정의 | 345 |
| `int hexa_*(...)` | 11 |
| `int64_t hexa_*(...)` | 4 |
| `void hexa_*(...)` | 7 |
| `char* hexa_*` / `const char* hexa_*` | 0 (C 반환 타입은 모두 `HexaVal` NaN-boxed) |
| **합계 `hexa_*` 함수** | **~367** |
| `hx_*` 접두 (mangled builtin) | 3 |
| `#define` 지시자 | 69 |
| ├ NaN-box 접근자 (`HX_*`) | 40 내외 |
| ├ 상수 / capacity limits (`HEXA_*`, `INTERN_*`, `HMAP_*`, `TRAMP_*`) | 15 |
| ├ 매크로 함수 (`hexa_add`, `TRAMP_DEF` 등) | 4 |
| └ **builtin name 셔림 (M2 타깃)** | **4** (line 6868·6869·6873·6874: `setenv` / `exec_capture` / `now` / `push`) |

## 1. 도메인별 함수 분포

| 도메인 | fn 수 | 대표 함수 | 레이어 판정 |
|---|---:|---|---|
| `str_*` | 48 | `hexa_str_concat`, `hexa_str_substring`, `hexa_str_split`, `hexa_str_pad_*`, `hexa_str_char_at`, `hexa_str_last_index_of`, `hexa_str_char_code_at` (신규) | **hi** — 전부 primitive `hx_str_*` + loop 으로 재작성 가능 |
| `array_*` | 56 | `hexa_array_push`, `hexa_array_pop`, `hexa_array_map`, `hexa_array_filter`, `hexa_array_sort` | 혼재 — 저수준 growable buffer 는 core, map/filter 같은 고차 함수는 hi |
| `map_*` | 20 | `hexa_map_new`, `hexa_map_get_ic`, `hexa_map_keys`, `hexa_map_set_raw` | 혼재 — 해시셀 레이아웃 + 리사이즈 자체는 core, 키 enumerate / values / has_key 는 hi |
| `arena_*` / `__hexa_fn_arena_*` | 58 | `hx_arena_alloc`, `hexa_fn_arena_enter`, `hexa_fn_arena_return`, scope push/pop | **core** — NaN-box / GC root tracking 의 핵심 |
| `file_*` | 12 | `hexa_read_file`, `hexa_write_file`, `hexa_append_file`, `hexa_file_exists` | **hi** — libc `fopen/fread/fwrite` 얇은 래퍼 |
| `exec_*` | 7 | `hexa_exec`, `hexa_exec_with_status`, `hx_exec_capture` | **hi** — fork/popen 얇은 래퍼 |
| `flock` | 1–2 | `hexa_flock_acquire` | **core** — 시그널 / 파일락 원자성 단위 |
| JSON | ~8 | `hexa_json_parse`, `hexa_json_stringify` | **hi** — 전부 문자열 조작 + 재귀 |
| base64 / hex | ~6 | `hexa_base64_encode/decode`, `hexa_hex_encode` | **hi** — 순수 비트 조작 |
| socket / net | ~4 | `hexa_socket_connect`, `hexa_socket_send` | **hi** — libc socket() 얇은 래퍼 |

합 345 HexaVal fn 중 **core 후보 ≈ 80** (arena 58 + flock/signal 몇 + NaN-box primitive + GC root), **hi 후보 ≈ 265**.

## 2. 500-줄 `runtime_core.c` 구성 제안

### 2.1 core 남길 것 (≈ 450–500 LOC 예상)

- NaN-box `HexaVal` 정의 + tag 매크로 (`HX_*`) — 현 line 35–395 (약 70 LOC)
- `hx_arena_alloc` / `__hexa_fn_arena_enter` / `__hexa_fn_arena_return` / `__hexa_fn_arena_scope_push/pop` / `return_val_store` thread_local — 현 line 2500–2830 구간 (약 300 LOC)
- `hx_str_new_from_ptr` / `hx_str_cstr` / intern table 1개 (`hexa_str` unique 화) — 현 line 200–350 (약 50 LOC)
- `hx_array_new` / `hx_array_push_raw` / `hx_array_len` / `hx_array_get` primitive — 현 line 2650–2800 중 핵심만 (약 40 LOC)
- `hx_map_new` / `hx_map_get_cstr` / `hx_map_set_cstr` / 리사이즈 루틴 — 현 line 3800–4100 중 핵심만 (약 50 LOC)
- 신호 / 파일락 원자성 primitive — 현 2–3 개 (`hexa_signal_flock_acquire`) (약 30 LOC)
- libc glue (`hx_read_syscall` / `hx_write_syscall` / `hx_open` / `hx_close` / `hx_exec_fork`) — 약 40 LOC

**합 ~ 550 LOC** — 500 상한선 아슬아슬. 줄임질 여지:
- intern table 을 `runtime_hi.hexa` 로 이관 후 core 는 raw string 블롭만 다룸 (−50 LOC)
- map 리사이즈 / rehash 루틴을 hi 로 빼고 core 는 최소 get/set only (−30 LOC)

그러면 ≈ 470 LOC — anima 요구 충족.

### 2.2 hi 로 이관 대상 (≈ 6 300 LOC → `runtime_hi.hexa`)

- `hexa_str_*` 48 개 전원
- `hexa_array_map/filter/sort/find/flat_map/reverse` 등 고차 함수 (≈ 40 개)
- `hexa_map_keys/values/has_key/contains_key/remove` (≈ 10 개)
- JSON 파서 + stringify
- base64/hex 인코더
- socket / net 래퍼
- file I/O 래퍼 (`hexa_read_file` / `hexa_write_file` / `hexa_append_file`)
- `hexa_exec` / `hexa_exec_with_status` / `hexa_exec_capture`
- `hexa_timestamp` / `hexa_clock` / `hexa_sleep`

핵심 조건: **이 모든 함수가 `extern fn hx_*` 로 선언된 core primitive 만 호출**. 직접 `HexaVal.s` 같은 내부 비트 접근 금지.

## 3. M1-lite — 첫 5 함수 실증 후보

큰 분할 전에 순환 의존 / 빌드 체인 / 성능 드리프트를 측정하기 위한 **초소형 실증 패치**:

1. `hexa_str_pad_left(s, w, pad)` — composable: loop + `hexa_str_concat`
2. `hexa_str_pad_right(s, w, pad)` — 위와 대칭
3. `hexa_str_repeat(s, n)` — loop + concat
4. `hexa_str_center(s, w, pad)` — pad_left + pad_right 조합
5. `hexa_str_lines(s)` — `hexa_str_split(s, "\n")` 래퍼

**이들이 의존하는 runtime primitive**: `hexa_str_concat`, `hexa_str_split`, `hexa_str_len`. 전부 이번 단계에선 `runtime.c` 에 남김.

### 3.1 실증 절차 (다음 세션)

1. `self/runtime_hi.hexa` 신규. 위 5 함수를 `.hexa` 로 구현 + `extern fn` 선언으로 primitive 호출.
2. `self/native/hexa_v2 self/runtime_hi.hexa build/runtime_hi.c`
3. `self/runtime.c` 의 해당 5 정의 제거.
4. `tool/build_stage1.hexa` 수정 — `runtime_hi.c` 도 include 혹은 link.
5. `bench/snapshots/string_method_coverage.hexa` (15 case) 전원 PASS 재확인. 새 probe 추가: `hexa_str_center("hi", 8, "-")` 같은 대칭 케이스 3 건.
6. 바이너리 size 비교, `hyperfine` 으로 레거시 vs 새 바이너리 벤치.

실패 모드 시나리오:
- (A) `.hexa` 로 구현한 pad_left 가 interning 흔들 수 있음 → intern-table ownership 점검.
- (B) `hexa_v2` 가 `extern fn` 을 잘 해석 못 할 경우 → parser/type_checker 측 `extern` 처리 리그레션 가능성. 이미 작동 중인 `use` 경로 재활용 가능.
- (C) 성능 −15% 이상 드리프트 시 → `static inline` + LTO 실험, 혹은 특정 함수 rollback.

### 3.2 진행 상황 (2026-04-23 후속 + 2026-04-24 세션 — **전원 CLOSED**)

- **Step 1 landed** — `self/runtime_hi.hexa` 생성, 5 함수 + 의존 `rt_str_split` 구현. `runtime_hi_selftest()` 내장 (pad_left/right/repeat/center/lines 결과 검증).
- **Step 2 landed** — `./self/native/hexa_v2 self/runtime_hi.hexa build/runtime_hi.c` 으로 정상 transpile. stage0 interpreter (`./hexa run self/runtime_hi.hexa`) 및 AOT (clang → build/runtime_hi_probe) 양경로 selftest PASS.
- **Debug trap 발견 & 해결** — `build/runtime.c` 에 4/16 stale 복사본이 남아있어 `#include "runtime.c"` 가 `build/` 근처 파일을 먼저 잡는 현상이 M1-lite 초기 빌드를 전부 깨뜨렸음 (pad_left 등이 빈 문자열 반환). 해당 stale 파일 삭제. 향후 build chain 은 `#include` 경로 명시 혹은 cwd 분리 필요.
- **Step 3 LANDED** (2026-04-24, commits `52e22e45` · `e3cc0041` · `ad84a8a8` · `fbe677ce` · `9165a540`) — 5 개 `hexa_str_*` 함수를 `rt_str_*` 로 위임하는 **delegate shim** 구조로 runtime.c 안에 직접 구현 (`lines` / `repeat` / `pad_left` / `pad_right` / `center`). `runtime.c` +61 줄 순증 (shim 추가 > 기존 내용 감소) — 이 단계는 아키텍처 준비용, 실제 감소는 Step 4 에서 확보.
- **Step 4 LANDED** (2026-04-24, commits `17dcafd9` + `8892686a` 포함) — `tool/extract_runtime_hi.sh` 로 `self/runtime_hi.hexa` → `self/runtime_hi_gen.c` (182 줄 신규) 외부 추출. `runtime.c` 의 hand-port `rt_str_*` 6 개 static 블록 (split + lines + repeat + pad_left + pad_right + center) 을 `#include "runtime_hi_gen.c"` 한 줄로 치환. Net on runtime.c: **−153 / +39 = −114 줄 (7079 → 6965)**. 공개 shim 5 개는 유지 (호출자 불변). SSOT 는 이제 `self/runtime_hi.hexa`.
- **Step 5 LANDED** (2026-04-24, commits `5b172ff5` + `b4c5a260`) — **codegen flip**: `self/codegen_c2.hexa` 의 5 string method 디스패치 심볼을 `hexa_str_*` → `rt_str_*` 로 전환 (10줄). 이후 `hexa_str_{lines,repeat,pad_left,pad_right,center}` 5 shim 제거 가능 → **runtime.c 6965 → 6943 (−22 추가)**. 바이너리 `./hexa` 383 424 → 366 496 B (−16 928 B, ~4.4%). `hexa_cc.c` regen + `hexa_v2` 재빌드.

**최종 메트릭 (Step 1-5 누적)**:

| 지표 | Before | After | Δ |
|---|---:|---:|---:|
| `self/runtime.c` | 7 018 | **6 943** | **−75** |
| `self/runtime_hi_gen.c` | — | **182** | **+182 (new SSOT extract)** |
| `./hexa` 바이너리 | — | 366 496 B | Step 5 직후 −16 928 B |
| coverage | — | **187/187 (100.0%)** | — |
| regression | string_method_coverage.hexa | **15/15** | — |
| regression | builtin_mangling.hexa | **12/12** | — |

SSOT 흐름: `self/runtime_hi.hexa` → `tool/extract_runtime_hi.sh` → `self/runtime_hi_gen.c` → `#include` in `self/runtime.c`. M1 full split 시 이 패턴을 도메인별 반복.

## 4. M2 builtin mangling — 현재 진행상황

`runtime.c` 끝자리 `#define setenv hx_setenv` / `#define exec_capture hx_exec_capture` / `#define now timestamp` / `#define push hx_push` 총 **4 건** 의 임시 셔림이 남음 (line 6868·6869·6873·6874).

데몬이 평행으로 roadmap-68 에서 진행 중:
- `2361bc25 feat(m2/roadmap-68): AOT dispatch for now/push` — codegen 측 `now` / `push` 정규 dispatch 추가
- `94b4aa66 test(m2/roadmap-68): builtin_mangling snapshot` — 회귀 probe

M2 종료 시 위 4 `#define` 전원 제거 가능. `grep -c '^#define.*hx_' self/runtime_core.c` == 0 이 anima M2 통과 기준.

## 5. 다음 세션 착수 권장 (2026-04-24 갱신)

**M1-lite 완전 CLOSED** — §3.2 참조. M2 / M3 / M4 / M5 도 이미 done.
본 audit 자료는 이제 **M1 full split** 의 설계 근거로 사용.

### 5.1 M1 full — 도메인별 incremental 이관 (권장 순서)

M1-lite 가 세운 패턴 (`self/runtime_hi.hexa` → `extract_runtime_hi.sh` → `runtime_hi_gen.c` → `#include`) 을 그대로 확장. 도메인당 1 PR:

1. **`file_*` (12 fn)** — **가장 작고 안전** (`read_file` / `write_file` / `append_file` / `file_exists` 등 libc glue). 첫 도메인 확장 증명으로 최적.
2. **`exec_*` (7 fn)** — fork / popen 얇은 래퍼. file_* 패턴 그대로.
3. **base64 / hex (~6 fn)** — 순수 비트 조작, 외부 상태 없음.
4. **JSON (~8 fn)** — 문자열 조작 + 재귀. `str_*` primitive 의존만 extern 주입.
5. **`map_*` 고차 (~10 fn)** — `keys` / `values` / `has_key` / `contains_key` / `remove`. 해시셀 core 유지.
6. **`array_*` 고차 (~40 fn)** — `map` / `filter` / `sort` / `find` / `flat_map` / `reverse`. 가장 크지만 마지막이면 패턴 완숙.
7. **`str_*` 잔여 (~43 fn)** — M1-lite 이외 전부. 가장 뜨거운 path 이므로 벤치 필수.

도메인당 표준 PR: (a) `self/runtime_hi.hexa` 함수 블록 추가, (b) `tool/extract_runtime_hi.sh` 재실행으로 `runtime_hi_gen.c` 재생성, (c) `self/runtime.c` 에서 구 정의 블록 제거, (d) codegen flip (`cg_string_sym` 혹은 도메인 등가), (e) snapshot + coverage 회귀, (f) 바이너리 size diff 기록.

### 5.2 M2 / M5 잔여

- **M2 builtin mangling** — 이미 종결. `#define` 4 건 제거 완료 (별도 PR 로 확인).
- **M5 execvp / Linux ./hexa / CI** — 전부 done (`self/runtime.c:2848` · `build/hexa_cli_linux.hexa` · `.github/workflows/bootstrap.yml` 10/10 green).

### 5.3 비-M1 minor

- interp regression 추적: `pad_start` / `u_floor` 가 AOT PASS 이나 interp FAIL (pre-existing). `cg_string_sym` / gen2_expr dispatch table 점검.
- `.raw` dispatch_tag worktree commit `4a5dc42c` main 병합 ceremony.

---

**참조**:
- anima origin prompt: `../anima/docs/upstream_notes/hexa_lang_full_selfhost_prompt_20260423.md`
- 전 세션 handoff: `./next_session_self_host_m1_m5.md`
- 현재 probe: `../bench/snapshots/string_method_coverage.hexa`

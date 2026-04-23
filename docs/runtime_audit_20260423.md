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

### 3.2 진행 상황 (2026-04-23 후속 세션)

- **Step 1 landed** — `self/runtime_hi.hexa` 생성, 5 함수 + 의존 `rt_str_split` 구현. `runtime_hi_selftest()` 내장 (pad_left/right/repeat/center/lines 결과 검증).
- **Step 2 landed** — `./self/native/hexa_v2 self/runtime_hi.hexa build/runtime_hi.c` 으로 정상 transpile. stage0 interpreter (`./hexa run self/runtime_hi.hexa`) 및 AOT (clang → build/runtime_hi_probe) 양경로 selftest PASS.
- **Debug trap 발견 & 해결** — `build/runtime.c` 에 4/16 stale 복사본이 남아있어 `#include "runtime.c"` 가 `build/` 근처 파일을 먼저 잡는 현상이 M1-lite 초기 빌드를 전부 깨뜨렸음 (pad_left 등이 빈 문자열 반환). 해당 stale 파일 삭제. 향후 build chain 은 `#include` 경로 명시 혹은 cwd 분리 필요.
- **Step 3 보류** — runtime.c 의 5 정의 제거는 전체 codegen 이 아직 `hexa_str_*` 을 호출하고 있어 심볼 재매핑 필요. 다음 세션 작업 (`cg_string_sym` 플립 또는 함수 리네임 `rt_str_* → hexa_str_*`).
- **Step 4~6 보류** — build_stage1 수정 / 벤치마크 / size diff 는 Step 3 이후.

## 4. M2 builtin mangling — 현재 진행상황

`runtime.c` 끝자리 `#define setenv hx_setenv` / `#define exec_capture hx_exec_capture` / `#define now timestamp` / `#define push hx_push` 총 **4 건** 의 임시 셔림이 남음 (line 6868·6869·6873·6874).

데몬이 평행으로 roadmap-68 에서 진행 중:
- `2361bc25 feat(m2/roadmap-68): AOT dispatch for now/push` — codegen 측 `now` / `push` 정규 dispatch 추가
- `94b4aa66 test(m2/roadmap-68): builtin_mangling snapshot` — 회귀 probe

M2 종료 시 위 4 `#define` 전원 제거 가능. `grep -c '^#define.*hx_' self/runtime_core.c` == 0 이 anima M2 통과 기준.

## 5. 다음 세션 착수 권장

본 audit 자료를 기반으로:

- 신규 작업: §3.1 실증 절차 (M1-lite 5 함수). 범위 작고 회귀 안전망 있음.
- 데몬 평행: M2 builtin mangling 마감 — `#define` 4 건 제거 PR.
- 병행 가능: M5 execvp primitive + 3-플랫폼 CI.

M1 본 분할 (≈ 265 hi 함수 전량 이관) 은 실증 단계 통과 후 다단계 PR 로 진행 권장.

---

**참조**:
- anima origin prompt: `../anima/docs/upstream_notes/hexa_lang_full_selfhost_prompt_20260423.md`
- 전 세션 handoff: `./next_session_self_host_m1_m5.md`
- 현재 probe: `../bench/snapshots/string_method_coverage.hexa`

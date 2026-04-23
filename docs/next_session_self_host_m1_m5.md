# 다음 세션 인수인계 — `hxa-20260423-003` Full self-host roadmap M1 + M5

**From**: hexa-lang (2026-04-23, post Phase C.2 fixpoint + M3/M4 closure)
**To**: hexa-lang 차기 세션 (anima upstream blocker 남은 부분 진행)
**Parent prompt**: `../anima/docs/upstream_notes/hexa_lang_full_selfhost_prompt_20260423.md`
**Parent inventory entry**: `state/proposals/inventory.json` → `hxa-20260423-003` (in_progress, prio=95, lang_evolution)

---

## 0. 한 줄 상태

M3 (argv) + M4 (codegen 메서드 완성) 완료. M2 (builtin mangling) 는 데몬이 평행 진행 중
(roadmap-68, 커밋 `2361bc25`/`94b4aa66` 참조). **남은 두 축**: **M1 런타임 2-레이어 분할** ·
**M5 self-hosted driver** (`hexa_driver.hexa` Rust 탈피).

---

## 1. 이번 세션 (2026-04-23) 에 닫은 것

| 커밋 | 범위 | 영향 |
|---|---|---|
| `968900b8` | Phase C.2 fixpoint (`__hexa_ic_` + `__hexa_sl_` + `__hexa_strlit_init` 3-family per-module prefix) + `/tmp/.hexa-runtime/` 격납 | 4개 SSOT 모듈 머지 시 redefinition 에러 제거, APFS freeze 재발 방지 |
| `4238b758` | Linux musl ELF 재빌드 | anima/nexus/airgenome 의 "Linux baseline 미제공" hand-off 해제 |
| `388eece8` | M4 `.pad_start` / `.pad_end` dispatch + `merge_modules_awk` 의 sed → string-literal-aware awk 교체 (`hxa-20260423-006` 자진 리포트) | `hexa cc --regen` 경로가 fixpoint 유지 |
| `9fef570b` | M4 `.char_at` / `.char_code_at` runtime 신규 + dispatch | 남은 M4 string method gap 닫음 |
| `bench/snapshots/string_method_coverage.hexa` | 15-case probe (last_index_of / rfind / find / index_of / pad_start / pad_end / char_at / char_code_at) | 재회귀 smoke |

Mac ARM64 바이너리: `self/native/hexa_v2` (1398656 B) + `./hexa` (366272 B).
Linux x86_64 바이너리: `build/hexa_v2_linux` (1771168 B, sha256 `dea32902a710…`).

## 1.1 관찰 (이번 세션 파생 — 다음 세션에서 재검토)

- `./hexa` 가 `.hexa-cache/<hash>/` 샌드박스 안에서 `tool/*.hexa` 를 실행할 때 `hexa_dir` 해석이 어긋남 — 직접 호출 후 샌드박스 밖 경로가 깨져 `build_stage1.hexa` 등이 cache-miss source 를 찾음. 우회: `hexa_v2` + `clang` 직접 호출. 근본 고침은 `hexa cc` / tool dispatcher 에서 script_path() 기반 hexa_dir 복원 로직 필요.
- 설치 직후 `self/native/hexa_v2: internal error in Code Signing subsystem` 경고 — 재시도 후 정상화. Gatekeeper 상태 노이즈, 기능 영향 없음. macOS 15.x 에서 반복되면 debug.

---

## 2. M1 — 런타임 2-레이어 분할 (장기)

### 2.1 목표

anima 프롬프트 §3 M1 그대로:
- `self/runtime_core.c` ≤ 500 줄: `HexaVal` 정의, arena, scope push/pop, GC 훅, libc glue, signal/flock 만.
- 나머지 ≈ 6500 줄을 `self/runtime_hi.hexa` 로 이관. `hexa_str_*`, `hexa_array_*`, `hexa_map_*`, `hexa_timestamp`, `hexa_base64_*`, `hexa_exec_capture`, `hexa_setenv`, `hexa_read_file`, `hexa_write_file`, JSON, 소켓 등.
- 통과 기준: `nm build/libruntime.a | wc -l` 이 현재 대비 −80%.

### 2.2 하위 스텝 (권장 순서)

1. **Audit**: 현재 `self/runtime.c` 7 k 줄 중 실제로 `hexa_` 접두 C 함수가 몇 개인지 카운트 + 분류.
   ```bash
   grep -c '^HexaVal hexa_' self/runtime.c
   grep -c '^int64_t hexa_' self/runtime.c
   grep -c '^int hexa_' self/runtime.c
   ```
   → 결과표를 `docs/runtime_audit_20260423.md` 로 (anima 가 요청한 형식).
2. **Layer 분류**: 각 함수를 `{core, hi}` 에 라벨. `core` 는 NaN-box / arena / libc-syscall / signal-handler 만. 나머지는 `hi`.
3. **Primitive 추출**: `hi` 레이어가 의존하는 `core` primitive 최소집 정의:
   - `hx_arena_alloc(size)` / `hx_arena_free_frame()`
   - `hx_str_new_from_ptr(ptr, len)` / `hx_str_cstr(hv)`
   - `hx_array_new()` / `hx_array_push_raw(hv, item)` / `hx_array_len(hv)` / `hx_array_get(hv, i)`
   - `hx_map_new()` / `hx_map_get_cstr(hv, k)` / `hx_map_set_cstr(hv, k, v)`
   - `hx_signal_install(sig, fn)` / `hx_flock(path, op)`
   - `hx_read_syscall(fd, buf, n)` / `hx_write_syscall(fd, buf, n)` / `hx_open(path, flags)` / `hx_close(fd)`
   - `hx_exec(cmd)` → fork + exec + wait 래퍼만.
4. **`extern fn` 선언**: `self/runtime_hi.hexa` 상단에 primitive 들을 extern 으로 가져오고, 나머지 high-level 을 `.hexa` 로 재구현.
5. **빌드 체인 변경**: `tool/build_stage1.hexa` 가 `runtime_core.c` + `hexa_v2 runtime_hi.hexa runtime_hi.c` → 2 개 컴파일 유닛 링크하도록.
6. **Soak test**: 모든 `bench/snapshots/*.hexa` + `bench/*.hexa` + `tool/*.hexa` 회귀. 특히 `bench/snapshots/string_method_coverage.hexa` (이번 세션 probe) 는 기존 runtime.c 의 `hexa_str_*` 에 직접 의존하므로 layer 이동 시 경로 재검증 필수.

### 2.3 리스크

- **Bootstrap 순환 의존**: `hexa_v2` 가 `runtime_hi.hexa` 를 트랜스파일하려면 이미 working 한 `hexa_v2` 가 필요. 스테이지 0 에서 `runtime_hi.hexa` 를 stage0 인터프리터로 돌려 core-only 바이너리 생성 → 그 바이너리로 `runtime_hi.hexa` 트랜스파일 → 최종 링크. anima 프롬프트 §2.2 (1) "clean-room 재구축 가능" 조건과 일치.
- **성능**: NaN-boxing + arena 가 inline 되던 것이 function call 경계를 넘게 됨 — `-flto` 혹은 `static inline` 으로 완화. 벤치 드리프트는 `tool/bench_hexa_ir.hexa` 로 측정.
- **ABI**: `HexaVal` 정의가 `runtime_core.c` 에만 존재해야 함. `runtime_hi.hexa` 가 `HexaVal` 의 internal 비트에 접근하려 하면 안 됨 — 전부 primitive 경유.

### 2.4 최소 1차 스플릿 후보 (M1-lite, P0)

본격 분할이 크다면 먼저 "순수 composable" 함수 5 개만 떼어 실증:
- `hexa_str_pad_left` / `hexa_str_pad_right` (이미 `hexa_str_repeat` + `hexa_str_concat` 조합으로 재작성 가능)
- `hexa_str_repeat` (loop + concat)
- `hexa_str_center` (pad_left + pad_right 조합)
- `hexa_str_lines` (split("\n") 래퍼)

이 5 개를 `self/runtime_hi.hexa` 로 이관 + `self/runtime.c` 에서 제거. 빌드 체인은 그대로
(현 `runtime.c` 가 `runtime_hi.hexa` 트랜스파일 산출물을 `#include` 하는 식).
이걸로 (a) 순환 의존 없음 입증, (b) 성능 드리프트 측정, (c) 회귀 test harness 마련.

---

## 3. M5 — Self-hosted driver `hexa_driver.hexa`

### 3.1 목표

anima 프롬프트 §3 M5:
- 서브커맨드: `run | build | fmt | cache | test | self-build | version | help`
- implicit run: 첫 positional 이 `.hexa` 로 끝나고 파일 존재 시 자동 `run` 승격 (Mac CLI 관성 호환).
- content-hash 캐시: `$HOME/.hexa-cache/<sha1(src+deps+compiler_ver)>/exe`
- `exec` → `execvp` 으로 fork 비용 제거.
- 3-플랫폼 CI (mac arm64 / ubuntu x86_64 / ubuntu arm64).
- **통과 기준**: `/opt/homebrew/bin/hexa` Rust 바이너리를 이 레포의 `build/hexa` 로 교체 가능. `*.rs` / `Cargo.toml` / `cargo` 0 줄.

### 3.2 현재 자산

- **Mac CLI**: `./hexa` 가 이미 `self/main.hexa` 로 재작성되어 있음 (`self/main.hexa` ≈ 2334 줄). `cmd_run` / `cmd_build` / `cmd_cc` / `cmd_help` / `cmd_version` / `cmd_status` / `cmd_gc` 등 서브커맨드 존재. 이게 **M5 가 사실상 거의 도착** 했다는 뜻 — Rust driver 가 주관하던 orchestration 을 `.hexa` 가 이미 수행.
- **Linux CLI**: 없음. `build/hexa_v2_linux` (transpile-only) 만 제공. anima 가 여전히 `tool/hexa_linux_shim.bash` 래퍼를 올림.

### 3.3 실제로 남은 M5 작업

1. **Linux ./hexa 바이너리**:
   - `tool/build_stage1.hexa` 의 Linux 분기는 이미 있음 (`-D_GNU_SOURCE -std=gnu11 -lm -ldl`).
   - `tool/build_hexa_v2_linux.hexa` 처럼 `tool/build_hexa_cli_linux.hexa` 작성 — `hexa_v2 self/main.hexa build/stage1/main.c` → `x86_64-linux-musl-gcc -static -I self build/stage1/main.c -o build/hexa_linux`.
   - 배포: `scp build/hexa_linux root@<pod>:/opt/hexa/bin/hexa`.
   - Smoke (pod): `hexa version` + `hexa run <simple.hexa> arg1 arg2`.
2. **implicit run**: `self/main.hexa:cmd_dispatch` (혹은 동등 진입) 에서 `av[1]` 이 `.hexa` 로 끝나는 기존 파일이면 `cmd_run` 으로 auto-promote. 현재는 모르는 subcmd 로 떨어져 help 출력. 약 10 줄 edit + test.
3. **execvp 이어달리기**: `cmd_run` 의 AOT 캐시 히트 경로 (`self/main.hexa:try_cache_exec`) 에서 현재 `exec()` 는 popen/fork+wait. 직접 `execvp` 으로 교체하면 부모 종료 후 자식이 셸 1등 시민처럼 동작 — signal forwarding / PID 테이블 깔끔. runtime primitive 추가 필요 (`hexa_execvp(argv_array)` → runtime.c:fork 없이 현재 프로세스 replace).
4. **CI 3-플랫폼**:
   - `.github/workflows/bootstrap.yml` — `macos-latest` (arm64), `ubuntu-latest` (x86_64), `ubuntu-24.04-arm` (arm64) 매트릭스.
   - 각 러너: (a) `self/native/hexa_v2` 빌드, (b) `./hexa` 빌드, (c) `bench/snapshots/*.hexa` 전 probe 실행, (d) 3 개 아티팩트 업로드.
5. **Rust 탈피 검증**:
   ```bash
   find . -name '*.rs' -o -name 'Cargo.toml' -o -name 'Cargo.lock'
   grep -r 'cargo\|rustc' tool/ .github/
   ```
   결과 0 줄이어야 M5 done.

### 3.4 `/opt/homebrew/bin/hexa` (Rust) 실제 교체 절차

- 현재 `/opt/homebrew/bin/hexa` 는 Homebrew tap 이 배포한 Rust 바이너리로 보임 (macOS 사용자 관성).
- 교체 옵션:
  - (a) **Symlink**: `ln -sf /Users/ghost/core/hexa-lang/hexa /opt/homebrew/bin/hexa` — 사용자 PATH 우선순위 주의.
  - (b) **Homebrew tap 갱신**: `hexa-lang/homebrew-tap` 의 formula 을 stage1 `.hexa` 산출물 기준으로 재작성 + `hexa-lang/{mac-arm64,linux-x86_64,linux-arm64}-binary` 3 릴리스 아티팩트를 tag + download.
  - (b) 가 정석. (a) 는 단일 사용자 실증용.

---

## 4. 다음 세션 우선순위 (H-MINPATH 권장 순서)

1. **M5 Linux ./hexa 빌드** — `tool/build_hexa_cli_linux.hexa` 추가 (약 50 줄, `build_hexa_v2_linux.hexa` 복사-수정). anima pod 배포 즉시 anima `tool/hexa_linux_shim.bash` 제거 가능 → 교차 저장소 블로커 해제.
2. **M5 implicit run** — 10 줄 edit + probe. 즉시 가치.
3. **M1-lite 5-fn 스플릿** — M1 의 "실증" 단계. 회귀 안전망 (`bench/snapshots/string_method_coverage.hexa`) 이미 있음. `hexa_str_pad_*` 등이 `runtime_hi.hexa` 에서 작동하면 큰 분할에 신뢰 확보.
4. **M1 full audit 문서화** — `docs/runtime_audit_20260423.md`. 의사결정 근거 남김.
5. **M5 execvp primitive** — runtime.c 변경 (primitive 1 개 추가 + codegen entry). 순수 성능/UX 이슈.
6. **M5 CI 매트릭스** — 3-플랫폼 러너. 다른 모든 것이 닫힌 뒤.

각 단계 후 anima `docs/upstream_notes/hexa_lang_full_selfhost_prompt_20260423.md` 의 해당 체크리스트 항목 update, `state/proposals/inventory.json:hxa-20260423-003` 의 done_note 누적.

---

## 5. 관련 참조

- anima origin prompt: `../anima/docs/upstream_notes/hexa_lang_full_selfhost_prompt_20260423.md`
- anima session handoff: `../anima/docs/session_handoff_20260423.md`
- roadmap-65 (M3 argv): 커밋 `9bdf2336`
- roadmap-68 (M2 builtin mangling, 데몬 진행 중): 커밋 `2361bc25` · `94b4aa66`
- 이번 세션 M4 커밋: `388eece8` · `9fef570b`
- probe: `bench/snapshots/string_method_coverage.hexa`

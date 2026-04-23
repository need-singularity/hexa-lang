# Cross-repo notifications — 2026-04-23 (post M3/M4 closure)

본 문서는 sister repo 쪽 작업을 unblock 하기 위한 공지 모음.
각 섹션을 해당 repo 의 `proposal_inbox` 혹은 maintainer 로 전달.

---

## ▶ anima — M4 codegen 메서드 완료 알림 (결손-E 전체 해제)

**발신**: hexa-lang (2026-04-23, post 커밋 `9fef570b`)
**수신**: anima (`docs/upstream_notes/hexa_lang_full_selfhost_prompt_20260423.md` §1.2 결손-E / §3 M4 / §4 우선순위 (2) 에 대한 응답)
**관련 블로커**: hxa-20260423-003 M4 슬라이스, anima I7 / I11

### 변경 요약

- `.last_index_of` / `.rfind` — codegen dispatch + runtime 완료 (`hxa-20260422-002`, 커밋 `968900b8` 선행)
- `.find` / `.index_of` — 기존 dispatch 유지 + 회귀 probe 편입
- `.pad_start` / `.pad_end` — JS/Python naming, 기존 `hexa_str_pad_left` / `_pad_right` 재사용 (커밋 `388eece8`)
- `.char_at` / `.char_code_at` — runtime 신규 2 개 + dispatch (커밋 `9fef570b`)

### 런타임 의미론

- `char_at(i)` → 1-byte string, OOB 시 `""`
- `char_code_at(i)` → byte value 0..255 (int), OOB 시 `-1`
- **바이트 해석 only** — UTF-8 codepoint 디코딩은 별도 `.code_point_at` 이슈로 분리

### anima 쪽 cleanup 권장

- `an11_a_verifier.hexa` 및 기타 `_last_idx_char(s, ch)` 수작 구현 제거 → `s.last_index_of(str(ch))` 직행.
- `skip0=2` 분기 (argv duplication 우회) 는 **M3 완료** (커밋 `9bdf2336`, `real_args()` + `script_path()`) 라 이미 제거 가능. 별도 후속 확인 요청.

### 배포

- Mac ARM64: `self/native/hexa_v2` 신규 (1398656 B)
- Linux x86_64: `build/hexa_v2_linux` 신규 (1771168 B, sha256 `dea32902a710d8eceb4598175eaa50c6495af1c166d8a7818bac917e2166cc58`)
- anima pod 배포: `scp build/hexa_v2_linux root@<pod>:/opt/hexa/bin/hexa_v2` → `chmod +x`

### probe

`bench/snapshots/string_method_coverage.hexa` (hexa-lang 이번 세션 추가) — 15 case 전원 PASS. anima 쪽에서 재실행해 동일 결과 확인 요청.

---

## ▶ airgenome — Linux x86_64 baseline ELF 갱신 알림

**발신**: hexa-lang (2026-04-23, post 커밋 `4238b758` · `9fef570b`)
**수신**: airgenome maintainer
**관련 블로커**: agm-20260422-003 / agm-20260422-006 (resource_gap, "Linux x86_64 binary 미제공 / macOS ARM64 만 빌드됨 → pod bootstrap"), nxs-20260422-006, anima-20260422-003

### 변경 요약

`build/hexa_v2_linux` 가 Phase C.2 namespacing fixpoint + `/tmp/.hexa-runtime/` 격납 + M4 전체 메서드 슬라이스 포함으로 재빌드됨:

| 항목 | 값 |
|---|---|
| 경로 | `hexa-lang/build/hexa_v2_linux` |
| 크기 | 1 771 168 bytes |
| 포맷 | ELF 64-bit LSB executable, x86-64, statically linked (musl), version 1 (SYSV) |
| sha256 | `dea32902a710d8eceb4598175eaa50c6495af1c166d8a7818bac917e2166cc58` |
| 빌드 툴체인 | Homebrew `x86_64-linux-musl-gcc` — `-O2 -std=gnu11 -D_GNU_SOURCE -static -lm` |
| 소스 헤드 | `9fef570b feat(m4): .char_at / .char_code_at codegen + runtime` |

### pod 배포 스크립트

```
scp hexa-lang/build/hexa_v2_linux root@<pod>:/opt/hexa/bin/hexa_v2
ssh root@<pod> 'chmod +x /opt/hexa/bin/hexa_v2 && /opt/hexa/bin/hexa_v2 --help'
```

### 결함 리포트 가이드

이후 pod 에서 Linux 특화 버그 관측 시:
1. repro `.hexa` 최소화 → `/opt/hexa/bin/hexa_v2 repro.hexa /tmp/repro.c` 산출
2. `/tmp/repro.c` + 원본 `.hexa` + `uname -a` 첨부로 airgenome proposal_inbox 제출
3. 카테고리 `resource_gap` 혹은 `lang_gap` 태그

### 아직 미제공

- **Linux `hexa` CLI** (run/build/cache 풀세트) 는 여전히 없음 — transpile-only `hexa_v2_linux` 뿐. anima `tool/hexa_linux_shim.bash` 는 당분간 유지.
- 해결은 M5 (`docs/next_session_self_host_m1_m5.md` §3) — `tool/build_hexa_cli_linux.hexa` 추가 예정.

---

## ▶ nexus — M3 argv canonical API 알림 (기존 sig 유지용)

**발신**: hexa-lang (2026-04-23, 기 완료 — `9bdf2336` 알림 재공지)
**수신**: nexus (argv-consumer 도구 전수)

roadmap-65 M3 이미 완료 — `real_args()` / `script_path()` 표준 API 사용 가능.
`argv()` legacy API 도 유지 (backward-compat). nexus `tool/` 내 인덱싱 도구가
`args[1]` 경로 해석 이슈 있으면 `real_args()` / `script_path()` 로 전환 권장.

코드 예:
```hexa
let av = real_args()      // user 인자만 (stage0/AOT/native 동일)
let me = script_path()    // 런처 식별자 (빈값 아님 보장)
```

---

## 라우팅 참고

- anima: `/Users/ghost/core/anima/state/proposals/inventory.json` 에 `hxa-20260423-M4-done` 등 엔트리 추가 권장.
- airgenome: `/Users/ghost/core/airgenome/state/proposals/inventory.json` 에 `hxa-linux-elf-20260423` 엔트리 추가 권장.
- nexus: 동일 패턴.

각 repo 는 본 문서 복사 필요 없음 — 인벤토리 entry 의 `prompt_ref` 로 본 경로(`hexa-lang/docs/cross_repo_notifications_20260423.md`) 참조하면 SSOT 유지.

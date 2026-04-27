# hexa-lang auto-marker protocol (2026-04-26)

## 목적

silent-land 근본 해결. subagent / cron / hook 환경에서 마지막 메시지가 truncate / rate-limit으로 사라져도, hexa-lang 인터프리터가 사용자 `.hexa` 스크립트의 `main()` 정상 종료 직후 disk 위에 marker 파일을 남겨 "commit signal"을 보존한다.

기존 prompt-level marker (subagent system prompt에 marker write 명시)는 best-effort. interpreter-level은 forced — 사용자 스크립트가 어떤 형태든 정상 종료되기만 하면 자동으로 마커가 남는다.

## 동작

`hexa run <file>` 또는 `hexa <file.hexa>` 호출로 진입한 cmd_run 의 모든 종료 경로 (cache fast-path / 일반 interp / FAIL exit) 직전에 `auto_marker_write(file, rc)` 호출.

### marker 경로

```
<HEXA_MARKER_DIR or "state/markers">/<basename(file, .hexa)>_<ts><suffix>
```

- `ts` = epoch seconds (또는 `HEXA_FROZEN_TIME` 동결값)
- `suffix` = `.marker` (rc=0) | `_FAILED.marker` (rc≠0)

### marker 내용 (JSON one-line)

```json
{"source":"<absolute or relative file path>","exit":<rc>,"fingerprint":"<8 hex>","ts":<epoch>}
```

- `fingerprint` = `shasum <file> | head -c 8` (FNV-class fast hash)

## 환경 변수

| 변수 | 기본값 | 효과 |
|------|--------|------|
| `HEXA_NO_MARKER` | (미설정) | `=1` 일 때 marker 작성 skip (backward-compat opt-out) |
| `HEXA_MARKER_DIR` | `state/markers` | marker 디렉토리 override |
| `HEXA_FROZEN_TIME` | (미설정) | epoch seconds 동결 → byte-identical marker (테스트/재현용) |

## 검증

```bash
# Positive: 정상 실행 → marker 생성
HEXA_MARKER_DIR=/tmp/markers hexa run sample.hexa
ls /tmp/markers/sample_*.marker

# Negative-1: opt-out → marker 없음
HEXA_NO_MARKER=1 hexa run sample.hexa
# /tmp/markers 갱신 없음 확인

# Negative-2: rc=1 → _FAILED.marker
hexa run failing.hexa
ls /tmp/markers/failing_*_FAILED.marker

# Byte-identical: 같은 frozen time → sha256 동일
HEXA_MARKER_DIR=/tmp/m1 HEXA_FROZEN_TIME=1700000099 hexa run sample.hexa
HEXA_MARKER_DIR=/tmp/m2 HEXA_FROZEN_TIME=1700000099 hexa run sample.hexa
shasum /tmp/m1/*.marker /tmp/m2/*.marker  # 동일 sha256
```

## 산출물

- `self/main.hexa`: `auto_marker_*` 5개 helper 함수 + `cmd_run` 3개 종료 경로 hook
- `self/main.hexa.bak_pre_auto_marker_20260426`: 패치 전 백업 (1차 시도 시 작성됨, 본 사이클이 실제 hook 추가)
- `test/test_auto_marker.hexa`: standalone helper 검증 스크립트
- `docs/auto_marker_protocol_20260426.md`: 본 문서

## Backward compatibility

- 기존 anima 도구 100+ 모두 영향 없음. marker 생성은 부가 작용일 뿐, 기존 stdin/stdout/stderr/rc 계약은 그대로.
- `HEXA_NO_MARKER=1` 로 완전 disable 가능 (전역 환경 변수로 fleet-wide opt-out).
- `state/markers/` 디렉토리는 `mkdir -p` 로 자동 생성, 기존 디렉토리 충돌 없음.
- helper 5개 함수 모두 `auto_marker_` 접두사 — 네임스페이스 분리.

## 알려진 한계

1. **Native binary 재컴파일 필요**: 본 패치는 `self/main.hexa` source-level. 사용자가 실제 `~/.hx/bin/hexa` 를 통해 받는 native Mach-O는 재빌드되어야 변경 발효. 본 사이클은 source patch + standalone interp 검증 까지. 재빌드는 별도 사이클.
2. **interp meta-driver 검증**: `hexa_interp.real self/main.hexa run <script>` 형태로 메타 인터프리터를 통해 실행하면 native builtin (`mono_ns`, `exec_stream`) 일부가 미정의여서 stderr 노이즈가 나오지만, marker write 자체는 정상 작동 확인.
3. **Cache miss 후 fall-through path**: cache 시도 실패 후 일반 interp 경로로 fall-through 할 때 marker는 일반 경로 종료에서 1회만 작성 — 중복 없음 (cache 시도 실패 시 try_cache_exec rc=-1 반환, marker write 호출 안됨).

## 다음 단계 후보

- **A: 전체 fleet rollout** — 재빌드 후 모든 anima 도구에 marker enforcement. 1주 monitoring 으로 실패율 측정.
- **B: opt-in 만 유지** — 기본 OFF, `HEXA_AUTO_MARKER=1` 필요한 케이스에만. 영향 범위 최소화. *현 권고: A.*
- **C: marker GC** — `state/markers/` 가 무한 증가하지 않도록 90일+ marker 자동 청소. cron 또는 cmd_run 시작 시 probabilistic 트리거.
- **D: marker-aware subagent dispatcher** — agent 종료 후 marker 검사로 silent-truncate 자동 감지 + retry.

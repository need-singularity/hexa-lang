# hexa-lang stdlib upstream — ANU QRNG client landing

land date: 2026-05-02
modules:
- `stdlib/net/http_client.hexa` (120 LOC) — curl-based HTTP GET primitive
- `stdlib/qrng_anu.hexa` (187 LOC) — ANU Quantum RNG REST wrapper

motivation: anima 4-Tier QRNG 표 T1 row (ANU free REST API, length≤1024/req,
no auth) 를 sister-repo 가 stub 없이 그대로 import 할 수 있는 stdlib 단위
land. 호출자 측에서 sleep-rate-limit + chunking + JSON parse 재발명을
제거.

## §1 — 사용법 (anima 또는 다른 sister-repo)

```hexa
use "stdlib/qrng_anu"

fn collect_quantum_seed() {
    // 기본은 fixture (deterministic 0..255 cycle, entropy ZERO).
    // CI / 테스트에서 안전하게 흐름만 검증.
    let seed_bytes = qrng_anu_uint8(64)

    // 실제 ANU API 를 칠 때 — 두 가지 방법:
    //   1) ANIMA_QRNG_LIVE=1 환경변수 설정 후 qrng_anu_uint8(N) 호출
    //   2) 호출자가 정책상 명시적으로 결정한 경우 qrng_anu_uint8_live(N)
    let real_quantum = qrng_anu_uint8_live(8)

    // length 가 1024 초과면 자동 분할 + 호출 사이 sleep_ms 휴면.
    // ANU FAQ 권고 ~1 req/min → sleep_ms_between=60000 안전선.
    // 단 fixture 모드에선 sleep 무시 (zero entropy).
    let big = qrng_anu_chunked(8192, 60000)
}
```

import path 는 hexa-lang stdlib 정규 경로:
- `use "stdlib/qrng_anu"` (qrng wrapper)
- `use "stdlib/net/http_client"` (http get 직접)
- `use "stdlib/json_object"` (이미 land — 응답 parse 헬퍼)

세 모듈은 자동 의존: qrng_anu 가 http_client + json_object 를 use 하므로
caller 는 qrng_anu 한 줄만 import 해도 충분.

## §2 — API surface

### http_client

```
pub fn http_get(url: string, timeout_sec: int) -> string
  - 200 OK body 반환. non-2xx / curl 실패 / DNS / TLS 오류 → "".
  - timeout_sec <= 0 면 30초 default. integer 단위 (curl --max-time).
  - --fail flag 사용 → 4xx/5xx 자동 silent fail.

pub fn http_get_status(url: string, timeout_sec: int) -> map
  - { "status": int, "body": string, "ok": bool }
  - status==0 = curl 자체 실패 (DNS/TLS/timeout).
  - status>=200 && <300 → ok=true.
  - 4xx/5xx 도 status 로 회수 (--fail 미사용).

pub fn http_get_or_default(url: string, timeout_sec: int, default_body: string) -> string
  - ok 면 body, 아니면 default. error log 없이 silent fallback.
```

### qrng_anu

```
pub fn qrng_anu_uint8(length: int) -> []int
  - 1..1024 byte (uint8 0-255) array. range 위반 → [].
  - ANIMA_QRNG_LIVE!=1 → fixture path (deterministic [0,1,...,255,0,1,...]).
  - ANIMA_QRNG_LIVE==1 → ANU API 호출.

pub fn qrng_anu_uint8_live(length: int) -> []int
  - env 우회 explicit live path. 정책상 진짜 ANU 호출이 필요한 callsite.
  - 동일 range 검증, 동일 반환.

pub fn qrng_anu_chunked(total_bytes: int, sleep_ms_between: int) -> []int
  - total_bytes > 1024 일 때 자동 1024-byte chunk + 사이 sleep_ms 휴면.
  - 마지막 chunk 는 잔여만. 중간 chunk 실패 시 누적분만 반환 (부분-결과).
  - sleep_ms_between=0 → no-sleep (mock/test 친화). 진짜 호출은 60000 권장.

pub fn qrng_anu_parse_response(text: string) -> map
  - 응답 JSON → { "success": bool, "data": []int, "length": int }.
  - network unit 분리 helper. caller 가 직접 응답 검증 가능.

pub fn qrng_anu_live_enabled() -> bool
  - env("ANIMA_QRNG_LIVE") == "1" 체크.
  - hexa-lang interp 에서 nested-call 후 env 가시성 race 가 관측되므로
    selftest/loop 안에서는 main top 에서 env 를 미리 캡처하는 패턴 권장.
```

### env override

`ANU_QRNG_BASE` 환경변수로 endpoint mirror/proxy 가능. 기본값:
`https://qrng.anu.edu.au/API/jsonI.php`.

## §3 — ANIMA_QRNG_LIVE flag

- **default (unset 또는 != "1")** — fixture path. 호출 안 나가, deterministic
  cycle, byte-identical reproducibility 보장. CI 와 selftest 에서 안전.
- **"1"** — `qrng_anu_uint8(N)` 가 진짜 ANU API 호출. timeout 30s,
  실패 시 빈 array.
- 명시적 강제: `qrng_anu_uint8_live(N)` — env 무시, 항상 live.

selftest 안에서는 `let live_env = env("ANIMA_QRNG_LIVE")` 로 main top 에서
한 번 읽고, 이후 분기에서 그 변수를 사용한다. 함수-내부 중첩 호출 후에는
`env()` 의 가시성이 hexa-lang interp 에서 일시적으로 잃어버리는 동작이
관측됐기 때문 (raw#10 caveat 5 참조).

## §4 — raw#10 caveats

1. **외부 binary curl 의존**. macOS / 대부분의 Linux 배포판은 base 설치이지만,
   minimal Alpine + airgapped 환경에서는 curl 이 없을 수 있다. 실패 시
   `http_get` 가 빈 string 을 반환할 뿐 panic 없음. 호출자 측 fallback 책임.

2. **ANU rate limit 책임은 호출자**. 본 모듈은 chunked variant 의
   `sleep_ms_between` 를 강제하지 않는다 (0 도 허용). FAQ 권고 ~1 req/min
   을 무시한 burst 호출이 ANU 에서 차단될 수 있다. anima 측 정책으로
   sleep_ms_between=60000 minimum 권장.

3. **JSON parse strict — malformed 응답에 panic**. 본 stdlib 의
   `qrng_anu_parse_response` 는 `json_parse_object` 에 의존하는데, 그
   wrapper 는 `json_parse(garbage)` 가 builtin 단계에서 죽는 케이스를
   막아주지 못한다. 따라서 ANU 가 HTML error page (예: maintenance) 를
   반환하면 process abort. 호출자가 응답 길이 ≥ ~30byte + `{` prefix
   확인하는 pre-filter 추가 권장. selftest 는 이 케이스를 회피하기 위해
   empty-body 와 missing-success 두 가지 fallback 만 검증한다.

4. **length validation 1..1024**. ANU API spec 의 max-per-req 는 1024 이고,
   본 모듈은 그 위/아래 모두 빈 array 로 reject. `qrng_anu_chunked` 는 N>1024
   를 자동 분할로 허용. caller 가 0 / 음수 length 를 의도적으로 전달하면
   silent empty 반환 — 입력 검증은 caller 가 해야 한다.

5. **env() visibility race**. hexa-lang interp 의 .hexa-cache 와 nested-call
   상호작용에서 `env("X")` 가 같은 호출 흐름의 두 번째 호출 이후 빈 string
   으로 보이는 비결정적 동작이 관측됨 (2026-05-02). 따라서 selftest 의 live
   probe 는 main top 의 한 번-읽기 후 분기 패턴을 강제하고, runtime 정책은
   `qrng_anu_uint8_live()` explicit path 로 우회 가능하게 했다.

6. **fixture entropy ZERO**. default 경로 [0,1,2,...,255,0,1,...] 는 절대
   보안 (key generation, nonce, OTP) 용도로 사용 금지. CI 통과만을 위한
   reproducibility 단위.

## §5 — 다른 RNG (T2 IBM Q / T3 IDQ / T4 KAIST) 추가 시 abstraction

같은 `stdlib/qrng_<vendor>.hexa` 패턴을 따른다 — `http_client` 를 use 하고,
`{success, data, length}` map 으로 정규화하는 `_parse_response` 와
`_uint8_live(length)` + fixture 분기 + chunked variant 4점 세트를 유지.
공통 cross-vendor `qrng.hexa` 는 land 보류 — vendor 별 auth (IBM Q
token, IDQ HTTPS+POST, KAIST WebSocket) 와 unit (uint8 vs uint16 vs hex
string) 이 너무 달라서 단일 abstraction 으로 묶으면 누수가 더 크다. caller
가 vendor 선택 정책을 하나 위에서 결정하고 vendor-specific 모듈 한 개만
use 하는 구조 권장.

---

본 land 후 anima 측 후속:

```hexa
// anima/tool/anima_qrng_collect.hexa 같은 신규 wrapper:
use "stdlib/qrng_anu"

fn collect_quantum_seed_for_eeg(n_bytes: int) {
    return qrng_anu_chunked(n_bytes, 60000)  // 1 req/min 권장 sleep.
}
```

selftest 결과: http_client 5/5 + qrng_anu 20/20, byte-identical 검증 완료.
live API hit 확인 (2026-05-02 실측 b0=33 b3=175 entropy varies).

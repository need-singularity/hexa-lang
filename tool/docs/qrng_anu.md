# `qrng_anu`

_qrng_anu_

**Source:** [`stdlib/qrng_anu.hexa`](../../stdlib/qrng_anu.hexa)  

## Overview

ANU Quantum Random Number Generator REST API client.
Source: https://qrng.anu.edu.au/
Endpoint: https://qrng.anu.edu.au/API/jsonI.php?length=N&type=uint8
Response shape: {"data":[ints 0-255],"length":N,"success":true}

이 stdlib 모듈은 anima 같은 sister-repo 가 ANU QRNG 를 그대로 끌어
쓸 수 있도록 thin wrapper 를 제공한다.

Tier 표 (anima 4-Tier QRNG 준비표) T1 row 의 운영 단위 — length≤1024
per request, free, no auth, REST GET. HW rate 5.7 Gbps 이지만 internet
throttle 때문에 client effective unit 은 byte/req chunk.

Rate limit: ANU FAQ 권고 ~1 req/min. 본 모듈의 chunked variant 는
호출자가 sleep_ms_between 으로 명시 — 무지각 burst 방지.

Live network 호출은 ANIMA_QRNG_LIVE=1 일 때만 실제 API 를 친다 (CI
안전). default 는 fixture/mock 경로.

API:
  qrng_anu_uint8(length) -> []int
    1..1024 byte (uint8) array. validate range; out-of-range → [].
  qrng_anu_chunked(total_bytes, sleep_ms_between) -> []int
    length>1024 일 때 자동 분할 + 호출 사이 sleep_ms_between ms 휴면.
  qrng_anu_parse_response(text) -> map
    {"success": bool, "data": [int], "length": int}
    network 분리 unit-test 친화 helper.
  qrng_anu_live_enabled() -> bool
    ANIMA_QRNG_LIVE=1 체크.

## Functions

| Function | Returns |
|---|---|
| [`qrng_anu_live_enabled`](#fn-qrng-anu-live-enabled) | `bool` |
| [`qrng_anu_parse_response`](#fn-qrng-anu-parse-response) | `_inferred_` |
| [`qrng_anu_uint8`](#fn-qrng-anu-uint8) | `_inferred_` |
| [`qrng_anu_uint8_live`](#fn-qrng-anu-uint8-live) | `_inferred_` |
| [`qrng_anu_chunked`](#fn-qrng-anu-chunked) | `_inferred_` |

### <a id="fn-qrng-anu-live-enabled"></a>`fn qrng_anu_live_enabled`

```hexa
pub fn qrng_anu_live_enabled() -> bool
```

**Parameters:** _none_  
**Returns:** `bool`  

qrng_anu_live_enabled — ANIMA_QRNG_LIVE=1 일 때 true.
CI / 자동 테스트는 default 로 false → fixture path 사용.

### <a id="fn-qrng-anu-parse-response"></a>`fn qrng_anu_parse_response`

```hexa
pub fn qrng_anu_parse_response(text: string)
```

**Parameters:** `text: string`  
**Returns:** _inferred_  

qrng_anu_parse_response — JSON 응답 텍스트를 받아 {success, data, length}
형태의 map 으로 환원. data 는 hexa array of int (0..255).
응답이 비정상이면 success=false + 빈 data.

### <a id="fn-qrng-anu-uint8"></a>`fn qrng_anu_uint8`

```hexa
pub fn qrng_anu_uint8(length: int)
```

**Parameters:** `length: int`  
**Returns:** _inferred_  

qrng_anu_uint8 — single ANU API call, 1..1024 byte (uint8) array.
Out-of-range length → empty array (caller 가 인자 검증 책임).
Network 실패 / success=false → 빈 array.

Live mode (ANIMA_QRNG_LIVE=1) 가 아니면 deterministic fixture 반환:
length 길이의 [0,1,2,...,255,0,1,...] (byte 사이클). 이는 selftest
친화이며 entropy 없음 — 절대 보안용도 사용 금지.

Note: ANIMA_QRNG_LIVE env 는 stdlib import + nested-call 시 하위 함수
에서 빈 string 으로 보이는 hexa-lang interp 캐시 race 가 관측됨
(2026-05-02). 따라서 caller 가 명시적으로 live mode 를 강제하고 싶을 때는
`qrng_anu_uint8_live(length)` 변종을 사용한다.

### <a id="fn-qrng-anu-uint8-live"></a>`fn qrng_anu_uint8_live`

```hexa
pub fn qrng_anu_uint8_live(length: int)
```

**Parameters:** `length: int`  
**Returns:** _inferred_  

qrng_anu_uint8_live — env 우회 explicit live path. caller 가 정책상
진짜 ANU 호출이 필요하다고 직접 결정한 경우에만 호출. range check 동일.

### <a id="fn-qrng-anu-chunked"></a>`fn qrng_anu_chunked`

```hexa
pub fn qrng_anu_chunked(total_bytes: int, sleep_ms_between: int)
```

**Parameters:** `total_bytes: int, sleep_ms_between: int`  
**Returns:** _inferred_  

qrng_anu_chunked — total_bytes 가 1024 초과일 때 자동 chunk + 호출 사이
sleep_ms_between ms 휴면. 마지막 chunk 는 잔여만.

Returns concatenated byte array. 중간 실패 chunk 는 [] 로 채우지 않고
즉시 abort — partial-truncation 응답 방지. 누적된 만큼만 반환.

Live 가 아니면 동일 fixture 분기.

---

← [Back to stdlib index](README.md)

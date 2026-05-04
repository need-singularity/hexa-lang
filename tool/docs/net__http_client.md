# `net/http_client`

_net/http_client_

**Source:** [`stdlib/net/http_client.hexa`](../../stdlib/net/http_client.hexa)  

## Overview

curl-based HTTP GET primitive. Cross-platform (BSD/Linux) without
new C builtins.

Why shell-out to curl?
  stdlib/net/{socket,http_request,http_server} 는 server-side
  raw-TCP framework 위주이고 client GET (특히 HTTPS+TLS) 가 빠져 있다.
  raw socket → TLS handshake 를 hexa-side 에서 다시 짤 수도 있겠지만
  그건 새 C runtime + crypto dep + cert chain 부담을 부른다.
  curl 은 BSD (macOS) / GNU/Linux 양쪽에서 base 설치이고, ω-hexa-1
  portability 제약 (H1) 안에서 가장 가벼운 client-side HTTP 솔루션이다.

Tradeoffs (raw#10):
  · 외부 binary 의존: curl 미설치 환경에서는 동작 불가.
  · stdout 만 읽음 — header/status code 별도 옵션 (-w '%{http_code}')
    으로 추가 fetch. body 만 필요할 때 fast-path.
  · timeout 정확도는 curl --max-time 에 위임 — 1초 미만 단위 미지원.
  · binary body 는 hexa-string 으로 들어와서 NUL byte 발생 가능 시
    truncate 위험. JSON/text response 만 안전. binary 가 필요하면
    callsite 가 base64 stdout 옵션 추가 (-w 미지원, 별도 file dump).

API:
  http_get(url, timeout_sec) -> string
    200 OK 본문 반환. non-200 또는 curl 실패 시 빈 string + eprintln.
  http_get_status(url, timeout_sec) -> map
    {"status": int, "body": string, "ok": bool}
    status 0 = curl 자체 실패 (timeout/DNS/TLS).

Selftest hint: stdlib/test/test_http_client.hexa.

_shell_escape — single-quote 로 감싸고 내부 single-quote 는 escape.
curl URL 에 들어가는 사용자-제공 문자열 보호. proc.hexa 의 동일 패턴.

## Functions

| Function | Returns |
|---|---|
| [`http_get`](#fn-http-get) | `string` |
| [`http_get_status`](#fn-http-get-status) | `_inferred_` |
| [`http_get_or_default`](#fn-http-get-or-default) | `string` |

### <a id="fn-http-get"></a>`fn http_get`

```hexa
pub fn http_get(url: string, timeout_sec: int) -> string
```

**Parameters:** `url: string, timeout_sec: int`  
**Returns:** `string`  

http_get — body-only GET. 200 OK + body 반환, 실패 시 "".

curl options:
  -s        silent (no progress meter)
  -S        show error messages on stderr (silent 와 함께 쓸 때 표시)
  -L        follow redirects
  --max-time N  hard timeout (seconds, integer)
  --fail    HTTP >= 400 일 때 exit code 22 → exec() result empty.

Note: --fail 활용으로 status check 단순화. status 가 필요하면
      http_get_status() 사용.

### <a id="fn-http-get-status"></a>`fn http_get_status`

```hexa
pub fn http_get_status(url: string, timeout_sec: int)
```

**Parameters:** `url: string, timeout_sec: int`  
**Returns:** _inferred_  

http_get_status — 풀 status + body. status==0 일 때 curl 자체 실패.

curl -w "\n%{http_code}" 로 stdout 끝에 status code 한 줄 추가.
body 와 status 분리는 마지막 newline 기준 split.

### <a id="fn-http-get-or-default"></a>`fn http_get_or_default`

```hexa
pub fn http_get_or_default(url: string, timeout_sec: int, default_body: string) -> string
```

**Parameters:** `url: string, timeout_sec: int, default_body: string`  
**Returns:** `string`  

http_get_or_default — 200 OK 면 body, 그 외에는 default. error log 없이.
CI / mock-friendly path: network 실패해도 흐름이 끊기지 않도록.

---

← [Back to stdlib index](README.md)

# `net/http_request`

_net/http_request_

**Source:** [`stdlib/net/http_request.hexa`](../../stdlib/net/http_request.hexa)  

## Overview

HTTP/1.1 요청 파서. self/serve/http_server.hexa (컴파일러 내부)의
parse_request / parse_query 를 사용자 응용 코드용으로 포팅한 버전.

Request 는 struct 대신 map 으로 표현한다(기존 서버와의 호환). 맵 키:
  method  : string    ("GET", "POST", ...)
  path    : string    ("/api/ping")
  query   : string    (물음표 뒤 전체 raw 문자열)
  body    : string
  remote  : string    (상위 계층이 채워넣음; 파서는 "")
  headers : map       (키 소문자 정규화)

-- Parse raw HTTP request --

## Functions

| Function | Returns |
|---|---|
| [`parse_request`](#fn-parse-request) | `_inferred_` |
| [`parse_query`](#fn-parse-query) | `_inferred_` |
| [`content_length_of`](#fn-content-length-of) | `_inferred_` |

### <a id="fn-parse-request"></a>`fn parse_request`

```hexa
pub fn parse_request(raw)
```

**Parameters:** `raw`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-parse-query"></a>`fn parse_query`

```hexa
pub fn parse_query(qs)
```

**Parameters:** `qs`  
**Returns:** _inferred_  

-- Parse query string "a=1&b=2" into map --

### <a id="fn-content-length-of"></a>`fn content_length_of`

```hexa
pub fn content_length_of(request)
```

**Parameters:** `request`  
**Returns:** _inferred_  

-- Helper: get Content-Length header as int (0 if absent or invalid) --

---

← [Back to stdlib index](README.md)

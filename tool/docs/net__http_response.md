# `net/http_response`

_net/http_response_

**Source:** [`stdlib/net/http_response.hexa`](../../stdlib/net/http_response.hexa)  

## Overview

HTTP/1.1 응답 빌더 + 고수준 헬퍼.
Response 는 맵 형식:
  status       : int
  content_type : string
  headers      : map   (추가 헤더; 선택)
  body         : string

-- Build HTTP response string from response map --

## Functions

| Function | Returns |
|---|---|
| [`build_response`](#fn-build-response) | `_inferred_` |
| [`json_ok`](#fn-json-ok) | `_inferred_` |
| [`json_error`](#fn-json-error) | `_inferred_` |
| [`text_ok`](#fn-text-ok) | `_inferred_` |
| [`cors_preflight`](#fn-cors-preflight) | `_inferred_` |
| [`sse_response`](#fn-sse-response) | `_inferred_` |
| [`sse_event`](#fn-sse-event) | `_inferred_` |
| [`sse_done`](#fn-sse-done) | `_inferred_` |

### <a id="fn-build-response"></a>`fn build_response`

```hexa
pub fn build_response(resp)
```

**Parameters:** `resp`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-json-ok"></a>`fn json_ok`

```hexa
pub fn json_ok(data)
```

**Parameters:** `data`  
**Returns:** _inferred_  

-- JSON helpers --
json_stringify 빌트인이 등록되어 있으므로 그대로 사용.

### <a id="fn-json-error"></a>`fn json_error`

```hexa
pub fn json_error(status, message)
```

**Parameters:** `status, message`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-text-ok"></a>`fn text_ok`

```hexa
pub fn text_ok(body)
```

**Parameters:** `body`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-cors-preflight"></a>`fn cors_preflight`

```hexa
pub fn cors_preflight()
```

**Parameters:** _none_  
**Returns:** _inferred_  

-- CORS preflight helper (for OPTIONS requests) --

### <a id="fn-sse-response"></a>`fn sse_response`

```hexa
pub fn sse_response(body)
```

**Parameters:** `body`  
**Returns:** _inferred_  

-- SSE (Server-Sent Events) --

### <a id="fn-sse-event"></a>`fn sse_event`

```hexa
pub fn sse_event(data)
```

**Parameters:** `data`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-sse-done"></a>`fn sse_done`

```hexa
pub fn sse_done()
```

**Parameters:** _none_  
**Returns:** _inferred_  

_No docstring._

---

← [Back to stdlib index](README.md)

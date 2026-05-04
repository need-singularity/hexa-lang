# `net/http_server`

_net/http_server_

**Source:** [`stdlib/net/http_server.hexa`](../../stdlib/net/http_server.hexa)  

## Overview

순수 HEXA 로 작성된 최소 HTTP/1.1 서버. self/serve/http_server.hexa 의
사용자 패키징 사본. 컴파일러 내부 파일은 수정하지 않는다.

  - Route 테이블(exact + prefix).
  - handler_name: string 으로 보관(async_runtime.scheduler_spawn_task 관례).
  - 실제 함수 호출은 server_serve/handle_conn 에 넘긴 dispatch_fn 이 담당.
    dispatch_fn(handler_name: string, request: map) -> response map.
  - Stage0 에 문자열로 함수를 찾아 호출하는 리플렉션 빌트인이 없기에,
    호출측에서 switch/if 로 handler_name 을 분기한다.

  server_serve 는 매 루프마다 file_exists(stop_flag_path) 를 확인해
  파일이 생기면 정상 종료한다. server_shutdown 이 해당 파일을 생성한다.

## Functions

| Function | Returns |
|---|---|
| [`route_new`](#fn-route-new) | `_inferred_` |
| [`server_new`](#fn-server-new) | `_inferred_` |
| [`server_route`](#fn-server-route) | `_inferred_` |
| [`server_mount`](#fn-server-mount) | `_inferred_` |
| [`server_listen`](#fn-server-listen) | `_inferred_` |
| [`server_accept_once`](#fn-server-accept-once) | `_inferred_` |
| [`server_handle_conn`](#fn-server-handle-conn) | `_inferred_` |
| [`server_serve`](#fn-server-serve) | `_inferred_` |
| [`server_shutdown`](#fn-server-shutdown) | `_inferred_` |

### <a id="fn-route-new"></a>`fn route_new`

```hexa
pub fn route_new(method, pattern, handler_name, prefix)
```

**Parameters:** `method, pattern, handler_name, prefix`  
**Returns:** _inferred_  

-- Route --

### <a id="fn-server-new"></a>`fn server_new`

```hexa
pub fn server_new(host, port)
```

**Parameters:** `host, port`  
**Returns:** _inferred_  

-- Server struct (as map for Stage0 portability) --

### <a id="fn-server-route"></a>`fn server_route`

```hexa
pub fn server_route(s, method, pattern, handler_name)
```

**Parameters:** `s, method, pattern, handler_name`  
**Returns:** _inferred_  

-- Route registration (exact match) --
반환: 갱신된 서버 맵. 기존 맵을 변이 후 그대로 돌려준다.

### <a id="fn-server-mount"></a>`fn server_mount`

```hexa
pub fn server_mount(s, method, prefix, handler_name)
```

**Parameters:** `s, method, prefix, handler_name`  
**Returns:** _inferred_  

-- Mount (prefix match) --

### <a id="fn-server-listen"></a>`fn server_listen`

```hexa
pub fn server_listen(s)
```

**Parameters:** `s`  
**Returns:** _inferred_  

-- Listener bind --

### <a id="fn-server-accept-once"></a>`fn server_accept_once`

```hexa
pub fn server_accept_once(s)
```

**Parameters:** `s`  
**Returns:** _inferred_  

-- Accept one connection (returns conn_fd; -1 on error) --

### <a id="fn-server-handle-conn"></a>`fn server_handle_conn`

```hexa
pub fn server_handle_conn(s, conn_fd, dispatch_fn)
```

**Parameters:** `s, conn_fd, dispatch_fn`  
**Returns:** _inferred_  

-- Handle one connection --
dispatch_fn(handler_name: string, request: map) -> response map.
반환값: 서버를 계속 돌릴지 여부. dispatch_fn 결과의
response["__stop__"] == true 이면 false 를 돌려준다.

### <a id="fn-server-serve"></a>`fn server_serve`

```hexa
pub fn server_serve(s, dispatch_fn)
```

**Parameters:** `s, dispatch_fn`  
**Returns:** _inferred_  

-- Accept loop with graceful shutdown sentinel --

### <a id="fn-server-shutdown"></a>`fn server_shutdown`

```hexa
pub fn server_shutdown(s)
```

**Parameters:** `s`  
**Returns:** _inferred_  

-- Shutdown: create sentinel file so server_serve exits on next iteration --

---

← [Back to stdlib index](README.md)

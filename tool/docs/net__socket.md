# `net/socket`

_net/socket_

**Source:** [`stdlib/net/socket.hexa`](../../stdlib/net/socket.hexa)  

## Overview

TCP 소켓 얇은 래퍼. 내부적으로 net_listen / net_accept / net_connect /
net_read / net_write / net_close 빌트인을 호출한다.

Stage0 env.hexa 등록 빌트인: net_listen, net_accept, net_connect,
                             net_read, net_write, net_close.

TODO roadmap 56 prereq A3: net_read_n builtin missing; Content-Length > 1 packet may truncate.
  native C (self/native/net.c) 에는 hexa_net_read_n / hexa_net_set_timeout 이
  이미 구현되어 있으나 self/env.hexa 빌트인 리스트에 등록되지 않아 .hexa
  소스에서 호출 불가. 등록되면 read_n / set_timeout 을 직접 래핑하도록
  전환하고, 아래 read_body_best_effort 의 루프 fallback 을 제거한다.

-- TCP primitives --

## Functions

| Function | Returns |
|---|---|
| [`socket_listen`](#fn-socket-listen) | `_inferred_` |
| [`socket_accept`](#fn-socket-accept) | `_inferred_` |
| [`socket_connect`](#fn-socket-connect) | `_inferred_` |
| [`socket_read`](#fn-socket-read) | `_inferred_` |
| [`socket_write`](#fn-socket-write) | `_inferred_` |
| [`socket_close`](#fn-socket-close) | `_inferred_` |
| [`read_body_best_effort`](#fn-read-body-best-effort) | `_inferred_` |

### <a id="fn-socket-listen"></a>`fn socket_listen`

```hexa
pub fn socket_listen(addr)
```

**Parameters:** `addr`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-socket-accept"></a>`fn socket_accept`

```hexa
pub fn socket_accept(listener)
```

**Parameters:** `listener`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-socket-connect"></a>`fn socket_connect`

```hexa
pub fn socket_connect(addr)
```

**Parameters:** `addr`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-socket-read"></a>`fn socket_read`

```hexa
pub fn socket_read(conn)
```

**Parameters:** `conn`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-socket-write"></a>`fn socket_write`

```hexa
pub fn socket_write(conn, data)
```

**Parameters:** `conn, data`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-socket-close"></a>`fn socket_close`

```hexa
pub fn socket_close(conn)
```

**Parameters:** `conn`  
**Returns:** _inferred_  

_No docstring._

### <a id="fn-read-body-best-effort"></a>`fn read_body_best_effort`

```hexa
pub fn read_body_best_effort(conn, already_have, expected, max_iters)
```

**Parameters:** `conn, already_have, expected, max_iters`  
**Returns:** _inferred_  

-- Body-framing fallback --
Content-Length > 현재 버퍼에 수신된 양보다 클 때 best-effort 로 추가 수신.
net_read_n / net_set_timeout 빌트인이 등록되면 이 함수를 단순 위임으로
교체한다.

already_have: 첫 net_read 결과에서 header 이후 남은 body 조각.
expected   : Content-Length 헤더 값.
max_iters  : 추가 net_read 최대 시도 횟수 (무한루프 방지).

---

← [Back to stdlib index](README.md)

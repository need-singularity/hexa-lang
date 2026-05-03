# `net/concurrent_serve`

_net/concurrent_serve_

**Source:** [`stdlib/net/concurrent_serve.hexa`](../../stdlib/net/concurrent_serve.hexa)  

## Overview

Work-stealing concurrent HTTP serve substrate for roadmap 63 / HXA-#10 / B12.

상위 모듈 stdlib/net/http_server.hexa (sequential accept-loop) 위에 올려
① 수락된 conn_fd 를 work-stealing deque 로 밀어 넣고
② 워커가 팝해서 파싱→라우팅→pre/post cert-trail→dispatch→metrics→write 를 수행.

Stage0 주의:
  - async_runtime.hexa 의 _global_scheduler 싱글톤을 쓰지 않는다(리스크 #4).
    ConcurrentServer 는 자신 전용 Scheduler 핸들(scheduler_id)을 등록한다.
  - 문자열 기반 handler dispatch: http_server.server_handle_conn 과 동일하게
    "handler_name" 을 사용자 dispatch_fn 에 넘긴다. Stage0 리플렉션 부재 대응.
  - Mutable struct fields: work_stealing.hexa 가 self.next_id = self.next_id + 1
    패턴을 검증했으므로 ConcurrentServer.metrics/endpoints 도 동일 방식으로 변이.
  - graceful shutdown: stop_flag_path 파일 생성 → accept 루프가 매 iter 체크.
    roadmap 62 (signal trap / in-flight agent) 와 통합은 landed 후 연결 예정.

상위 import — self/ 폴더 경로를 쓰면 stdlib 일관성이 깨지므로
stdlib 내부에서는 자신의 짐을 지닌 work-stealing 로직을 직접 구현한다.

## Functions

| Function | Returns |
|---|---|
| [`concurrent_server_new`](#fn-concurrent-server-new) | `ConcurrentServer` |
| [`set_metrics_hook`](#fn-set-metrics-hook) | `ConcurrentServer` |
| [`use_sharded_cert_trail`](#fn-use-sharded-cert-trail) | `ConcurrentServer` |
| [`dispatch_one`](#fn-dispatch-one) | `ConcurrentServer` |
| [`enqueue_request`](#fn-enqueue-request) | `ConcurrentServer` |
| [`drain_all`](#fn-drain-all) | `ConcurrentServer` |
| [`run`](#fn-run) | `int` |
| [`set_fairness`](#fn-set-fairness) | `ConcurrentServer` |
| [`shutdown`](#fn-shutdown) | `ConcurrentServer` |
| [`scheduler_wait_idle`](#fn-scheduler-wait-idle) | `ConcurrentServer` |
| [`get_metrics`](#fn-get-metrics) | `EndpointMetrics` |
| [`total_dispatched`](#fn-total-dispatched) | `int` |
| [`queue_depth`](#fn-queue-depth) | `int` |

### <a id="fn-concurrent-server-new"></a>`fn concurrent_server_new`

```hexa
pub fn concurrent_server_new(addr: string, port: int, cert_trail_path: string) -> ConcurrentServer
```

**Parameters:** `addr: string, port: int, cert_trail_path: string`  
**Returns:** `ConcurrentServer`  

── Public API ────────────────────────────────────────────────────────

### <a id="fn-set-metrics-hook"></a>`fn set_metrics_hook`

```hexa
pub fn set_metrics_hook(s: ConcurrentServer, hook_name: string) -> ConcurrentServer
```

**Parameters:** `s: ConcurrentServer, hook_name: string`  
**Returns:** `ConcurrentServer`  

_No docstring._

### <a id="fn-use-sharded-cert-trail"></a>`fn use_sharded_cert_trail`

```hexa
pub fn use_sharded_cert_trail(s: ConcurrentServer) -> ConcurrentServer
```

**Parameters:** `s: ConcurrentServer`  
**Returns:** `ConcurrentServer`  

_No docstring._

### <a id="fn-dispatch-one"></a>`fn dispatch_one`

```hexa
pub fn dispatch_one(s: ConcurrentServer, ticket: WorkTicket, dispatch_fn) -> ConcurrentServer
```

**Parameters:** `s: ConcurrentServer, ticket: WorkTicket, dispatch_fn`  
**Returns:** `ConcurrentServer`  

── Dispatch one ticket (pre-hook → handler → post-hook → metrics) ────
dispatch_fn(handler_name: string, payload: string) -> response map
Risk #3 mitigation: handler 호출은 recover 로 격리.

### <a id="fn-enqueue-request"></a>`fn enqueue_request`

```hexa
pub fn enqueue_request(s: ConcurrentServer, method: string, path: string, body: string) -> ConcurrentServer
```

**Parameters:** `s: ConcurrentServer, method: string, path: string, body: string`  
**Returns:** `ConcurrentServer`  

── Enqueue a synthetic request (test substrate) ─────────────────────
실 소켓 없이 파이프라인 테스트에 쓰인다. method/path 로 endpoint 조회 후
해당 ticket 을 pool 에 submit.

### <a id="fn-drain-all"></a>`fn drain_all`

```hexa
pub fn drain_all(s: ConcurrentServer, dispatch_fn) -> ConcurrentServer
```

**Parameters:** `s: ConcurrentServer, dispatch_fn`  
**Returns:** `ConcurrentServer`  

Drain all queued tickets through the dispatcher. 순수 파이프라인 실행.
preferred worker 는 rr_cursor 가 돌면서 고루 분산된다.

### <a id="fn-run"></a>`fn run`

```hexa
pub fn run(s: ConcurrentServer, workers: int) -> int
```

**Parameters:** `s: ConcurrentServer, workers: int`  
**Returns:** `int`  

── run() ─────────────────────────────────────────────────────────────
Blocks: accept 루프 + dispatch. Stage0 blocking net_accept 때문에 실제로는
단일 스레드 직렬 처리이지만 work-stealing deque 를 매개로 하여 logical
concurrency 형태를 유지한다. 멀티 OS 스레드는 roadmap 62 통합 후 활성화.

fairness="round_robin": enqueue 시점에 rr_cursor % workers 로 분산.
fairness="work_stealing"(default): least-loaded deque 로 push.

### <a id="fn-set-fairness"></a>`fn set_fairness`

```hexa
pub fn set_fairness(s: ConcurrentServer, mode: string) -> ConcurrentServer
```

**Parameters:** `s: ConcurrentServer, mode: string`  
**Returns:** `ConcurrentServer`  

Fairness 설정 accessor — run() 이 pool 을 재생성하기 전에 호출.

### <a id="fn-shutdown"></a>`fn shutdown`

```hexa
pub fn shutdown(s: ConcurrentServer) -> ConcurrentServer
```

**Parameters:** `s: ConcurrentServer`  
**Returns:** `ConcurrentServer`  

── shutdown() ────────────────────────────────────────────────────────
sentinel 생성. 동일 프로세스 내 accept-loop 는 다음 iter 에서 break.
scheduler_wait_idle: in_flight == 0 이 될 때까지 poll (테스트 대상).

### <a id="fn-scheduler-wait-idle"></a>`fn scheduler_wait_idle`

```hexa
pub fn scheduler_wait_idle(s: ConcurrentServer) -> ConcurrentServer
```

**Parameters:** `s: ConcurrentServer`  
**Returns:** `ConcurrentServer`  

_No docstring._

### <a id="fn-get-metrics"></a>`fn get_metrics`

```hexa
pub fn get_metrics(s: ConcurrentServer, path: string) -> EndpointMetrics
```

**Parameters:** `s: ConcurrentServer, path: string`  
**Returns:** `EndpointMetrics`  

── Introspection helpers (tests) ─────────────────────────────────────

### <a id="fn-total-dispatched"></a>`fn total_dispatched`

```hexa
pub fn total_dispatched(s: ConcurrentServer) -> int
```

**Parameters:** `s: ConcurrentServer`  
**Returns:** `int`  

_No docstring._

### <a id="fn-queue-depth"></a>`fn queue_depth`

```hexa
pub fn queue_depth(s: ConcurrentServer) -> int
```

**Parameters:** `s: ConcurrentServer`  
**Returns:** `int`  

_No docstring._

---

← [Back to stdlib index](README.md)

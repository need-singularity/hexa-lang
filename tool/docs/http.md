# `http`

_HTTP GET with custom headers (P1 — qmirror ANU x-api-key)_

**Source:** [`stdlib/http.hexa`](../../stdlib/http.hexa)  
**Module slug:** `stdlib-http`  
**Description:** curl-based HTTP GET with header map support  

**Usage:**

```hexa
import stdlib/http; let body = http_get_with_headers(url, #{"x-api-key": k}, 30)
```

## Overview

Source: anima/docs/hexa_lang_attr_review_for_qmirror_2026_05_03.md §5.4

PROBLEM (qmirror entropy.hexa ↔ ANU REST API)
  [`stdlib/net/http_client::http_get`](net__http_client.md#fn-http-get)(url, timeout) is body-only with no
  way to pass request headers. The ANU quantum-RNG endpoint requires
  an `x-api-key: <key>` header on every call. Today qmirror.entropy
  shells out raw `exec("curl -H 'x-api-key: " + key + "' ...")` — every
  caller re-derives the same shell-escape boilerplate, with one bug
  per call site (single-quote in the key value will break the line).

FIX (additive, no runtime.c change, parallel API to http_client)
  1. http_get_with_headers(url, headers, timeout_sec) -> string
       Body-only GET. Returns "" on non-200 / curl failure (mirrors
       [`http_client::http_get`](net__http_client.md#fn-http-get) behaviour for drop-in compatibility).
  2. http_get_with_headers_status(url, headers, timeout_sec) -> map
       {"status": int, "body": string, "ok": bool} — full status info,
       mirrors [`http_client::http_get_status`](net__http_client.md#fn-http-get-status).

Breaking risk: 0. New module, new symbols. stdlib/net/http_client
remains the canonical no-headers path; this module exists for the
header-required ANU / IBM / Mistral paths.

Caveats (raw#91):
  · curl is a hard dependency (same as stdlib/net/http_client). Not
    installed → empty body returned silently. Callers MUST treat ""
    as failure indistinguishable from a real empty body.
  · Header value containing a literal newline produces broken HTTP —
    this wrapper does NOT scan for it. ANU keys are alphanumeric;
    callers passing user-supplied header values must validate upstream.
  · Header KEYS are passed as-is to curl's `-H "k: v"` flag — case
    insensitive per RFC 7230. `dict_keys()` order is preserved.
  · binary body unsafe (NUL truncation in hexa string). Same caveat as
    stdlib/net/http_client; JSON/text response only.

raw#9 hexa-only-strict: pure hexa wrapper, exec("curl ...") shell-out
  identical to stdlib/net/http_client (no new C builtin needed).

_shell_escape — single-quote wrap with internal quote escape.
Identical pattern to [`stdlib/net/http_client::_shell_escape`](net__http_client.md#fn-shell-escape); duplicated
here so this module imports nothing from net/ (resolver simplicity).

## Functions

| Function | Returns |
|---|---|
| [`http_get_with_headers`](#fn-http-get-with-headers) | `string` |
| [`http_get_with_headers_status`](#fn-http-get-with-headers-status) | `_inferred_` |

### <a id="fn-http-get-with-headers"></a>`fn http_get_with_headers`

```hexa
pub fn http_get_with_headers(url: string, headers, timeout_sec: int) -> string
```

**Parameters:** `url: string, headers, timeout_sec: int`  
**Returns:** `string`  

http_get_with_headers — body-only GET with custom headers.

Returns the response body on 200-299, "" on any failure (non-2xx,
network error, curl missing). Matches [`stdlib/net/http_client::http_get`](net__http_client.md#fn-http-get)
failure-mode semantics.

curl options: same as [`http_client::http_get`](net__http_client.md#fn-http-get). `--fail` makes curl exit
non-zero on >=400, swallowing the body — caller gets "" and can either
retry or fall back to http_get_with_headers_status for diagnostics.

### <a id="fn-http-get-with-headers-status"></a>`fn http_get_with_headers_status`

```hexa
pub fn http_get_with_headers_status(url: string, headers, timeout_sec: int)
```

**Parameters:** `url: string, headers, timeout_sec: int`  
**Returns:** _inferred_  

http_get_with_headers_status — full {status, body, ok}.

Drops `--fail` so 4xx/5xx bodies are recoverable. Status code is
peeled from a `\n%{http_code}` sentinel appended via curl -w.
status==0 indicates curl itself failed (DNS, TLS, timeout).

---

← [Back to stdlib index](README.md)

#!/usr/bin/env python3
"""
token-forge compression proxy for Claude Code CLI.

Usage:
    ANTHROPIC_BASE_URL=http://localhost:8080/v1 claude

Intercepts /v1/messages, compresses old conversation turns (keeping the
last 3 verbatim), and forwards to the real Anthropic API.  Streams the
response back unchanged.

Zero external dependencies — stdlib only (Python 3.12+).
"""

from __future__ import annotations

import http.server
import json
import os
import ssl
import sys
import threading
import time
import urllib.request
from datetime import datetime
from typing import Any

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

LISTEN_HOST = "127.0.0.1"
LISTEN_PORT = int(os.environ.get("TF_PORT", "8080"))
UPSTREAM_URL = os.environ.get(
    "TF_UPSTREAM", "https://api.anthropic.com"
)
CHAR_THRESHOLD = int(os.environ.get("TF_THRESHOLD", "50000"))  # chars
KEEP_RECENT = int(os.environ.get("TF_KEEP_RECENT", "3"))       # turns
MAX_TOOL_RESULT_CHARS = int(os.environ.get("TF_MAX_TOOL_RESULT", "300"))
MAX_ASSISTANT_CHARS = int(os.environ.get("TF_MAX_ASSISTANT", "800"))
LOG_FILE = os.environ.get(
    "TF_LOG", os.path.expanduser("~/.token-forge/proxy.log")
)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def char_count(messages: list[dict]) -> int:
    """Rough char-count of the entire messages array."""
    return len(json.dumps(messages, ensure_ascii=False))


def summarise_text(text: str, limit: int) -> str:
    """Truncate with an ellipsis marker."""
    if len(text) <= limit:
        return text
    return text[:limit] + f"\n... [truncated {len(text) - limit} chars]"


def compress_content_block(block: dict) -> dict:
    """Compress a single content block inside a message."""
    btype = block.get("type", "")

    # tool_result — keep tool_use_id, strip bulky content
    if btype == "tool_result":
        content = block.get("content", "")
        if isinstance(content, list):
            # flatten text parts
            text = "\n".join(
                p.get("text", "") for p in content if isinstance(p, dict)
            )
        elif isinstance(content, str):
            text = content
        else:
            text = str(content)
        return {
            "type": "tool_result",
            "tool_use_id": block.get("tool_use_id", ""),
            "content": summarise_text(text, MAX_TOOL_RESULT_CHARS),
        }

    # text block
    if btype == "text":
        return {
            "type": "text",
            "text": summarise_text(block.get("text", ""), MAX_ASSISTANT_CHARS),
        }

    # tool_use — keep as-is (small)
    return block


def compress_message(msg: dict) -> dict:
    """Return a compressed copy of a single message."""
    role = msg.get("role", "")
    content = msg.get("content", "")

    # Simple string content (user text)
    if isinstance(content, str):
        if role == "assistant":
            return {**msg, "content": summarise_text(content, MAX_ASSISTANT_CHARS)}
        # user text — keep shorter than assistant but still trim
        return {**msg, "content": summarise_text(content, MAX_ASSISTANT_CHARS)}

    # List of content blocks
    if isinstance(content, list):
        compressed = [compress_content_block(b) for b in content]
        return {**msg, "content": compressed}

    return msg


# ---------------------------------------------------------------------------
# DESIGN NOTE (2026-04-10) — A_hybrid integration hook
# ---------------------------------------------------------------------------
# The bench/dfs_breakthrough.py experiment rebuilt from
# reports/breakthrough_1775473383.json confirms that the A_hybrid strategy
# ("last 3 turns verbatim + older turns LLM-compressed into a single prior
# block") is the strongest performer: avg similarity 70 %, PASS 3/5.
#
# Planned integration (DO NOT IMPLEMENT YET — PoC is the next step):
#
#   1. Gate on total char_count >= CHAR_THRESHOLD (already in place).
#   2. Split messages into `old = messages[:-KEEP_RECENT]` and `recent`.
#   3. Render `old` into "[USER]/[ASST] text" form, call tf.utils.llm_call
#      with the same compression prompt used in dfs_100pass_v2.compress_verified
#      (keywords-preserving, <=MAX_CTX chars).
#   4. Self-verify the compressed block via keyword coverage (>=0.5).
#      If verification fails, fall back to the current char-limit summariser
#      (compress_message) so we never regress below the current baseline.
#   5. Emit a single synthetic user message
#         {"role": "user", "content": "[PRIOR — compressed]\n" + compressed}
#      followed by the verbatim `recent` messages.
#   6. Log the A_hybrid verdict (sim/reduction) alongside original/compressed
#      char counts in log_entry for observability.
#
# Known open questions:
#   - llm_call backend choice inside a proxy (claude-cli would re-enter the
#     proxy → infinite recursion). Need a direct Anthropic API call using the
#     forwarded Authorization header, or an out-of-band summariser model.
#   - Latency budget: one extra round-trip per large request.
#   - Streaming: compression must happen before the upstream POST, so it is
#     synchronous — acceptable since compress_messages is already synchronous.
# ---------------------------------------------------------------------------


def compress_messages(messages: list[dict]) -> list[dict]:
    """
    Compress the message array:
      - Keep the last KEEP_RECENT messages verbatim.
      - Compress everything before that.
    """
    total = char_count(messages)
    if total < CHAR_THRESHOLD:
        return messages  # nothing to do

    if len(messages) <= KEEP_RECENT:
        return messages

    boundary = len(messages) - KEEP_RECENT
    old = messages[:boundary]
    recent = messages[boundary:]

    compressed_old = [compress_message(m) for m in old]
    return compressed_old + recent


def log_entry(original_chars: int, compressed_chars: int, model: str) -> None:
    """Append one line to the log file."""
    os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
    ratio = (
        f"{(1 - compressed_chars / original_chars) * 100:.1f}%"
        if original_chars > 0
        else "0%"
    )
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = (
        f"{ts}  model={model}  "
        f"original={original_chars}  compressed={compressed_chars}  "
        f"saved={ratio}\n"
    )
    try:
        with open(LOG_FILE, "a") as f:
            f.write(line)
    except OSError:
        pass
    # Also print to stderr for immediate feedback
    sys.stderr.write(f"[token-forge] {line}")


# ---------------------------------------------------------------------------
# HTTP handler
# ---------------------------------------------------------------------------

class ProxyHandler(http.server.BaseHTTPRequestHandler):
    """Handles POST /v1/messages — compresses and forwards."""

    # Silence per-request log lines from BaseHTTPRequestHandler
    def log_message(self, fmt: str, *args: Any) -> None:
        pass

    # ---------- plumbing ----------

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(length)

    def _forward_headers(self) -> dict[str, str]:
        """Build headers to send upstream (pass through auth, content-type, etc.)."""
        forward = {}
        for key in (
            "authorization",
            "anthropic-version",
            "anthropic-beta",
            "content-type",
            "x-api-key",
        ):
            val = self.headers.get(key)
            if val:
                forward[key] = val
        return forward

    def _proxy_streaming(self, upstream_resp) -> None:
        """Stream upstream response back to the client byte-for-byte."""
        self.send_response(upstream_resp.status)
        # Forward response headers
        for key, val in upstream_resp.getheaders():
            # hop-by-hop headers we must skip
            if key.lower() in ("transfer-encoding", "connection", "keep-alive"):
                continue
            self.send_header(key, val)
        # We will send chunked ourselves if upstream was chunked
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

        while True:
            chunk = upstream_resp.read(4096)
            if not chunk:
                break
            # Write chunk in HTTP chunked encoding
            self.wfile.write(f"{len(chunk):x}\r\n".encode())
            self.wfile.write(chunk)
            self.wfile.write(b"\r\n")
            self.wfile.flush()
        # Terminating chunk
        self.wfile.write(b"0\r\n\r\n")
        self.wfile.flush()

    # ---------- route ----------

    def do_POST(self) -> None:
        # Only handle /v1/messages (and strip leading /v1 that Claude Code
        # already appended because ANTHROPIC_BASE_URL includes /v1).
        path = self.path
        if not path.startswith("/v1/messages"):
            # Pass through other endpoints without modification
            self._passthrough(path)
            return

        raw_body = self._read_body()
        try:
            body = json.loads(raw_body)
        except json.JSONDecodeError:
            self.send_error(400, "Invalid JSON")
            return

        messages = body.get("messages", [])
        original_chars = char_count(messages)

        # Compress
        compressed = compress_messages(messages)
        body["messages"] = compressed
        compressed_chars = char_count(compressed)

        model = body.get("model", "unknown")
        log_entry(original_chars, compressed_chars, model)

        # Forward
        payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
        upstream = f"{UPSTREAM_URL}/v1/messages"
        headers = self._forward_headers()
        headers["content-length"] = str(len(payload))

        req = urllib.request.Request(upstream, data=payload, headers=headers, method="POST")

        try:
            # Create SSL context that uses system certs
            ctx = ssl.create_default_context()
            resp = urllib.request.urlopen(req, context=ctx, timeout=300)
            self._proxy_streaming(resp)
        except urllib.error.HTTPError as e:
            # Forward the error status and body
            self.send_response(e.code)
            for key, val in e.headers.items():
                if key.lower() not in ("transfer-encoding", "connection"):
                    self.send_header(key, val)
            self.end_headers()
            self.wfile.write(e.read())
            self.wfile.flush()
        except Exception as e:
            sys.stderr.write(f"[token-forge] upstream error: {e}\n")
            self.send_error(502, f"Upstream error: {e}")

    def do_GET(self) -> None:
        """Health check and stats."""
        if self.path in ("/health", "/v1/health"):
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok", "proxy": "token-forge"}).encode())
            return
        self.send_error(404)

    def _passthrough(self, path: str) -> None:
        """Forward non-messages endpoints unchanged."""
        raw_body = self._read_body()
        upstream = f"{UPSTREAM_URL}{path}"
        headers = self._forward_headers()
        if raw_body:
            headers["content-length"] = str(len(raw_body))
        req = urllib.request.Request(upstream, data=raw_body or None, headers=headers, method="POST")
        try:
            ctx = ssl.create_default_context()
            resp = urllib.request.urlopen(req, context=ctx, timeout=300)
            self._proxy_streaming(resp)
        except urllib.error.HTTPError as e:
            self.send_response(e.code)
            for key, val in e.headers.items():
                if key.lower() not in ("transfer-encoding", "connection"):
                    self.send_header(key, val)
            self.end_headers()
            self.wfile.write(e.read())
            self.wfile.flush()
        except Exception as e:
            self.send_error(502, f"Upstream error: {e}")


# ---------------------------------------------------------------------------
# Threaded server for concurrent requests
# ---------------------------------------------------------------------------

class ThreadedHTTPServer(http.server.HTTPServer):
    """Handle each request in a new thread."""
    daemon_threads = True

    def process_request(self, request, client_address):
        t = threading.Thread(target=self._handle, args=(request, client_address))
        t.daemon = True
        t.start()

    def _handle(self, request, client_address):
        try:
            self.finish_request(request, client_address)
        except Exception:
            self.handle_error(request, client_address)
        finally:
            self.shutdown_request(request)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    server = ThreadedHTTPServer((LISTEN_HOST, LISTEN_PORT), ProxyHandler)
    sys.stderr.write(
        f"[token-forge] proxy listening on {LISTEN_HOST}:{LISTEN_PORT}\n"
        f"[token-forge] upstream: {UPSTREAM_URL}\n"
        f"[token-forge] threshold: {CHAR_THRESHOLD} chars, keep last {KEEP_RECENT} turns\n"
        f"[token-forge] log: {LOG_FILE}\n"
        f"[token-forge] usage: ANTHROPIC_BASE_URL=http://localhost:{LISTEN_PORT}/v1 claude\n"
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        sys.stderr.write("\n[token-forge] shutting down\n")
        server.shutdown()


if __name__ == "__main__":
    main()

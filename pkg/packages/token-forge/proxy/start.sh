#!/usr/bin/env bash
# token-forge compression proxy — one-liner launcher
# Usage:  ./start.sh          (foreground)
#         ./start.sh &        (background)
#         ./start.sh --bg     (background, logs to ~/.token-forge/proxy.log)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON="${TF_PYTHON:-/opt/homebrew/bin/python3.12}"

if [[ "${1:-}" == "--bg" ]]; then
    nohup "$PYTHON" "$SCRIPT_DIR/server.py" >> ~/.token-forge/proxy.log 2>&1 &
    PID=$!
    echo "[token-forge] proxy started in background (PID $PID)"
    echo "[token-forge] ANTHROPIC_BASE_URL=http://localhost:${TF_PORT:-8080}/v1 claude"
    echo "$PID" > ~/.token-forge/proxy.pid
else
    exec "$PYTHON" "$SCRIPT_DIR/server.py"
fi

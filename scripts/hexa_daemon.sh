#!/bin/bash
# hexa-daemon — persistent worker daemon for hexa.real
#
# Installed at: /Users/ghost/.hx/bin/hexa-daemon (symlink to this file)
#
# Architecture:
#   socat listens on a Unix domain socket and forks per connection.
#   Each connection runs hexa-daemon-handler, which parses a JSON
#   request, executes hexa.real, and returns stdout/stderr/exit_code.
#
# This MVP saves the bash shim + a couple of fork layers. It does NOT
# yet skip module parsing — that requires hexa.real-side changes
# (deferred to upstream cycle, "option A" in the brainstorm).
#
# Reference: qmirror_speedup_brainstorm_2026_05_06.ai.md H.02
#            hexa_speedup_brainstorm_2026_05_06.ai.md

set -u

SOCK="${HEXA_DAEMON_SOCK:-/tmp/hexa-daemon.sock}"
PIDFILE="${HEXA_DAEMON_PIDFILE:-/tmp/hexa-daemon.pid}"
LOGFILE="${HEXA_DAEMON_LOG:-/tmp/hexa-daemon.log}"
HANDLER="${HEXA_DAEMON_HANDLER:-/Users/ghost/.hx/bin/hexa-daemon-handler}"
HEXA_REAL="${HEXA_REAL_BIN:-/Users/ghost/.hx/packages/hexa/hexa.real}"
SOCAT="$(command -v socat 2>/dev/null || echo /opt/homebrew/bin/socat)"

usage() {
    cat <<EOF
hexa-daemon — persistent worker daemon

Usage: hexa-daemon <start|stop|status|restart|exec>

Subcommands:
  start         start daemon (idempotent — no-op if running)
  stop          stop daemon, remove socket + pidfile
  status        print 'running' or 'stopped' (exit 0 / 1)
  restart       stop then start
  exec ARGS...  send a request to running daemon (stdout/stderr passthrough)

Env vars:
  HEXA_DAEMON_SOCK     socket path (default: /tmp/hexa-daemon.sock)
  HEXA_DAEMON_PIDFILE  pid file path (default: /tmp/hexa-daemon.pid)
  HEXA_DAEMON_LOG      log path (default: /tmp/hexa-daemon.log)
  HEXA_REAL_BIN        hexa.real binary path
EOF
}

is_running() {
    if [ ! -f "$PIDFILE" ]; then
        return 1
    fi
    local pid
    pid="$(cat "$PIDFILE" 2>/dev/null)"
    if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null; then
        return 1
    fi
    if [ ! -S "$SOCK" ]; then
        return 1
    fi
    return 0
}

cmd_start() {
    if is_running; then
        echo "hexa-daemon: already running (pid $(cat "$PIDFILE"))"
        return 0
    fi
    if [ ! -x "$SOCAT" ]; then
        echo "hexa-daemon: socat not found (try: brew install socat)" >&2
        return 2
    fi
    if [ ! -x "$HANDLER" ]; then
        echo "hexa-daemon: handler missing or not executable: $HANDLER" >&2
        return 2
    fi
    if [ ! -x "$HEXA_REAL" ]; then
        echo "hexa-daemon: hexa.real missing or not executable: $HEXA_REAL" >&2
        return 2
    fi
    rm -f "$SOCK"
    # nohup + disown so daemon survives shell exit.
    nohup "$SOCAT" \
        "UNIX-LISTEN:$SOCK,fork,reuseaddr,unlink-early" \
        "EXEC:$HANDLER,stderr" \
        >>"$LOGFILE" 2>&1 &
    local pid=$!
    disown "$pid" 2>/dev/null || true
    echo "$pid" >"$PIDFILE"
    # wait for socket
    local i=0
    while [ $i -lt 20 ]; do
        if [ -S "$SOCK" ] && kill -0 "$pid" 2>/dev/null; then
            echo "hexa-daemon: started (pid $pid, sock $SOCK)"
            return 0
        fi
        sleep 0.05
        i=$((i + 1))
    done
    echo "hexa-daemon: failed to start — see $LOGFILE" >&2
    rm -f "$PIDFILE" "$SOCK"
    return 1
}

cmd_stop() {
    if [ -f "$PIDFILE" ]; then
        local pid
        pid="$(cat "$PIDFILE" 2>/dev/null)"
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            local i=0
            while [ $i -lt 20 ] && kill -0 "$pid" 2>/dev/null; do
                sleep 0.05
                i=$((i + 1))
            done
            kill -9 "$pid" 2>/dev/null || true
        fi
    fi
    rm -f "$PIDFILE" "$SOCK"
    echo "hexa-daemon: stopped"
}

cmd_status() {
    if is_running; then
        echo "running (pid $(cat "$PIDFILE"), sock $SOCK)"
        return 0
    fi
    echo "stopped"
    return 1
}

cmd_exec() {
    if ! is_running; then
        echo "hexa-daemon: not running" >&2
        return 2
    fi
    # Build a minimal request line: tab-separated cwd + args (NUL-safe alternative
    # would need stricter tooling; tab is a pragmatic compromise for the MVP).
    local cwd
    cwd="$(pwd)"
    # Encode args by joining with US (\x1f) — disallowed in shell args normally.
    local req
    req="$cwd"
    local a
    for a in "$@"; do
        req="$req"$'\x1f'"$a"
    done
    # Send request, receive framed response.
    printf '%s\n' "$req" | "$SOCAT" - "UNIX-CONNECT:$SOCK"
}

case "${1:-status}" in
    start)   cmd_start ;;
    stop)    cmd_stop ;;
    status)  cmd_status ;;
    restart) cmd_stop; cmd_start ;;
    exec)    shift; cmd_exec "$@" ;;
    -h|--help|help) usage ;;
    *)       usage; exit 2 ;;
esac

#!/bin/bash
# hexa-cli — daemon-aware hexa wrapper.
#
# Tries to dispatch to the hexa-daemon if reachable; falls back to direct
# hexa.real invocation otherwise. Behaviour is identical to invoking
# /Users/ghost/.hx/bin/hexa for callers — same args, stdout, exit code.
#
# Disable the daemon path with HEXA_DAEMON=0.
#
# Reference: qmirror_speedup_brainstorm_2026_05_06.ai.md H.02
#
# Operator subcommands (A4/A5):
#   --daemon-stats [--json]   query daemon stats endpoint
#   --daemon-flush            flush daemon AST cache (SIGHUP)

set -u

SOCK="${HEXA_DAEMON_SOCK:-/tmp/hexa-daemon.sock}"
HEXA_REAL="${HEXA_REAL_BIN:-/Users/ghost/.hx/packages/hexa/hexa.real}"
SOCAT="$(command -v socat 2>/dev/null || echo /opt/homebrew/bin/socat)"
PIDFILE="${HEXA_DAEMON_PIDFILE:-/tmp/hexa-daemon.pid}"

run_direct() {
    export HEXA_SHIM_NO_DARWIN_LANDING=1
    exec "$HEXA_REAL" "$@"
}

# ── operator: --daemon-stats ────────────────────────────────────────────
# Friendly summary of daemon state. Exits 1 if daemon not running.
daemon_stats() {
    local want_json=0
    if [ "${1:-}" = "--json" ]; then want_json=1; fi

    if [ ! -S "$SOCK" ] || [ ! -x "$SOCAT" ]; then
        echo "daemon: not running (socket=$SOCK)" >&2
        return 1
    fi

    local tmp_stats
    tmp_stats="$(mktemp -t hexa-stats.XXXXXX)"
    trap 'rm -f "$tmp_stats"' RETURN
    if ! printf '__STATS__\n' | "$SOCAT" - "UNIX-CONNECT:$SOCK" >"$tmp_stats" 2>&1; then
        echo "daemon: not running (socket connect failed)" >&2
        return 1
    fi

    # First line is the JSON payload, second is __HEXA_DAEMON_RC__=0.
    local json
    json="$(head -n 1 "$tmp_stats")"
    case "$json" in
        \{*) ;;
        *)
            echo "daemon: malformed stats response" >&2
            cat "$tmp_stats" >&2
            return 1
            ;;
    esac

    if [ "$want_json" = "1" ]; then
        printf '%s\n' "$json"
        return 0
    fi

    # Crude JSON field extraction — avoids jq dependency.
    local cached cap hits misses requests pid uptime
    cached=$(  printf '%s' "$json" | sed -n 's/.*"cached":\([0-9]*\).*/\1/p')
    cap=$(     printf '%s' "$json" | sed -n 's/.*"cap":\([0-9]*\).*/\1/p')
    hits=$(    printf '%s' "$json" | sed -n 's/.*"hits":\([0-9]*\).*/\1/p')
    misses=$(  printf '%s' "$json" | sed -n 's/.*"misses":\([0-9]*\).*/\1/p')
    requests=$(printf '%s' "$json" | sed -n 's/.*"requests":\([0-9]*\).*/\1/p')
    pid=$(     printf '%s' "$json" | sed -n 's/.*"pid":\([0-9]*\).*/\1/p')
    uptime=$(  printf '%s' "$json" | sed -n 's/.*"uptime_sec":\([0-9]*\).*/\1/p')

    cached=${cached:-0}; cap=${cap:-0}; hits=${hits:-0}; misses=${misses:-0}
    requests=${requests:-0}; pid=${pid:-0}; uptime=${uptime:-0}

    # Hit ratio. Avoid divide-by-zero. Integer percent.
    local total ratio
    total=$(( hits + misses ))
    if [ "$total" -gt 0 ]; then
        ratio=$(( hits * 100 / total ))
    else
        ratio=0
    fi

    # Format uptime as Hh Mm Ss (or just seconds if < 60).
    local h m s up_str
    h=$(( uptime / 3600 ))
    m=$(( (uptime % 3600) / 60 ))
    s=$(( uptime % 60 ))
    if [ "$h" -gt 0 ]; then
        up_str="${h}h${m}m${s}s"
    elif [ "$m" -gt 0 ]; then
        up_str="${m}m${s}s"
    else
        up_str="${s}s"
    fi

    printf 'daemon:  running (pid=%s, uptime=%s)\n' "$pid" "$up_str"
    printf 'cache:   hits=%s misses=%s hit_ratio=%s%%\n' "$hits" "$misses" "$ratio"
    printf '         cached=%s/%s\n' "$cached" "$cap"
    printf 'request: total=%s\n' "$requests"
    return 0
}

# ── operator: --daemon-flush ───────────────────────────────────────────
# SIGHUP the daemon → cache flushes, stats counters preserved.
daemon_flush() {
    local pid=""
    if [ -f "$PIDFILE" ]; then
        pid="$(cat "$PIDFILE" 2>/dev/null || true)"
    fi
    if [ -z "$pid" ]; then
        # Fall back: pgrep by process name.
        pid="$(pgrep -f 'hexa-daemon-serve' | head -n 1 || true)"
    fi
    if [ -z "$pid" ]; then
        echo "daemon: not running (no pid)" >&2
        return 1
    fi
    if ! kill -HUP "$pid" 2>/dev/null; then
        echo "daemon: SIGHUP failed (pid=$pid stale?)" >&2
        return 1
    fi
    echo "daemon: SIGHUP sent (pid=$pid) — cache flush requested"
    return 0
}

# ── dispatch operator subcommands ──────────────────────────────────────
case "${1:-}" in
    --daemon-stats)
        shift
        daemon_stats "$@"
        exit $?
        ;;
    --daemon-flush)
        shift
        daemon_flush "$@"
        exit $?
        ;;
esac

# Daemon disabled or socket missing -> fall back.
if [ "${HEXA_DAEMON:-1}" = "0" ] || [ ! -S "$SOCK" ] || [ ! -x "$SOCAT" ]; then
    run_direct "$@"
fi

# Build the request: <cwd>\x1f<arg1>\x1f<arg2>...
cwd="$(pwd)"
req="$cwd"
for a in "$@"; do
    req="$req"$'\x1f'"$a"
done

# Pipe the request, capture the streamed response. Trailing
# __HEXA_DAEMON_RC__=N line carries the exit code.
tmp_out="$(mktemp -t hexa-cli.XXXXXX)"
trap 'rm -f "$tmp_out"' EXIT

if ! printf '%s\n' "$req" | "$SOCAT" - "UNIX-CONNECT:$SOCK" >"$tmp_out" 2>&1; then
    # socket connect failed mid-flight — fall back to direct path.
    run_direct "$@"
fi

# Extract exit code from the trailing sentinel line; print everything else.
rc_line="$(tail -n 1 "$tmp_out")"
case "$rc_line" in
    __HEXA_DAEMON_RC__=*)
        rc="${rc_line#__HEXA_DAEMON_RC__=}"
        # Print body without the sentinel.
        sed -e '$d' "$tmp_out"
        exit "$rc"
        ;;
    *)
        # Sentinel missing — daemon hiccup. Fall back to direct.
        cat "$tmp_out" >&2
        run_direct "$@"
        ;;
esac

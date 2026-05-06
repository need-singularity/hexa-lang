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

set -u

SOCK="${HEXA_DAEMON_SOCK:-/tmp/hexa-daemon.sock}"
HEXA_REAL="${HEXA_REAL_BIN:-/Users/ghost/.hx/packages/hexa/hexa.real}"
SOCAT="$(command -v socat 2>/dev/null || echo /opt/homebrew/bin/socat)"

run_direct() {
    export HEXA_SHIM_NO_DARWIN_LANDING=1
    exec "$HEXA_REAL" "$@"
}

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

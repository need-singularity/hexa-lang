#!/bin/bash
# hexa-daemon-handler — single-request handler invoked by socat per connection.
#
# Protocol (MVP):
#   Request (one line on stdin):
#     <cwd>\x1f<arg1>\x1f<arg2>\x1f...\n
#   Response (on stdout):
#     <hexa.real stdout/stderr interleaved as written>
#     trailing line: __HEXA_DAEMON_RC__=<exit_code>
#
# stderr from this handler propagates back through socat's stderr=stderr.
#
# Note: this handler still spawns hexa.real per request — the persistent
# benefit is socket-listener persistence, fewer wrapper shells, and a
# single attach point for future "warm parse cache" work (option A in
# the brainstorm).

set -u

HEXA_REAL="${HEXA_REAL_BIN:-/Users/ghost/.hx/packages/hexa/hexa.real}"

# Read one request line.
IFS= read -r req || exit 0

# Split on US (\x1f) into cwd + args.
# Use bash array splitting via IFS.
old_IFS="$IFS"
IFS=$'\x1f'
read -r -a parts <<<"$req"
IFS="$old_IFS"

cwd="${parts[0]:-}"
unset 'parts[0]'

if [ -n "$cwd" ] && [ -d "$cwd" ]; then
    cd "$cwd" || true
fi

# Pass HEXA_SHIM_NO_DARWIN_LANDING=1 to mirror the legacy shim behaviour.
export HEXA_SHIM_NO_DARWIN_LANDING=1

# Run hexa.real with the remaining args. We do not currently capture
# stdout/stderr separately — both flow back through the socket as the
# child's stdio. This keeps output ordering correct without buffering.
"$HEXA_REAL" "${parts[@]}"
rc=$?

printf '__HEXA_DAEMON_RC__=%d\n' "$rc"
exit 0

#!/bin/bash
# install_darwin_marker.sh — L1 lossless cycle (2026-04-25)
#
# Purpose: stamp the darwin-bypass eligibility marker at install time.
# Presence of $HEXA_DARWIN_MARKER (default ~/.hx/.darwin-bypass-eligible)
# tells the resolver shim that this Darwin host is pre-cleared for direct
# Mach-O exec on inherently-safe argv patterns (--version/--help/lsp/etc).
#
# Idempotent + safe to run on every install or reinstall. No-op on non-Darwin.
#
# Cycle JSON: state/design_strategy_trawl/2026-04-25_hexa_lang_lossless_improvements_omega_cycle.json (L1)
# Resolver shim self-heal counterpart: ~/.hx/bin/hexa lines 80-112

set -u

# Skip on non-Darwin systems entirely (no marker needed; resolver gates on uname -s)
[ "$(uname -s 2>/dev/null)" = "Darwin" ] || { echo "install_darwin_marker: skipping non-Darwin host"; exit 0; }

HX_HOME="${HX_HOME:-$HOME/.hx}"
DARWIN_MARKER="${HEXA_DARWIN_MARKER:-$HX_HOME/.darwin-bypass-eligible}"

mkdir -p "$(dirname "$DARWIN_MARKER")" 2>/dev/null || {
  echo "install_darwin_marker: failed to mkdir $(dirname "$DARWIN_MARKER") (read-only HOME?)" >&2
  exit 0  # silent — resolver self-heal will retry on first invocation
}
: > "$DARWIN_MARKER" 2>/dev/null || {
  echo "install_darwin_marker: failed to stamp $DARWIN_MARKER" >&2
  exit 0
}
echo "install_darwin_marker: stamped $DARWIN_MARKER (darwin-bypass eligible)"

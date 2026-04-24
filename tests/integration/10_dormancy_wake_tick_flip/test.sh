#!/usr/bin/env bash
# 10_dormancy_wake_tick_flip — runs tool/dormancy_wake_tick.hexa in an isolated
# NEXUS sandbox with a fixture watchlist where last_event_ts is in 2020 (→ elapsed >= 72h).
# Asserts: (a) tick emits dormancy_wake_fire, (b) writes growth_bus.jsonl,
# (c) row state flips to "fired_executed".
#
# dormancy_wake_tick.hexa is a nexus-owned tool — SKIP (rc=77) if not present.

set -u
NEXUS_ROOT_SRC="${NEXUS_ROOT:-/Users/ghost/core/nexus}"
SCRIPT="$NEXUS_ROOT_SRC/tool/dormancy_wake_tick.hexa"
if [ ! -f "$SCRIPT" ]; then
    echo "SKIP: $SCRIPT missing (nexus tool not available)"
    exit 77
fi

SANDBOX="$(mktemp -d -t hxa_dormancy_XXXXXX)"
trap 'rm -rf "$SANDBOX"; rm -f /tmp/nexus_dormancy_wake.pid' EXIT

mkdir -p "$SANDBOX/state"
cp "$FIXTURES/mini_trigger_watchlist.jsonl" "$SANDBOX/state/meta_trigger_watchlist.jsonl"
: > "$SANDBOX/growth_bus.jsonl"

# NEXUS env var is read by the script (line 13).
out="$(NEXUS="$SANDBOX" "$HEXA_BIN" "$SCRIPT" 2>&1)"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: tick rc=$rc"
    echo "$out"
    exit 1
fi

# (a) stdout contains dormancy_wake_fire with fired=1
fired_line=$(echo "$out" | grep -c 'dormancy_wake_fire' || true)
tick_line=$(echo "$out" | grep -c '"kind":"dormancy_wake_tick","fired":1' || true)
if [ "$fired_line" -lt 1 ] || [ "$tick_line" -lt 1 ]; then
    echo "FAIL: expected dormancy_wake_fire + tick(fired=1)"
    echo "---- output ----"
    echo "$out"
    exit 1
fi

# (b) growth_bus.jsonl has the fire event appended
if ! grep -q 'dormancy_wake_fire' "$SANDBOX/growth_bus.jsonl"; then
    echo "FAIL: growth_bus.jsonl missing dormancy_wake_fire"
    cat "$SANDBOX/growth_bus.jsonl"
    exit 1
fi

# (c) watchlist row state flipped to fired_executed
if ! grep -q '"state":"fired_executed"\|"state": "fired_executed"' "$SANDBOX/state/meta_trigger_watchlist.jsonl"; then
    echo "FAIL: watchlist row did not flip to fired_executed"
    cat "$SANDBOX/state/meta_trigger_watchlist.jsonl"
    exit 1
fi

echo "PASS: dormancy tick fired, bus appended, row flipped"
exit 0

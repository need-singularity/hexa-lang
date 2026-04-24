#!/usr/bin/env bash
# 08_roadmap_schema_adapter_anima_fixture — guards hxa-20260424-007
# (to-flat infinite hang on 261KB nexus.json v2 schema).
# We run to-flat on a tiny (<2KB) v2_tracks fixture under a strict 60s test
# timeout (run_all.sh). If convert_v2_to_flat hangs, timeout → FAIL.

set -u
SCRIPT="$HEXA_ROOT/tool/roadmap_schema_adapter.hexa"
FIXTURE="$FIXTURES/mini_roadmap_v2.json"
if [ ! -f "$SCRIPT" ]; then
    echo "FAIL: $SCRIPT missing"
    exit 1
fi
if [ ! -f "$FIXTURE" ]; then
    echo "FAIL: fixture $FIXTURE missing"
    exit 1
fi

OUT="$(mktemp -t hxa_v2_flat_XXXXXX).json"
trap 'rm -f "$OUT"' EXIT

# Internal wall-clock bound of 30s keeps us well under run_all's 60s guard.
log="$(timeout 30 "$HEXA_BIN" "$SCRIPT" to-flat "$FIXTURE" --out="$OUT" 2>&1)"
rc=$?
if [ $rc -eq 124 ]; then
    echo "FAIL: to-flat hung (>30s) on a <2KB fixture — hxa-20260424-007 regression"
    echo "$log"
    exit 1
fi
if [ $rc -ne 0 ]; then
    echo "FAIL: to-flat rc=$rc"
    echo "$log"
    exit 1
fi
if [ ! -s "$OUT" ]; then
    echo "FAIL: output file empty at $OUT"
    echo "$log"
    exit 1
fi

# Sanity — converted JSON should contain at least one node id or nodes_total.
if grep -q '"nodes"\|"id":"n1"\|nodes_total' "$OUT" 2>/dev/null \
   || echo "$log" | grep -q 'nodes_total='; then
    echo "PASS: to-flat finished under bound, output populated"
    exit 0
fi
echo "FAIL: output lacks expected node markers"
echo "---- log ----"
echo "$log"
echo "---- out ----"
head -20 "$OUT"
exit 1

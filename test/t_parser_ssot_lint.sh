#!/usr/bin/env bash
# test/t_parser_ssot_lint.sh — regression test for tool/parser_ssot_lint.sh.
#
# Asserts:
#   1. The lint runs successfully against the live SSOT triad and emits
#      its summary header (parser/full/cc paths printed, hexa-pair stats
#      printed, hexa_cc.c mirror-marker stats printed).
#   2. The G-AMBIG fix (commit 0c03b3b9) is correctly mirrored:
#      `p_lookahead_generic_args` body MUST hash-identical between
#      parser.hexa and hexa_full.hexa, and the `Mirrors p_lookahead_generic_args()
#      in self/parser.hexa` annotation in hexa_cc.c MUST resolve to a
#      static C definition. Both conditions are checked via JSON output.
#   3. Synthetic-drift detection: when we splice a fake fn-body change
#      into a temp copy of hexa_full.hexa, the lint MUST exit non-zero
#      and report the diverging fn name.
#
# This is the regression test referenced by the parser-SSOT-lint commit.
# CI gate (Path C, conservative): fail on real drift; informational on
# parser_only fns (which is normal during Stage-2 self-hosting roll-out).

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LINT="$REPO_ROOT/tool/parser_ssot_lint.sh"

if [ ! -x "$LINT" ]; then
    echo "[t_parser_ssot_lint] FAIL — lint script not executable: $LINT" >&2
    exit 1
fi

PARSER="$REPO_ROOT/self/parser.hexa"
FULL="$REPO_ROOT/self/hexa_full.hexa"
CC="$REPO_ROOT/self/native/hexa_cc.c"

# ── Test 1: lint runs and emits expected sections ────────────────────────
out="$("$LINT" 2>&1)"
rc=$?
# rc may be 0 (clean) or 1 (drift); both are valid here. rc=2 is fatal.
if [ "$rc" -eq 2 ]; then
    echo "[t_parser_ssot_lint] FAIL — lint returned 2 (file-not-found)"
    echo "$out"
    exit 1
fi

for needle in "[parser-ssot-lint] parser " \
              "[parser-ssot-lint] full " \
              "[parser-ssot-lint] cc " \
              "[parser-ssot-lint] hexa pair" \
              "[parser-ssot-lint] hexa_cc.c mirror markers"; do
    if ! printf '%s' "$out" | grep -qF "$needle"; then
        echo "[t_parser_ssot_lint] FAIL — missing expected output: $needle"
        echo "--- captured output ---"
        echo "$out"
        exit 1
    fi
done
echo "[t_parser_ssot_lint] PASS test 1: lint runs, emits expected sections"

# ── Test 2: G-AMBIG mirror is intact ─────────────────────────────────────
json="$("$LINT" --json 2>/dev/null | tail -n 1)"
# Extract numeric fields from the JSON via grep + cut. We avoid jq so the
# test runs on bare CI runners.
gv() { printf '%s' "$json" | sed -E "s/.*\"$1\":([0-9-]+).*/\1/" ; }
divergent="$(gv divergent)"
cc_resolved="$(gv cc_resolved)"
cc_broken="$(gv cc_broken)"
cc_unknown="$(gv cc_unknown_in_parser)"

# G-AMBIG specific: at least one mirror marker resolved (the
# p_lookahead_generic_args one), and zero broken markers.
if [ "${cc_resolved:-0}" -lt 1 ]; then
    echo "[t_parser_ssot_lint] FAIL — expected >=1 hexa_cc.c mirror marker resolved, got $cc_resolved"
    echo "--- json ---"
    echo "$json"
    exit 1
fi
if [ "${cc_broken:-0}" -ne 0 ]; then
    echo "[t_parser_ssot_lint] FAIL — expected 0 broken hexa_cc.c mirror markers, got $cc_broken"
    exit 1
fi
if [ "${cc_unknown:-0}" -ne 0 ]; then
    echo "[t_parser_ssot_lint] FAIL — expected 0 hexa_cc.c markers pointing at unknown parser.hexa fns, got $cc_unknown"
    exit 1
fi

# Specifically: p_lookahead_generic_args must NOT be in the divergent list.
# (If a future edit re-breaks the mirror, this test will catch it.)
if printf '%s' "$json" | grep -q '"p_lookahead_generic_args"'; then
    echo "[t_parser_ssot_lint] FAIL — p_lookahead_generic_args drifted between parser.hexa and hexa_full.hexa"
    echo "--- json ---"
    echo "$json"
    exit 1
fi
echo "[t_parser_ssot_lint] PASS test 2: G-AMBIG mirror (p_lookahead_generic_args) intact"

# ── Test 3: synthetic-drift detection ────────────────────────────────────
# Pick a small `fn p_at_end()` body (3 lines). Splice a comment marker into
# a temp copy of hexa_full.hexa to force a body-hash mismatch, then assert
# the lint flags exactly that fn.
TMPDIR_T="$(mktemp -d -t parser_ssot_lint_test.XXXXXX)"
trap 'rm -rf "$TMPDIR_T"' EXIT
TMP_FULL="$TMPDIR_T/hexa_full.hexa"
cp "$FULL" "$TMP_FULL"

# Insert a unique non-comment line inside p_at_end body. We add a no-op
# `let _injected_drift = 1` line right after the fn header — guaranteed to
# change body hash without breaking syntactic balance.
awk '
    BEGIN { injected = 0 }
    {
        print $0
        if (!injected && $0 ~ /^fn p_at_end\(\)/) {
            print "    let _injected_drift = 1"
            injected = 1
        }
    }
' "$FULL" > "$TMP_FULL"

# Verify injection happened (sanity).
if ! grep -q "_injected_drift" "$TMP_FULL"; then
    echo "[t_parser_ssot_lint] FAIL — synthetic drift injection failed (p_at_end fn not found in $FULL?)"
    exit 1
fi

drift_json="$("$LINT" --json "$PARSER" "$TMP_FULL" "$CC" 2>/dev/null | tail -n 1)"
drift_rc=$?
# Expect non-zero because synthetic drift was added on top of any existing.
"$LINT" "$PARSER" "$TMP_FULL" "$CC" >/dev/null 2>&1
drift_rc=$?
if [ "$drift_rc" -eq 0 ]; then
    echo "[t_parser_ssot_lint] FAIL — lint should have detected synthetic drift on p_at_end but exited 0"
    exit 1
fi
if ! printf '%s' "$drift_json" | grep -q '"p_at_end"'; then
    echo "[t_parser_ssot_lint] FAIL — synthetic drift not reported for p_at_end"
    echo "--- json ---"
    echo "$drift_json"
    exit 1
fi
echo "[t_parser_ssot_lint] PASS test 3: synthetic drift on p_at_end detected"

echo ""
echo "[t_parser_ssot_lint] ALL TESTS PASSED"
exit 0

#!/usr/bin/env bash
# tool/parser_ssot_lint.sh — drift detector for the parser SSOT triad.
#
# Background (FIX-1, commit 0c03b3b9): the G-AMBIG fix had to be hand-mirrored
# across THREE places because the same parser logic is duplicated:
#   1. self/parser.hexa            — Stage-2 self-hosted parser (canonical SSOT)
#   2. self/hexa_full.hexa         — interp build target (hand-edited monolith)
#   3. self/native/hexa_cc.c       — generated C transpiler (`hexa cc --regen`)
#
# tool/check_ssot_sync.hexa already covers (interpreter.hexa <-> hexa_full.hexa)
# but does NOT cover the parser-specific triad above. This script closes that
# gap with a pragmatic, build-free, dependency-free lint:
#
#   * For every `fn p_<NAME>(` defined in self/parser.hexa we extract the
#     normalised body (trim + strip blank/`//`-comment lines + brace-balance)
#     and compute a stable hash.
#   * We do the same for self/hexa_full.hexa and report any name common to
#     BOTH whose body hashes diverge — that is structural drift in the
#     hexa pair.
#   * For self/native/hexa_cc.c we cannot hash a body byte-identically (it's
#     C, not hexa) so we instead require every `Mirrors p_<NAME>() in
#     self/parser.hexa` annotation in hexa_cc.c to resolve to a `static <ret>
#     p_<NAME>(_c)?(...)` definition within ~50 lines after the marker. We
#     also flag mirror markers that name a fn that doesn't exist in
#     parser.hexa (stale annotation).
#
# Why a shell script (Path C, the conservative ship):
#   * Runs in CI without needing build/hexa_interp.real to be already built.
#   * No dependency on the running interp/hexa_v2 toolchain (chicken-and-egg
#     when the SSOT itself is broken — the lint must still execute).
#   * Plain awk + sha256sum, available on every macOS/Linux runner.
#   * Path C is documented in PLAN: "lint-only solution, ship the conservative
#     baseline first."  Auto-generation (Path A) and module-import (Path B)
#     remain future work — see footer.
#
# Exit codes:
#   0 — no drift; all common p_*() bodies match across parser.hexa &
#       hexa_full.hexa, all p_* mirror markers in hexa_cc.c resolve.
#   1 — at least one drift detected (body hash mismatch OR broken mirror
#       marker OR mirror marker without matching parser.hexa definition).
#   2 — file not found / argv error.
#
# Usage:
#   tool/parser_ssot_lint.sh                                # default paths
#   tool/parser_ssot_lint.sh --json                         # machine-readable
#   tool/parser_ssot_lint.sh /path/to/parser.hexa /path/to/hexa_full.hexa /path/to/hexa_cc.c

set -uo pipefail

# ── arg parse ────────────────────────────────────────────────────────────
JSON=0
PARSER=""
FULL=""
CC=""
for a in "$@"; do
    case "$a" in
        --json) JSON=1 ;;
        --help|-h)
            sed -n '2,40p' "$0"
            exit 0
            ;;
        *)
            if [ -z "$PARSER" ]; then PARSER="$a"
            elif [ -z "$FULL" ]; then FULL="$a"
            elif [ -z "$CC" ]; then CC="$a"
            fi
            ;;
    esac
done

# Default paths — relative to repo root (script lives in tool/).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
: "${PARSER:=$REPO_ROOT/self/parser.hexa}"
: "${FULL:=$REPO_ROOT/self/hexa_full.hexa}"
: "${CC:=$REPO_ROOT/self/native/hexa_cc.c}"

for f in "$PARSER" "$FULL" "$CC"; do
    if [ ! -f "$f" ]; then
        echo "[parser-ssot-lint] FATAL not found: $f" >&2
        exit 2
    fi
done

# Pick a portable hash. macOS ships shasum; Linux ships sha256sum.
if command -v sha256sum >/dev/null 2>&1; then
    HASH() { sha256sum | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
    HASH() { shasum -a 256 | awk '{print $1}'; }
else
    echo "[parser-ssot-lint] FATAL: no sha256sum or shasum available" >&2
    exit 2
fi

# ── extract_fn_body <file> <fn_name> ─────────────────────────────────────
# Emits the brace-balanced body lines (header line included) of `fn NAME(`
# from <file>, NORMALISED: trim leading/trailing whitespace, drop blank
# lines, drop pure `//`-comment lines. Stable across cosmetic edits.
extract_fn_body() {
    local file="$1"
    local name="$2"
    awk -v target="$name" '
    BEGIN { depth = 0; opened = 0; capturing = 0 }
    {
        if (!capturing) {
            line = $0
            t = line
            sub(/^[ \t]+/, "", t)
            if (substr(t, 1, 3) == "fn ") {
                rest = substr(t, 4)
                lp = index(rest, "(")
                if (lp > 0) {
                    nm = substr(rest, 1, lp - 1)
                    gsub(/[ \t]+/, "", nm)
                    if (nm == target) {
                        capturing = 1
                        depth = 0
                        opened = 0
                    }
                }
            }
        }
        if (capturing) {
            line = $0
            o = gsub(/\{/, "{", line)
            c = gsub(/\}/, "}", line)
            depth = depth + o - c
            if (o > 0) opened = 1

            t = $0
            sub(/^[ \t]+/, "", t)
            sub(/[ \t]+$/, "", t)
            if (t != "" && substr(t, 1, 2) != "//") {
                print t
            }

            if (opened && depth <= 0) { exit 0 }
        }
    }
    ' "$file"
}

hash_fn_body() {
    local body
    body="$(extract_fn_body "$1" "$2")"
    if [ -z "$body" ]; then
        echo ""
    else
        printf '%s\n' "$body" | HASH
    fi
}

# ── enumerate `^fn p_NAME(` from parser.hexa ─────────────────────────────
# Restrict to `p_*` names — those are the parser-internal helpers that share
# semantics across the triad. Other fn names (lexer, eval, etc.) legitimately
# differ between parser.hexa and hexa_full.hexa.
parser_fns="$(awk '
    /^fn p_/ {
        line = $0
        sub(/^fn /, "", line)
        lp = index(line, "(")
        if (lp > 0) {
            nm = substr(line, 1, lp - 1)
            gsub(/[ \t]+/, "", nm)
            print nm
        }
    }
' "$PARSER")"

# ── compare bodies parser.hexa <-> hexa_full.hexa ────────────────────────
total=0
common=0
identical=0
divergent=0
parser_only=0
divergent_list=""
parser_only_list=""

while IFS= read -r fn; do
    [ -z "$fn" ] && continue
    total=$((total + 1))
    h_p="$(hash_fn_body "$PARSER" "$fn")"
    h_f="$(hash_fn_body "$FULL" "$fn")"
    if [ -z "$h_f" ]; then
        parser_only=$((parser_only + 1))
        parser_only_list="$parser_only_list $fn"
    else
        common=$((common + 1))
        if [ "$h_p" = "$h_f" ]; then
            identical=$((identical + 1))
        else
            divergent=$((divergent + 1))
            divergent_list="$divergent_list $fn"
        fi
    fi
done <<< "$parser_fns"

# ── verify hexa_cc.c mirror markers ──────────────────────────────────────
cc_markers="$(awk '
    /Mirrors p_[A-Za-z0-9_]+\(\) in self\/parser\.hexa/ {
        s = $0
        i = index(s, "Mirrors p_")
        if (i > 0) {
            tail = substr(s, i + 8)
            j = index(tail, "(")
            if (j > 0) {
                nm = substr(tail, 1, j - 1)
                gsub(/[ \t]+/, "", nm)
                printf("%d\t%s\n", NR, nm)
            }
        }
    }
    /Mirror of self\/parser\.hexa::p_[A-Za-z0-9_]+/ {
        s = $0
        i = index(s, "self/parser.hexa::p_")
        if (i > 0) {
            tail = substr(s, i + 17)
            sub(/^:/, "", tail)
            n = length(tail)
            buf = ""
            for (k = 1; k <= n; k++) {
                ch = substr(tail, k, 1)
                if (ch ~ /[A-Za-z0-9_]/) buf = buf ch
                else break
            }
            if (length(buf) > 0) printf("%d\t%s\n", NR, buf)
        }
    }
' "$CC")"

cc_marker_count=0
cc_marker_resolved=0
cc_marker_broken=0
cc_marker_unknown_in_parser=0
cc_broken_list=""
cc_unknown_list=""

while IFS=$'\t' read -r line nm; do
    [ -z "$nm" ] && continue
    cc_marker_count=$((cc_marker_count + 1))

    if ! grep -qE "^fn $nm\(" "$PARSER"; then
        cc_marker_unknown_in_parser=$((cc_marker_unknown_in_parser + 1))
        cc_unknown_list="$cc_unknown_list $nm@cc:$line"
        continue
    fi

    end_line=$((line + 50))
    found="$(awk -v s="$line" -v e="$end_line" -v nm="$nm" '
        NR >= s && NR <= e {
            # static <decls...> p_NAME(   or   p_NAME_c(
            if ($0 ~ ("static[ \\t].*[ \\t*]"nm"(_c)?[ \\t]*\\(")) { print NR; exit 0 }
        }
    ' "$CC")"

    if [ -z "$found" ]; then
        cc_marker_broken=$((cc_marker_broken + 1))
        cc_broken_list="$cc_broken_list $nm@cc:$line"
    else
        cc_marker_resolved=$((cc_marker_resolved + 1))
    fi
done <<< "$cc_markers"

# ── report ───────────────────────────────────────────────────────────────
fail=0
if [ "$divergent" -gt 0 ]; then fail=1; fi
if [ "$cc_marker_broken" -gt 0 ]; then fail=1; fi
if [ "$cc_marker_unknown_in_parser" -gt 0 ]; then fail=1; fi

if [ "$JSON" = "1" ]; then
    printf '{'
    printf '"parser":"%s",' "$PARSER"
    printf '"full":"%s",' "$FULL"
    printf '"cc":"%s",' "$CC"
    printf '"parser_p_fns":%d,' "$total"
    printf '"common":%d,' "$common"
    printf '"identical":%d,' "$identical"
    printf '"divergent":%d,' "$divergent"
    printf '"parser_only":%d,' "$parser_only"
    printf '"cc_markers":%d,' "$cc_marker_count"
    printf '"cc_resolved":%d,' "$cc_marker_resolved"
    printf '"cc_broken":%d,' "$cc_marker_broken"
    printf '"cc_unknown_in_parser":%d,' "$cc_marker_unknown_in_parser"
    printf '"divergent_fns":['
    first=1
    for nm in $divergent_list; do
        [ "$first" = "1" ] || printf ','
        printf '"%s"' "$nm"
        first=0
    done
    printf '],'
    printf '"fail":%d' "$fail"
    printf '}\n'
else
    echo "[parser-ssot-lint] parser  = $PARSER"
    echo "[parser-ssot-lint] full    = $FULL"
    echo "[parser-ssot-lint] cc      = $CC"
    echo ""
    echo "[parser-ssot-lint] hexa pair (parser.hexa <-> hexa_full.hexa)"
    echo "  parser p_*() fns = $total"
    echo "  common           = $common"
    echo "  identical        = $identical"
    echo "  divergent        = $divergent"
    echo "  parser_only      = $parser_only"
    if [ "$divergent" -gt 0 ]; then
        echo "  divergent fns:"
        for nm in $divergent_list; do echo "    - $nm"; done
    fi
    if [ "$parser_only" -gt 0 ]; then
        echo "  parser_only fns (informational; not yet ported to hexa_full.hexa):"
        for nm in $parser_only_list; do echo "    - $nm"; done
    fi
    echo ""
    echo "[parser-ssot-lint] hexa_cc.c mirror markers"
    echo "  total markers          = $cc_marker_count"
    echo "  resolved to definition = $cc_marker_resolved"
    echo "  broken (no def near)   = $cc_marker_broken"
    echo "  unknown in parser.hexa = $cc_marker_unknown_in_parser"
    if [ "$cc_marker_broken" -gt 0 ]; then
        echo "  broken markers:"
        for ent in $cc_broken_list; do echo "    - $ent"; done
    fi
    if [ "$cc_marker_unknown_in_parser" -gt 0 ]; then
        echo "  unknown markers (mirror points at fn that doesn't exist in parser.hexa):"
        for ent in $cc_unknown_list; do echo "    - $ent"; done
    fi
    echo ""
    if [ "$fail" = "0" ]; then
        echo "[parser-ssot-lint] OK — no parser-SSOT drift detected."
    else
        echo "[parser-ssot-lint] FAIL — parser-SSOT drift detected. Reconcile via tool/ssot_mirror.hexa or hand-edit."
        if [ "$divergent" -gt 0 ]; then
            echo "[parser-ssot-lint] Suggested fix-up command:"
            echo "  hexa run tool/ssot_mirror.hexa $FULL $PARSER$divergent_list"
        fi
    fi
fi

# Future work (Path A / B):
#   * Path A: emit hexa_full.hexa::p_*() bodies and hexa_cc.c::p_*_c()
#     bodies from self/parser.hexa via a code-generator; eliminate the
#     hand-mirroring entirely. Requires hexa_cc front-end emit hooks for
#     parser fn extraction and a C-targeted lowering that matches the
#     hand-written C semantics. Out of scope for this lint.
#   * Path B: refactor hexa_full.hexa into multiple modules so it can
#     `use "parser"` directly from self/parser.hexa. Blocked on
#     flatten_imports + hexa_v2 import semantics being stable (the
#     comments in self/hexa_full.hexa note hexa_v2 silent-drops `use`).

exit "$fail"

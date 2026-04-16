#!/usr/bin/env bash
# hexa-diag.sh — Convert `hexa parse|run` plain-text errors into LSP JSON diagnostics.
#
# WHY
#   hexa's compiler prints errors as "Parse error at L:C: message" plain text and
#   exits 0 even on failure. LSP clients, editor gutters, and CI reporters want a
#   structured format. This wrapper provides that bridge WITHOUT touching
#   self/runtime.c, stage0, or self/ .hexa sources (HX7/HX8 safe).
#
# USAGE
#   scripts/hexa-diag.sh <file.hexa>                  # parse mode (default)
#   scripts/hexa-diag.sh --mode=run <file.hexa>       # runtime diagnostics
#   scripts/hexa-diag.sh --format=pretty <file.hexa>  # human-readable table
#   scripts/hexa-diag.sh --format=json <file.hexa>    # LSP-compatible JSON (default)
#
# EXIT CODES
#   0 — no diagnostics
#   1 — one or more diagnostics emitted
#   2 — tool invocation error (missing file, bad flag)
#
# OUTPUT (JSON, one object with `diagnostics` array — matches LSP
# textDocument/publishDiagnostics params shape, minus `uri`):
#   {
#     "uri":   "file:///abs/path.hexa",
#     "diagnostics": [
#       {
#         "range": { "start": {"line": L, "character": C},
#                    "end":   {"line": L, "character": C+1} },
#         "severity": 1,          // 1=Error 2=Warning 3=Info 4=Hint
#         "source":   "hexa",
#         "code":     "parse",
#         "message":  "unexpected token Newline"
#       }
#     ]
#   }
#
# INTEGRATION HINTS
#   VS Code — wrap this from a Task or LSP middleware that calls
#             publishDiagnostics using the JSON.
#   Neovim  — feed into vim.diagnostic.set via a coc/ale/lsp adapter.
#   CI      — `scripts/hexa-diag.sh file.hexa | jq '.diagnostics | length'`.

set -euo pipefail

HEXA_BIN="${HEXA_BIN:-$(dirname "$0")/../hexa}"
mode="parse"
format="json"
file=""

for arg in "$@"; do
    case "$arg" in
        --mode=*)    mode="${arg#--mode=}" ;;
        --format=*)  format="${arg#--format=}" ;;
        --help|-h)
            sed -n '1,40p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        -*)
            echo "unknown flag: $arg" >&2
            exit 2
            ;;
        *)
            file="$arg"
            ;;
    esac
done

if [ -z "$file" ] || [ ! -f "$file" ]; then
    echo "hexa-diag: missing or nonexistent file" >&2
    exit 2
fi

abs_file="$(cd "$(dirname "$file")" && pwd)/$(basename "$file")"
raw="$("$HEXA_BIN" "$mode" "$file" 2>&1 || true)"

# Collapse multi-line errors (e.g. "Parse error at 2:13: unexpected token Newline ('\n')"
# where the shell sees a literal LF mid-message). Any line NOT starting with a
# known prefix is folded into the previous line with a space separator.
raw="$(printf '%s\n' "$raw" | awk '
    BEGIN { buf = "" }
    /^Parse error at / || /^OK:/ || /^Error/ || /^Warning/ {
        if (buf != "") print buf
        buf = $0
        next
    }
    {
        # continuation line — fold into buffer, replacing the literal newline
        # with a visible \n marker for JSON clarity.
        if (buf == "") { buf = $0; next }
        buf = buf "\\n" $0
    }
    END { if (buf != "") print buf }
')"

# Parse "Parse error at L:C: message" lines. LSP uses 0-based line/char; hexa
# reports 1-based so subtract 1. We keep a 1-char-wide range as a sensible
# default; downstream tools can expand it.
# Portable across BSD/macOS awk (no gawk-specific 3-arg match).
diagnostics_json="$(
    printf '%s\n' "$raw" | awk '
        BEGIN { first = 1; printf "[" }
        /^Parse error at [0-9]+:[0-9]+:/ {
            # Strip the "Parse error at " prefix, then split "L:C: message"
            s = $0
            sub(/^Parse error at /, "", s)
            # Find first colon (line)
            p1 = index(s, ":")
            if (p1 == 0) next
            line = substr(s, 1, p1 - 1) + 0 - 1
            rest = substr(s, p1 + 1)
            # Find next colon (column)
            p2 = index(rest, ":")
            if (p2 == 0) next
            col = substr(rest, 1, p2 - 1) + 0 - 1
            msg = substr(rest, p2 + 2)   # skip ": " (colon + space)
            # JSON-escape the message
            gsub(/\\/, "\\\\", msg); gsub(/"/, "\\\"", msg)
            gsub(/\t/, "\\t", msg); gsub(/\r/, "\\r", msg); gsub(/\n/, "\\n", msg)
            if (!first) printf ","
            printf "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},\"end\":{\"line\":%d,\"character\":%d}},\"severity\":1,\"source\":\"hexa\",\"code\":\"parse\",\"message\":\"%s\"}", line, col, line, col+1, msg
            first = 0
        }
        END { printf "]" }
    '
)"

# Count actual diagnostic objects (top-level "range" keys). Each diagnostic has
# exactly one top-level "range":, so this equals diagnostic count.
count=$(printf '%s' "$diagnostics_json" | awk -v RS='"range"' 'END{print NR-1}')

if [ "$format" = "pretty" ]; then
    if [ "$count" = "0" ]; then
        echo "OK: $file (0 diagnostics)"
    else
        echo "FILE: $file"
        echo "$raw" | grep -E '^Parse error at' || true
        echo "---"
        echo "total: $count diagnostic(s)"
    fi
else
    # JSON mode (LSP publishDiagnostics shape, minus the transport envelope)
    printf '{"uri":"file://%s","diagnostics":%s}\n' "$abs_file" "$diagnostics_json"
fi

if [ "$count" = "0" ]; then exit 0; else exit 1; fi

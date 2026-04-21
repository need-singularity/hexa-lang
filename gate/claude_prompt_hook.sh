#!/usr/bin/env bash
# UserPromptSubmit hook entry — Claude Code sends JSON on stdin with `.prompt`
# and `.transcript_path`. Extract the prompt, enrich with recent user-turn
# context if too short for drill auto-dispatch (commands.json D2: ≥30 chars),
# then forward to gate/entry.hexa. Stdout becomes the hook's additionalContext
# injected into Claude's context for the turn.
# SSOT: airgenome/rules/airgenome.json#AG10.bootstrap_hook

set -uo pipefail

# ai-native error emitter — 3 stderr lines, exit 0 always (best-effort hook).
# Usage: ai_err CODE "what happened" "fix action" "ssot path or ref"
ai_err() {
    local code="$1" what="$2" fix="$3" ssot="$4"
    printf '[claude_prompt_hook/%s] %s\n' "$code" "$what" >&2
    printf '  fix: %s\n' "$fix" >&2
    printf '  ssot: %s\n' "$ssot" >&2
}

# Per-invocation stderr log path — concurrent hooks do not stomp each other.
ERR_LOG="/tmp/claude_prompt_hook.err.$$"

# $WS is the single workspace-root SSOT (matches .workspace file layout).
# No probing, no $HOME/core fallback — if WS is unset we fail loud so the
# caller (Claude Code, airgenome-init, shell) fixes their environment.
if [ -z "${WS:-}" ]; then
    ai_err "E_WS_UNSET" \
        "\$WS not set — cannot locate hexa-lang tree" \
        "export WS=<workspace-root> (e.g. \$HOME/core) in ~/.claude/settings.json env block or shell rc" \
        "airgenome/rules/airgenome.json#AG10 (WS convention)"
    exit 0
fi

HEXA="$WS/hexa-lang/hexa"
ENTRY="$WS/hexa-lang/gate/entry.hexa"

if [ ! -x "$HEXA" ]; then
    ai_err "E_HEXA_MISSING" \
        "hexa binary not found at \$WS/hexa-lang/hexa → $HEXA" \
        "build hexa (make -C \$WS/hexa-lang) or correct \$WS" \
        "$HEXA"
    exit 0
fi

if [ ! -f "$ENTRY" ]; then
    ai_err "E_ENTRY_MISSING" \
        "gate/entry.hexa not found at \$WS/hexa-lang/gate/entry.hexa → $ENTRY" \
        "restore gate/entry.hexa under \$WS or correct \$WS" \
        "$ENTRY"
    exit 0
fi

if ! command -v jq >/dev/null 2>&1; then
    ai_err "E_JQ_MISSING" \
        "jq not found on PATH — cannot parse stdin JSON from Claude Code" \
        "brew install jq (or apt-get install jq) and retry" \
        "PATH / package manager"
    exit 0
fi

input=$(cat)
prompt=$(printf '%s' "$input" | jq -r '.prompt // empty' 2>/dev/null)

# Empty prompt → silent no-op.
[ -z "$prompt" ] && exit 0

# Seed enrichment — if the prompt is shorter than 30 chars (commands.json D2)
# pull the last 3 user turns from the transcript and append them so short
# imperatives like "drill!!" get a meaningful seed without relying on
# Claude's probabilistic context inference.
if [ "${#prompt}" -lt 30 ]; then
    transcript=$(printf '%s' "$input" | jq -r '.transcript_path // empty' 2>/dev/null)
    if [ -n "$transcript" ]; then
        if [ ! -f "$transcript" ]; then
            ai_err "E_TRANSCRIPT_READ" \
                "transcript_path set but file unreadable: $transcript" \
                "ignore if ephemeral; otherwise verify Claude Code transcript dir permissions" \
                ".transcript_path from hook stdin JSON"
        else
            # Each JSONL line is {type:"user", message:{content:"…"}} or
            # {type:"user", message:{content:[{type:"text", text:"…"}, …]}}.
            # Skip /slash-command wrappers (<command-message>…) and keep the last 3.
            context=$(tail -40 "$transcript" 2>/dev/null | jq -rs '
                [ .[] | select(.type == "user") | .message.content |
                  if type == "string" then .
                  elif type == "array" then (.[] | select(.type == "text") | .text)
                  else empty end
                ] | map(select(startswith("<command-") | not)) | .[-3:] | .[]
            ' 2>/dev/null | tr '\n' ' ' | tr -s ' ')
            if [ -n "$context" ]; then
                prompt="$prompt · ctx: $context"
            fi
        fi
    fi
fi

# Dispatch; failures shouldn't block Claude's turn.
if ! "$HEXA" run "$ENTRY" prompt "$prompt" 2>"$ERR_LOG"; then
    ai_err "E_DISPATCH_FAIL" \
        "entry.hexa dispatch returned non-zero — stderr captured" \
        "inspect $ERR_LOG then re-run: $HEXA run $ENTRY prompt '<prompt>'" \
        "$ENTRY"
    exit 0
fi

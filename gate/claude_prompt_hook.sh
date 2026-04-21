#!/usr/bin/env bash
# UserPromptSubmit hook entry — Claude Code sends JSON on stdin with `.prompt`
# and `.transcript_path`. Extract the prompt, enrich with recent user-turn
# context if too short for drill auto-dispatch (commands.json D2: ≥30 chars),
# then forward to gate/entry.hexa. Stdout becomes the hook's additionalContext
# injected into Claude's context for the turn.
# SSOT: airgenome/rules/airgenome.json#AG10.bootstrap_hook

set -uo pipefail

HEXA="/Users/ghost/core/hexa-lang/hexa"
ENTRY="/Users/ghost/core/hexa-lang/gate/entry.hexa"

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
    if [ -n "$transcript" ] && [ -f "$transcript" ]; then
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

# Dispatch; failures shouldn't block Claude's turn.
if ! "$HEXA" run "$ENTRY" prompt "$prompt" 2>/tmp/claude_prompt_hook.err; then
    printf '[gate] entry.hexa dispatch failed (stderr → /tmp/claude_prompt_hook.err)\n' >&2
    exit 0
fi

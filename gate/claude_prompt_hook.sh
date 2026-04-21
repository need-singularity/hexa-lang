#!/usr/bin/env bash
# UserPromptSubmit hook entry — Claude Code sends JSON on stdin with `.prompt`.
# Extract the prompt text and dispatch to gate/entry.hexa; stdout becomes the
# hook's additionalContext injected into Claude's context for the turn.
# SSOT: airgenome/rules/airgenome.json#AG10.bootstrap_hook

set -uo pipefail

HEXA="/Users/ghost/Dev/hexa-lang/hexa"
ENTRY="/Users/ghost/Dev/hexa-lang/gate/entry.hexa"

# Read full stdin, extract .prompt with jq.
input=$(cat)
prompt=$(printf '%s' "$input" | jq -r '.prompt // empty' 2>/dev/null)

# Empty prompt → silent success (no-op hook).
if [ -z "$prompt" ]; then
    exit 0
fi

# Dispatch. Any failure in hexa shouldn't block the user's turn — emit a
# short diagnostic on stderr and exit 0 so Claude still processes the prompt.
if ! "$HEXA" run "$ENTRY" prompt "$prompt" 2>/tmp/claude_prompt_hook.err; then
    printf '[gate] entry.hexa dispatch failed (stderr → /tmp/claude_prompt_hook.err)\n' >&2
    exit 0
fi

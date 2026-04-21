#!/bin/bash
# minimal stage0 shim — forwards to the hexa interpreter.
# Replaces the removed nexus/harness entry (nexus shared decommission).
exec "$HOME/.hx/bin/hexa" "$@"

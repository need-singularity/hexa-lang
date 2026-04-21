#!/usr/bin/env bash
# Spawn `~/.hx/bin/nexus drill --seed <seed>` in the background.
# Prints the PID on stdout. stderr of drill redirected to log file.
# Invoked by gate/prompt_scan.hexa on [CMD] drill match.
set -u
seed="${1:?seed required}"
log="${2:-/tmp/nexus_drill_active.log}"
nohup ~/.hx/bin/nexus drill --seed "$seed" > "$log" 2>&1 < /dev/null &
disown
echo "$!"

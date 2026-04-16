#!/usr/bin/env bash
# scripts/run_tests.sh — Isolated hexa test runner
#
# macOS-native process isolation (Docker 불필요):
#   ulimit -t  CPU초 하드캡 (커널 SIGKILL — hang 불가)
#   ulimit -v  RAM 캡     (malloc fail → OOM thrash 차단)
#   timeout    벽시계 SIGKILL 안전망
#   renice     최저 우선순위 (시스템 부하 차단)
#
# Usage:
#   bash scripts/run_tests.sh                    # all tests/t*.hexa
#   bash scripts/run_tests.sh tests/t34*.hexa    # specific test(s)
#   ARENA=1 bash scripts/run_tests.sh            # override HEXA_VAL_ARENA
#   CPU_SEC=60 WALL_SEC=60 bash scripts/run_tests.sh  # relax limits
#
# Test file directives (parsed from first 20 lines):
#   // ENV: KEY=VALUE          — set env var for this test
#   // SKIP: reason            — skip this test with message
#   // EXPECTED: text          — stdout must contain this string
#   // Requires stage0 rebuild — auto-skip (not yet baked into hexa_v2)
set -euo pipefail
cd "$(dirname "$0")/.."

# ── Isolation defaults ──
: "${CPU_SEC:=30}"
: "${RAM_MB:=2048}"
: "${WALL_SEC:=30}"
: "${KILL_GRACE:=5}"
: "${ARENA:=0}"

RAM_KB=$(( RAM_MB * 1024 ))

# ── Color ──
if [ -t 1 ]; then
    RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[0;33m'
    BLD='\033[1m'; RST='\033[0m'
else
    RED=''; GRN=''; YEL=''; BLD=''; RST=''
fi

# ── Parse directives from test header ──
parse_directive() {
    local f="$1" tag="$2"
    head -20 "$f" | grep -E "^// *${tag}: " | sed "s|^// *${tag}: *||" || true
}

# ── Run one test with isolation ──
run_one() {
    local f="$1"
    local name
    name=$(basename "$f" .hexa)

    # Check SKIP directive
    local skip_reason
    skip_reason=$(parse_directive "$f" "SKIP")
    if [ -n "$skip_reason" ]; then
        printf "${YEL}SKIP${RST}  %-30s %s\n" "$name" "$skip_reason"
        return 2
    fi

    # Check stage0 rebuild dependency
    if head -10 "$f" | grep -qi 'requires stage0 rebuild'; then
        printf "${YEL}SKIP${RST}  %-30s needs stage0 rebuild\n" "$name"
        return 2
    fi

    # Parse ENV directives
    local env_lines
    env_lines=$(parse_directive "$f" "ENV")

    # Parse EXPECTED directive
    local expected
    expected=$(parse_directive "$f" "EXPECTED")

    # Run in isolated subshell
    local exit_code=0
    local output
    output=$(
        (
            ulimit -t "$CPU_SEC" 2>/dev/null || true
            ulimit -v "$RAM_KB" 2>/dev/null || true
            renice +19 $$ >/dev/null 2>&1 || true

            # Apply per-test ENV
            while IFS= read -r line; do
                [ -z "$line" ] && continue
                export "$line"
            done <<< "$env_lines"

            # Defaults
            export HEXA_VAL_ARENA="${HEXA_VAL_ARENA:-$ARENA}"
            export DYLD_LIBRARY_PATH="${DYLD_LIBRARY_PATH:-$PWD/self/native/build}"

            timeout --kill-after="$KILL_GRACE" "$WALL_SEC" ./hexa run "$f" 2>&1
        )
    ) || exit_code=$?

    # EXPECTED check (if directive present, stdout must contain it)
    if [ $exit_code -eq 0 ] && [ -n "$expected" ]; then
        if ! echo "$output" | grep -qF "$expected"; then
            exit_code=99
        fi
    fi

    # Classify
    case $exit_code in
        0)
            printf "${GRN}PASS${RST}  %-30s\n" "$name"
            ;;
        124)
            printf "${RED}TIME${RST}  %-30s wall>${WALL_SEC}s\n" "$name"
            ;;
        137|152)
            printf "${RED}KILL${RST}  %-30s SIGKILL (CPU/RAM limit)\n" "$name"
            ;;
        99)
            printf "${RED}FAIL${RST}  %-30s expected: %s\n" "$name" "$expected"
            ;;
        *)
            printf "${RED}FAIL${RST}  %-30s exit=%d\n" "$name" "$exit_code"
            ;;
    esac

    # Show tail on failure
    if [ $exit_code -ne 0 ] && [ -n "$output" ]; then
        echo "$output" | tail -5 | sed 's/^/       /'
    fi

    return $exit_code
}

# ── Main ──
files=("$@")
if [ ${#files[@]} -eq 0 ]; then
    files=(tests/t*.hexa)
fi

total=${#files[@]}
pass=0; fail=0; skip=0
failed_names=()

printf "${BLD}═══ hexa test runner (%d tests) ═══${RST}\n" "$total"
printf "    CPU≤%ds  RAM≤%dMB  wall≤%ds  nice=+19  arena=%s\n\n" \
    "$CPU_SEC" "$RAM_MB" "$WALL_SEC" "$ARENA"

for f in "${files[@]}"; do
    rc=0
    run_one "$f" || rc=$?
    case $rc in
        0) pass=$((pass + 1)) ;;
        2) skip=$((skip + 1)) ;;
        *) fail=$((fail + 1)); failed_names+=("$(basename "$f")") ;;
    esac
done

echo ""
printf "${BLD}═══ %d pass  %d fail  %d skip  (%d total) ═══${RST}\n" \
    "$pass" "$fail" "$skip" "$total"

if [ ${#failed_names[@]} -gt 0 ]; then
    printf "${RED}FAILED:${RST} %s\n" "${failed_names[*]}"
fi

[ $fail -eq 0 ]

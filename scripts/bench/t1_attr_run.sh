#!/bin/bash
# ═══════════════════════════════════════════════════════════════
#  scripts/bench/t1_attr_run.sh — AI-native attr 가속 비교 벤치
#
#  1. htz 원격에서 hexa self/ml/t1_bench_attr.hexa 실행
#  2. stdout 의 '{' 로 시작하는 모든 JSON 라인 캡처 (모드당 1줄)
#  3. 각 라인에 ts(로컬), host="htz" 주입 후
#     nexus/shared/bench/zeta_compare.jsonl 에 append
#
#  사용: bash scripts/bench/t1_attr_run.sh
# ═══════════════════════════════════════════════════════════════
set -e
set -o pipefail

REMOTE_HOST="hetzner"
REMOTE_HEXA="\$HOME/Dev/hexa-lang/target/release/hexa"
REMOTE_BENCH="\$HOME/Dev/hexa-lang/self/ml/t1_bench_attr.hexa"
OUT_JSONL="/Users/ghost/Dev/nexus/shared/bench/zeta_compare.jsonl"
HOST_TAG="htz"

echo "[t1_attr_run] launching attr-compare bench on ${REMOTE_HOST}…" >&2

if ! RAW=$(ssh "${REMOTE_HOST}" "${REMOTE_HEXA} ${REMOTE_BENCH}" 2>&1); then
    echo "[t1_attr_run] ERROR: SSH or remote execution failed" >&2
    echo "----- remote output -----" >&2
    echo "${RAW}" >&2
    exit 2
fi

# '{' 로 시작하는 모든 라인 수집
LINES=$(echo "${RAW}" | grep '^{' || true)

if [ -z "${LINES}" ]; then
    echo "[t1_attr_run] ERROR: no JSON line found in remote output" >&2
    echo "----- remote output -----" >&2
    echo "${RAW}" >&2
    exit 3
fi

mkdir -p "$(dirname "${OUT_JSONL}")"

NOW_TS=$(date +%s)
COUNT=0
while IFS= read -r LINE; do
    [ -z "${LINE}" ] && continue
    # remove trailing '}' and re-append with injected host/ts
    STRIPPED="${LINE%\}}"
    ENRICHED="${STRIPPED},\"host\":\"${HOST_TAG}\",\"local_ts\":${NOW_TS}}"
    echo "${ENRICHED}"
    echo "${ENRICHED}" >> "${OUT_JSONL}"
    COUNT=$((COUNT + 1))
done <<< "${LINES}"

echo "[t1_attr_run] appended ${COUNT} lines to ${OUT_JSONL}" >&2

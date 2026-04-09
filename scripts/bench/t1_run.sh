#!/bin/bash
# ═══════════════════════════════════════════════════════════════
#  scripts/bench/t1_run.sh — T1 baseline 실측 (htz 원격 실행)
#
#  1. hetzner 원격에서 hexa self/ml/t1_bench.hexa 실행
#  2. stdout 마지막 '{' 시작 JSON 1줄 캡처
#  3. shared/bench/zeta_compare.jsonl 에 append
#
#  사용: bash scripts/bench/t1_run.sh
# ═══════════════════════════════════════════════════════════════
set -e
set -o pipefail

REMOTE_HOST="hetzner"
REMOTE_HEXA="\$HOME/Dev/hexa-lang/target/release/hexa"
REMOTE_BENCH="\$HOME/Dev/hexa-lang/self/ml/t1_bench.hexa"
OUT_JSONL="/Users/ghost/Dev/nexus/shared/bench/zeta_compare.jsonl"

echo "[t1_run] launching T1 baseline on ${REMOTE_HOST}…" >&2

# SSH 실행 + stdout 수집 (에러 시 즉시 종료)
if ! RAW=$(ssh "${REMOTE_HOST}" "${REMOTE_HEXA} ${REMOTE_BENCH}" 2>&1); then
    echo "[t1_run] ERROR: SSH or remote execution failed" >&2
    echo "----- remote output -----" >&2
    echo "${RAW}" >&2
    exit 2
fi

# JSON 한 줄 추출 (마지막 '{' 로 시작하는 줄)
RESULT=$(echo "${RAW}" | grep '^{' | tail -1 || true)

if [ -z "${RESULT}" ]; then
    echo "[t1_run] ERROR: no JSON line found in remote output" >&2
    echo "----- remote output -----" >&2
    echo "${RAW}" >&2
    exit 3
fi

# append 대상 디렉토리 보장
mkdir -p "$(dirname "${OUT_JSONL}")"

echo "${RESULT}"
echo "${RESULT}" >> "${OUT_JSONL}"

echo "[t1_run] appended to ${OUT_JSONL}" >&2

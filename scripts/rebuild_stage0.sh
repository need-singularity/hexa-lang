#!/usr/bin/env bash
# rebuild_stage0.sh — safe wrapper around build_stage0.sh
#
# WHY: 이번 세션(2026-04-15)에서 stage0 재빌드가 5~10초마다 반복되어
#   flatten_imports → hexa_v2 → clang → cc1 → linker 6-8 child × 누적
#   → macOS xpcproxy 폭주 → system load 24→112. 근본 원인:
#   (1) 동시 claude 세션 다중 재빌드
#   (2) 입력 변경 없이도 매번 풀 파이프라인 실행
#   (3) 재빌드마다 smoke test 또 스폰
#
# 이 wrapper 가 해결:
#   - mkdir lock (atomic, macOS-safe) → 동시 재빌드 차단
#   - mtime guard → self/*.hexa 최신이 output 보다 오래되면 skip
#   - NO_SMOKE 기본 on → smoke 스폰 제거 (호출측이 필요하면 본인 테스트 1회)
#   - 실패 시 lock 자동 해제
#
# 사용:
#   bash scripts/rebuild_stage0.sh               # mtime-guarded
#   FORCE=1 bash scripts/rebuild_stage0.sh       # 강제 rebuild
#   WITH_SMOKE=1 bash scripts/rebuild_stage0.sh  # smoke test 포함
#
# 반환 코드:
#   0 — rebuilt or already up-to-date
#   1 — build failed
#   2 — another session holds the lock (caller should wait or abort)

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HEXA_DIR/build/hexa_stage0"
LOCK_DIR="/tmp/hexa_rebuild_stage0.lock"
LOCK_TTL_SEC=300   # 5min: stale 판단 임계치

# ── 1. Lock 획득 (atomic mkdir) ─────────────────────────────
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    HOLDER_PID=""
    HOLDER_TIME=""
    if [ -f "$LOCK_DIR/pid" ]; then
        HOLDER_PID=$(cat "$LOCK_DIR/pid" 2>/dev/null || echo "?")
    fi
    if [ -d "$LOCK_DIR" ]; then
        # macOS stat vs GNU stat 호환
        HOLDER_TIME=$(stat -f %m "$LOCK_DIR" 2>/dev/null || stat -c %Y "$LOCK_DIR" 2>/dev/null || echo 0)
    fi
    NOW=$(date +%s)
    AGE=$(( NOW - ${HOLDER_TIME:-$NOW} ))

    if [ -n "$HOLDER_PID" ] && kill -0 "$HOLDER_PID" 2>/dev/null && [ "$AGE" -lt "$LOCK_TTL_SEC" ]; then
        echo "[rebuild_stage0] another session is building (pid=$HOLDER_PID, age=${AGE}s) — abort" >&2
        exit 2
    fi

    # stale: holder 죽음 OR TTL 초과 → 회수
    echo "[rebuild_stage0] stale lock (pid=$HOLDER_PID, age=${AGE}s) — reclaiming" >&2
    rm -rf "$LOCK_DIR"
    mkdir "$LOCK_DIR"
fi
echo $$ > "$LOCK_DIR/pid"
trap 'rm -rf "$LOCK_DIR"' EXIT INT TERM

# ── 2. mtime guard ─────────────────────────────────────────
if [ -z "$FORCE" ] && [ -x "$OUT" ]; then
    NEED=0
    for f in "$HEXA_DIR/self"/*.hexa "$HEXA_DIR/self/runtime.c" "$HEXA_DIR/self/native/hexa_v2"; do
        [ -e "$f" ] || continue
        if [ "$f" -nt "$OUT" ]; then
            NEED=1
            echo "[rebuild_stage0] trigger: $f newer than stage0" >&2
            break
        fi
    done
    if [ "$NEED" = "0" ]; then
        echo "[rebuild_stage0] up-to-date, skip"
        exit 0
    fi
fi

# ── 3. 빌드 위임 (smoke 기본 off) ──────────────────────────
if [ -z "$WITH_SMOKE" ]; then
    export NO_SMOKE=1
fi

echo "[rebuild_stage0] invoking build_stage0.sh (pid=$$, lock=$LOCK_DIR)"
bash "$HEXA_DIR/scripts/build_stage0.sh"

echo "[rebuild_stage0] OK"

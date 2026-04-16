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
#   - content-hash guard → sha256(hexa_full.hexa + runtime.c) vs stored hash
#   - NO_SMOKE 기본 on → smoke 스폰 제거 (호출측이 필요하면 본인 테스트 1회)
#   - 실패 시 lock 자동 해제
#
# 사용:
#   bash scripts/rebuild_stage0.sh               # content-hash guarded
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
cleanup() { rm -rf "$LOCK_DIR"; [ -n "${BACKUP:-}" ] && rm -f "$BACKUP" || :; }
trap cleanup EXIT INT TERM

# ── 2. content-hash guard (ROI-9: replaces mtime guard) ────
HASH_FILE="$HEXA_DIR/build/.rebuild_hash"

compute_source_hash() {
    # Hash the two primary inputs; sort for determinism
    cat "$HEXA_DIR/self/hexa_full.hexa" "$HEXA_DIR/self/runtime.c" 2>/dev/null \
        | shasum -a 256 | cut -d' ' -f1
}

if [ -z "$FORCE" ] && [ -x "$OUT" ] && [ -f "$HASH_FILE" ]; then
    CURRENT_HASH=$(compute_source_hash)
    STORED_HASH=$(cat "$HASH_FILE" 2>/dev/null || echo "")
    if [ "$CURRENT_HASH" = "$STORED_HASH" ]; then
        echo "[rebuild_stage0] content-hash unchanged ($CURRENT_HASH), skip"
        exit 0
    fi
    echo "[rebuild_stage0] content-hash changed, rebuilding" >&2
fi

# ── 3. 빌드 위임 (smoke 기본 off) ──────────────────────────
if [ -z "$WITH_SMOKE" ]; then
    export NO_SMOKE=1
fi

# ── 3a. pre-build backup ──────────────────────────────────
# 깨진 바이너리로 덮어쓰기 방지: 빌드 전에 working 바이너리 백업
BACKUP=""
MIN_SIZE="${HEXA_STAGE0_MIN_SIZE:-1000000}"  # 1MB — working binary는 ~3.9MB
if [ -x "$OUT" ]; then
    OLD_SIZE=$(wc -c < "$OUT" | tr -d ' ')
    if [ "$OLD_SIZE" -ge "$MIN_SIZE" ]; then
        BACKUP="/tmp/hexa_stage0_backup_$$"
        cp -f "$OUT" "$BACKUP"
        echo "[rebuild_stage0] backup: $BACKUP (${OLD_SIZE} bytes)"
    fi
fi

echo "[rebuild_stage0] invoking build_stage0.sh (pid=$$, lock=$LOCK_DIR)"
if ! bash "$HEXA_DIR/scripts/build_stage0.sh"; then
    echo "[rebuild_stage0] build FAILED" >&2
    if [ -n "$BACKUP" ] && [ -f "$BACKUP" ]; then
        cp -f "$BACKUP" "$OUT"
        echo "[rebuild_stage0] rolled back to backup" >&2
    fi
    exit 1
fi

# ── 3b. size guard — 빌드 성공해도 결과물이 비정상 소형이면 rollback
if [ -x "$OUT" ]; then
    NEW_SIZE=$(wc -c < "$OUT" | tr -d ' ')
    if [ "$NEW_SIZE" -lt "$MIN_SIZE" ]; then
        echo "[rebuild_stage0] ERROR: output too small (${NEW_SIZE} < ${MIN_SIZE} bytes) — broken transpile?" >&2
        if [ -n "$BACKUP" ] && [ -f "$BACKUP" ]; then
            cp -f "$BACKUP" "$OUT"
            if [ "$(uname)" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
                codesign --force --sign - "$OUT" 2>/dev/null || true
            fi
            echo "[rebuild_stage0] rolled back to backup ($(wc -c < "$OUT" | tr -d ' ') bytes)" >&2
        fi
        rm -f "$BACKUP"
        exit 1
    fi
fi
rm -f "$BACKUP"

# ── 4. store content hash after successful build ──────────
mkdir -p "$(dirname "$HASH_FILE")"
compute_source_hash > "$HASH_FILE"
echo "[rebuild_stage0] hash stored → $(cat "$HASH_FILE")"

echo "[rebuild_stage0] OK"

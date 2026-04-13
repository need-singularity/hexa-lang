#!/usr/bin/env bash
# build_stage0.sh — stage0 인터프리터(`build/hexa_stage0`) 재빌드 스크립트
#
# 목적: SSOT self/hexa_full.hexa 를 hexa_v2 로 트랜스파일하고 clang 으로
#       컴파일하여 build/hexa_stage0 생성. `./hexa run` 이 위임하는 stage0.
#
# 사용:
#   bash scripts/build_stage0.sh         # 전체 파이프라인 (transpile+compile)
#   CLANG=gcc bash scripts/build_stage0.sh
#   SKIP_TRANSPILE=1 bash scripts/build_stage0.sh   # 기존 .c 재사용
#
# OS 분기 (build_stage1.sh 와 동일 패턴):
#   Linux  — glibc POSIX/GNU 확장 노출 필요: -D_GNU_SOURCE -std=gnu11
#            수학/dl 심볼: -lm -ldl
#   macOS  — libSystem 자동 처리. codesign ad-hoc 서명 (AppleSystemPolicy).
#
# @pipeline (self/hexa_full.hexa 헤더와 동일):
#   1. hexa_v2  self/hexa_full.hexa  /tmp/hexa_full_regen.c
#   2. clang    $CFLAGS  /tmp/hexa_full_regen.c  -o build/hexa_stage0  $LDFLAGS
#   3. codesign --force --sign -  build/hexa_stage0   (Darwin only)
#   4. verify: echo 'println("ok")' > /tmp/_hx.hexa && build/hexa_stage0 /tmp/_hx.hexa

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_HEXA="$HEXA_DIR/self/hexa_full.hexa"
SRC_C="${HEXA_STAGE0_C:-/tmp/hexa_full_regen.c}"
OUT="$HEXA_DIR/build/hexa_stage0"
HEXA_V2="$HEXA_DIR/self/native/hexa_v2"
CLANG="${CLANG:-clang}"

if [ ! -f "$SRC_HEXA" ]; then
    echo "error: SSOT missing: $SRC_HEXA" >&2
    exit 1
fi

if [ ! -x "$HEXA_V2" ]; then
    echo "error: hexa_v2 missing or not executable: $HEXA_V2" >&2
    echo "       Linux 부트스트랩은 먼저 hexa_v2 빌드 필요:" >&2
    echo "       clang -O2 -D_GNU_SOURCE -std=gnu11 -I . -I self self/native/hexa_cc.c -o $HEXA_V2 -lm" >&2
    exit 1
fi

# 1) Transpile
if [ -z "$SKIP_TRANSPILE" ]; then
    echo "[build_stage0] transpile: $HEXA_V2 $SRC_HEXA $SRC_C"
    "$HEXA_V2" "$SRC_HEXA" "$SRC_C"
fi

if [ ! -f "$SRC_C" ]; then
    echo "error: transpiled C missing: $SRC_C" >&2
    exit 1
fi

# 2) Compile
UNAME="$(uname 2>/dev/null | tr -d ' \n\t')"
case "$UNAME" in
    Linux)
        CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -Wno-trigraphs -I $HEXA_DIR/self"
        LDFLAGS="-lm -ldl"
        ;;
    Darwin|*)
        CFLAGS="-O2 -std=c11 -Wno-trigraphs -I $HEXA_DIR/self"
        LDFLAGS=""
        ;;
esac

mkdir -p "$HEXA_DIR/build"
echo "[build_stage0] uname=$UNAME"
echo "[build_stage0] $CLANG $CFLAGS $SRC_C -o $OUT $LDFLAGS"

# shellcheck disable=SC2086
"$CLANG" $CFLAGS "$SRC_C" -o "$OUT" $LDFLAGS

# 3) Darwin codesign (AppleSystemPolicy SIGKILL 방지)
if [ "$UNAME" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - "$OUT" 2>/dev/null || true
fi

# 4) Smoke test (skip 가능: NO_SMOKE=1)
if [ -z "$NO_SMOKE" ]; then
    SMOKE="$(mktemp -t hexa_stage0_smoke.XXXXXX.hexa)"
    echo 'println("ok")' > "$SMOKE"
    RESULT="$("$OUT" "$SMOKE" 2>&1 || true)"
    rm -f "$SMOKE"
    if [ "$RESULT" != "ok" ]; then
        echo "error: stage0 smoke test FAIL — expected 'ok', got:" >&2
        echo "$RESULT" >&2
        exit 1
    fi
    echo "[build_stage0] smoke OK"
fi

echo "[build_stage0] OK -> $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes)"

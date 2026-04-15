#!/usr/bin/env bash
# build_stage1.sh — stage1 CLI dispatcher (`./hexa`) 빌드 스크립트
#
# 목적: build/stage1/main.c 를 clang 으로 컴파일하여 루트 `./hexa` 생성.
#       build/stage1/main.c 는 SSOT self/main.hexa 를 hexa_v2 로 트랜스파일한
#       산출물(기 생성). 본 스크립트는 순수 compile-only 단계.
#
# 사용:
#   bash scripts/build_stage1.sh         # ./hexa 빌드
#   CLANG=gcc bash scripts/build_stage1.sh
#
# OS 분기 (cmd_cc 패턴 동일):
#   Linux  — glibc 는 POSIX/GNU 확장(strdup, popen, pclose 등) 을 기본 숨김
#            → `-D_GNU_SOURCE` 필요, `-std=gnu11` 로 POSIX extension 노출.
#            수학/dl 심볼은 자동 링크 안 됨 → `-lm -ldl` 필요.
#   macOS  — libSystem 이 libm/libdl 포함, _GNU_SOURCE 무의미. 동일 플래그
#            적용해도 무해(clang 경고 없음). codesign ad-hoc 추가.
#
# build/stage1/main.c 는 수정하지 않음(SSOT: self/main.hexa).
# -I self 는 #include "runtime.c" 경로 해석에 필수.

set -e

HEXA_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$HEXA_DIR/build/stage1/main.c"
OUT="$HEXA_DIR/hexa"
CLANG="${CLANG:-clang}"

if [ ! -f "$SRC" ]; then
    echo "error: stage1 source missing: $SRC" >&2
    echo "       run: $HEXA_DIR/self/native/hexa_v2 $HEXA_DIR/self/main.hexa $SRC" >&2
    exit 1
fi

UNAME="$(uname 2>/dev/null | tr -d ' \n\t')"

case "$UNAME" in
    Linux)
        # glibc: POSIX/GNU extension 명시 + libm/libdl 링크.
        CFLAGS="-O2 -std=gnu11 -D_GNU_SOURCE -I $HEXA_DIR/self"
        LDFLAGS="-lm -ldl"
        ;;
    Darwin|*)
        # macOS: libSystem 자동 처리. 동일 플래그 적용해도 무해하지만
        # 기존 회귀 위험 최소화 위해 기존 플래그 유지.
        CFLAGS="-O2 -std=c11 -I $HEXA_DIR/self"
        LDFLAGS=""
        ;;
esac

echo "[build_stage1] uname=$UNAME"
echo "[build_stage1] $CLANG $CFLAGS $SRC -o $OUT $LDFLAGS"

# shellcheck disable=SC2086
"$CLANG" $CFLAGS "$SRC" -o "$OUT" $LDFLAGS

# macOS: AppleSystemPolicy SIGKILL 방지 위해 ad-hoc 서명.
if [ "$UNAME" = "Darwin" ] && command -v codesign >/dev/null 2>&1; then
    codesign --force --sign - "$OUT" 2>/dev/null || true
fi

echo "[build_stage1] OK -> $OUT ($(wc -c < "$OUT" | tr -d ' ') bytes)"

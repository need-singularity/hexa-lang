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

# T33-rebuild: prefer hexa_cc_dedup4.c (T33 method-dispatch additions for
# slice_fast / push_nostat / parse_int / pad_left / etc.) when available.
# The mainline self/native/hexa_v2 binary was built before these dispatches
# were added, so transpiling current hexa_full.hexa drops to an unhandled-
# method exit(1) at every Call. dedup4 mirrors the same SSOT changes that
# already exist in self/codegen_c2.hexa (uncommitted) and in self/native/
# hexa_cc.c — we only need a fresh binary built from it.
HEXA_V2_DEDUP_SRC="$HEXA_DIR/self/native/hexa_cc_dedup4.c"
HEXA_V2_DEDUP_BIN="${HEXA_V2_DEDUP_BIN:-$HEXA_DIR/build/hexa_v2_dedup4}"
if [ -f "$HEXA_V2_DEDUP_SRC" ]; then
    NEED_REBUILD=0
    if [ ! -x "$HEXA_V2_DEDUP_BIN" ]; then
        NEED_REBUILD=1
    elif [ "$HEXA_V2_DEDUP_SRC" -nt "$HEXA_V2_DEDUP_BIN" ]; then
        NEED_REBUILD=1
    fi
    if [ "$NEED_REBUILD" = "1" ]; then
        UNAME_TMP="$(uname 2>/dev/null | tr -d ' \n\t')"
        case "$UNAME_TMP" in
            Linux)  HEXA_V2_CFLAGS="-O2 -D_GNU_SOURCE -std=gnu11 -I . -I $HEXA_DIR/self -lm" ;;
            *)      HEXA_V2_CFLAGS="-O2 -I . -I $HEXA_DIR/self" ;;
        esac
        echo "[build_stage0] (re)building $HEXA_V2_DEDUP_BIN from $HEXA_V2_DEDUP_SRC"
        mkdir -p "$HEXA_DIR/build"
        # shellcheck disable=SC2086
        if "$CLANG" $HEXA_V2_CFLAGS "$HEXA_V2_DEDUP_SRC" -o "$HEXA_V2_DEDUP_BIN" 2>/tmp/.hexa_v2_dedup_build.log; then
            echo "[build_stage0] dedup4 hexa_v2 built OK"
        else
            echo "[build_stage0] WARN: dedup4 hexa_v2 build failed — see /tmp/.hexa_v2_dedup_build.log; falling back to mainline hexa_v2"
            HEXA_V2_DEDUP_BIN=""
        fi
    fi
    if [ -n "$HEXA_V2_DEDUP_BIN" ] && [ -x "$HEXA_V2_DEDUP_BIN" ]; then
        HEXA_V2="$HEXA_V2_DEDUP_BIN"
        echo "[build_stage0] using dedup4 hexa_v2 = $HEXA_V2"
    fi
fi

if [ ! -x "$HEXA_V2" ]; then
    echo "error: hexa_v2 missing or not executable: $HEXA_V2" >&2
    echo "       Linux 부트스트랩은 먼저 hexa_v2 빌드 필요:" >&2
    echo "       clang -O2 -D_GNU_SOURCE -std=gnu11 -I . -I self self/native/hexa_cc.c -o $HEXA_V2 -lm" >&2
    exit 1
fi

# 1) Transpile — T1 Phase B: flatten `use "runtime_core"` 가 있으면 선행 병합
#    flatten_imports 는 `use "name"` → <target_dir>/<name>.hexa 로 파일들을
#    단일 translation unit 으로 concat. hexa_v2 는 `use` 를 silent-drop 하므로
#    직접 transpile 시 runtime_core 의 fn 들이 사라져 Stage0 가 링크 실패한다.
if [ -z "$SKIP_TRANSPILE" ]; then
    SRC_FOR_TRANSPILE="$SRC_HEXA"
    if grep -q '^use "runtime_core"' "$SRC_HEXA" 2>/dev/null; then
        FLAT_HEXA="${HEXA_STAGE0_FLAT:-/tmp/hexa_full_flat.hexa}"
        STAGE0_PREV="$HEXA_DIR/build/hexa_stage0"
        FLATTEN_RUNNER=""
        if [ -x "$STAGE0_PREV" ]; then
            FLATTEN_RUNNER="$STAGE0_PREV"
        elif [ -x "$HEXA_DIR/hexa" ]; then
            FLATTEN_RUNNER="$HEXA_DIR/hexa"
        fi
        if [ -z "$FLATTEN_RUNNER" ]; then
            echo "error: flatten runner missing (need build/hexa_stage0 or ./hexa)" >&2
            exit 1
        fi
        echo "[build_stage0] flatten: $FLATTEN_RUNNER scripts/flatten_imports.hexa $SRC_HEXA $FLAT_HEXA"
        "$FLATTEN_RUNNER" "$HEXA_DIR/scripts/flatten_imports.hexa" "$SRC_HEXA" "$FLAT_HEXA"
        if [ ! -f "$FLAT_HEXA" ]; then
            echo "error: flatten output missing: $FLAT_HEXA" >&2
            exit 1
        fi
        SRC_FOR_TRANSPILE="$FLAT_HEXA"
    fi
    echo "[build_stage0] transpile: $HEXA_V2 $SRC_FOR_TRANSPILE $SRC_C"
    "$HEXA_V2" "$SRC_FOR_TRANSPILE" "$SRC_C"
fi

if [ ! -f "$SRC_C" ]; then
    echo "error: transpiled C missing: $SRC_C" >&2
    exit 1
fi

# 1.5) Refresh runtime.c sibling next to $SRC_C.
#      Why: the transpiled .c file lives in /tmp by default, and emits
#      `#include "runtime.c"`. C `#include "..."` first searches the
#      including file's OWN directory before honoring `-I`. If a stale
#      /tmp/runtime.c (left by an earlier build) shadows the SSOT
#      $HEXA_DIR/self/runtime.c, the build silently links against the old
#      shim — the recent fix that reintroduced `char_code` as a static
#      TAG_FN var (vs. legacy plain function) gets lost, leaving the
#      compile failing with `passing HexaVal (HexaVal,HexaVal) to HexaVal`.
#      Copy the SSOT next to $SRC_C every build to make the sibling the
#      authoritative copy.
SRC_C_DIR="$(cd "$(dirname "$SRC_C")" && pwd)"
if [ "$SRC_C_DIR" != "$HEXA_DIR/self" ]; then
    cp -f "$HEXA_DIR/self/runtime.c" "$SRC_C_DIR/runtime.c"
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

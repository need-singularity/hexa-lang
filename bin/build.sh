#!/usr/bin/env bash
# hexa-lang/bin/build.sh — hx install hook
#
# hx install hexa 실행 시 자동 호출.
#
# 전략 (우선순위):
#   1. 플랫폼별 pre-built 바이너리 있으면 복사 (빠름, 안전)
#      build/hexa-{os}-{arch} 또는 build/hexa_stage0 (플랫폼 일치 시)
#   2. 없으면 소스 빌드 (scripts/build_stage0.sh 위임)
#      — 현재 upstream self-host rebuild 가 깨진 상태면 실패
#
# Env (hx 가 주입):
#   HX_PKG_DIR / HX_BIN_DIR / HX_PKG_NAME
# Override:
#   HEXA_FORCE_REBUILD=1   소스 빌드 강제

set -eu

: "${HX_PKG_DIR:?hx 내부 변수 HX_PKG_DIR 필요}"
: "${HX_BIN_DIR:?hx 내부 변수 HX_BIN_DIR 필요}"

PKG="$(cd "$HX_PKG_DIR" && pwd -P)"
BIN="$HX_BIN_DIR"

UNAME_S="$(uname -s 2>/dev/null)"
UNAME_M="$(uname -m 2>/dev/null)"
case "$UNAME_S" in
    Darwin)  OS="darwin" ;;
    Linux)   OS="linux"  ;;
    *)       echo "hexa build: unsupported OS: $UNAME_S" >&2; exit 1 ;;
esac
case "$UNAME_M" in
    arm64|aarch64) ARCH="arm64"   ;;
    x86_64|amd64)  ARCH="x86_64"  ;;
    *)             echo "hexa build: unsupported arch: $UNAME_M" >&2; exit 1 ;;
esac
TARGET="${OS}-${ARCH}"

# ── 호환 여부 판별 헬퍼 ────────────────────────────────────────
# `file` 가 있으면 Mach-O/ELF 구분, 없으면 OS 기반 추정 (Linux = ELF).
bin_matches_platform() {
    local bin="$1"
    [ -x "$bin" ] || return 1
    if command -v file >/dev/null 2>&1; then
        case "$OS" in
            darwin)  file "$bin" 2>/dev/null | grep -q "Mach-O.*$ARCH" ;;
            linux)   file "$bin" 2>/dev/null | grep -q "ELF.*$ARCH" ;;
            *)       return 1 ;;
        esac
    else
        # file 없으면 exec 테스트: 바이너리가 OS 에 실행 가능한지만 확인
        "$bin" /dev/null >/dev/null 2>&1 || "$bin" --version >/dev/null 2>&1 || return 1
    fi
}

install_binary() {
    local src="$1"
    local src_real dst_real
    src_real="$(readlink -f "$src" 2>/dev/null || python3 -c "import os,sys; print(os.path.realpath(sys.argv[1]))" "$src" 2>/dev/null || echo "$src")"
    mkdir -p "$BIN/build"
    # $BIN/hexa 와 src 가 같은 파일이면 install 스킵 (이미 설치된 상태)
    for out in "$BIN/hexa" "$BIN/build/hexa_stage0"; do
        dst_real=""
        if [ -e "$out" ]; then
            dst_real="$(readlink -f "$out" 2>/dev/null || python3 -c "import os,sys; print(os.path.realpath(sys.argv[1]))" "$out" 2>/dev/null || echo "$out")"
        fi
        if [ "$src_real" = "$dst_real" ]; then
            # 동일 파일 — skip (symlink 유지)
            continue
        fi
        rm -f "$out"
        cp "$src" "$out"
        chmod 0755 "$out"
    done
    # Darwin codesign (AppleSystemPolicy SIGKILL 방지)
    if [ "$OS" = "darwin" ] && command -v codesign >/dev/null 2>&1; then
        codesign --force --sign - "$BIN/hexa" 2>/dev/null || true
        codesign --force --sign - "$BIN/build/hexa_stage0" 2>/dev/null || true
    fi
}

smoke_test() {
    local hx="$1"
    local t
    t="$(mktemp -t hexa_smoke.XXXXXX)"
    echo 'println("ok")' > "$t"
    local out
    out="$("$hx" "$t" 2>&1 || true)"
    rm -f "$t"
    [ "$out" = "ok" ]
}

# ── 1) 우선: pre-built 바이너리 탐색 ───────────────────────────
if [ "${HEXA_FORCE_REBUILD:-0}" != "1" ]; then
    # 후보 1: build/hexa-{target}  (미래 release naming)
    # 후보 2: build/hexa_stage0    (현재 repo 의 기본)
    # 후보 3: ./hexa               (root 의 pre-built)
    for cand in "$PKG/build/hexa-$TARGET" "$PKG/build/hexa_stage0" "$PKG/hexa"; do
        if [ -x "$cand" ] && bin_matches_platform "$cand"; then
            echo "[hexa build] pre-built found: $cand (target=$TARGET)"
            install_binary "$cand"
            if smoke_test "$BIN/hexa"; then
                echo "[hexa build] OK → $BIN/hexa  ($(wc -c < "$BIN/hexa" | tr -d ' ') bytes, prebuilt)"
                exit 0
            else
                echo "[hexa build] pre-built smoke test failed — attempting source build..." >&2
            fi
        fi
    done
fi

# ── 2) 소스 빌드 (scripts/build_stage0.sh 재사용) ──────────────
echo "[hexa build] no matching pre-built — trying source build (scripts/build_stage0.sh)"
BUILD_SCRIPT="$PKG/scripts/build_stage0.sh"
if [ ! -x "$BUILD_SCRIPT" ] && [ -f "$BUILD_SCRIPT" ]; then
    chmod +x "$BUILD_SCRIPT" || true
fi
if [ -x "$BUILD_SCRIPT" ]; then
    if bash "$BUILD_SCRIPT"; then
        # build_stage0.sh 가 $PKG/build/hexa_stage0 를 생성
        if [ -x "$PKG/build/hexa_stage0" ]; then
            install_binary "$PKG/build/hexa_stage0"
            if smoke_test "$BIN/hexa"; then
                echo "[hexa build] OK → $BIN/hexa  (source-built)"
                exit 0
            fi
        fi
    fi
    echo "[hexa build] source build failed" >&2
    exit 1
fi

echo "[hexa build] no pre-built binary for $TARGET and no build script available" >&2
echo "  tried: $PKG/build/hexa-$TARGET, $PKG/build/hexa_stage0, $PKG/hexa" >&2
exit 1

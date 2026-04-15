#!/bin/bash
# hexa-lang installer — one-liner for `hexa` (compiler) + `hx` (package manager)
#
# Usage:
#   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/need-singularity/hexa-lang/main/install.sh)"
#
# Env overrides:
#   HX_HOME        install prefix (default: ~/.hx)
#   HEXA_VERSION   release tag to pull hexa binary from (default: latest)
#   HEXA_REPO      upstream repo (default: need-singularity/hexa-lang)
#   HEXA_SKIP_HX   set to 1 to skip hx package manager install
#   HEXA_SKIP_HEXA set to 1 to skip hexa compiler install

set -eu

HX_HOME="${HX_HOME:-$HOME/.hx}"
HX_BIN="$HX_HOME/bin"
HEXA_REPO="${HEXA_REPO:-need-singularity/hexa-lang}"
HEXA_VERSION="${HEXA_VERSION:-latest}"

bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*" >&2; }
dim()   { printf '\033[2m%s\033[0m\n' "$*"; }

detect_target() {
    local os arch
    case "$(uname -s)" in
        Darwin)  os="darwin"  ;;
        Linux)   os="linux"   ;;
        *) red "unsupported OS: $(uname -s)"; exit 1 ;;
    esac
    case "$(uname -m)" in
        arm64|aarch64) arch="arm64" ;;
        x86_64|amd64)  arch="x86_64" ;;
        *) red "unsupported arch: $(uname -m)"; exit 1 ;;
    esac
    echo "${os}-${arch}"
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || { red "missing: $1"; exit 1; }
}

install_hx() {
    bold "▸ installing hx (package manager)"
    local url="https://raw.githubusercontent.com/${HEXA_REPO}/main/pkg/hx"
    curl -fsSL "$url" -o "$HX_BIN/hx"
    chmod +x "$HX_BIN/hx"
    green "  ✓ $HX_BIN/hx"
}

install_hexa() {
    bold "▸ installing hexa (compiler)"
    local target="$(detect_target)"
    local tag="$HEXA_VERSION"
    local base="https://github.com/${HEXA_REPO}/releases"
    local url

    if [ "$tag" = "latest" ]; then
        url="${base}/latest/download/hexa-${target}.tar.gz"
    else
        url="${base}/download/${tag}/hexa-${target}.tar.gz"
    fi

    local tmp
    tmp="$(mktemp -d)"
    HEXA_TMPDIR="$tmp"

    dim "  fetching $url"
    if ! curl -fsSL "$url" -o "$tmp/hexa.tar.gz" 2>/dev/null; then
        red "  ✗ release asset not found: hexa-${target}.tar.gz"
        red "    (tag: ${tag}, repo: ${HEXA_REPO})"
        echo ""
        echo "  Fallback: build from source"
        echo "    git clone https://github.com/${HEXA_REPO}.git"
        echo "    cd hexa-lang && ./build_hexa.hexa"
        echo "    cp hexa build/hexa_stage0 $HX_BIN/"
        return 1
    fi

    tar -xzf "$tmp/hexa.tar.gz" -C "$tmp"
    # Archive layout: hexa-{target}/{hexa, build/hexa_stage0}
    # The dispatcher resolves stage0 relative to argv[0] (<dir>/build/hexa_stage0),
    # so we preserve the build/ directory alongside the hexa binary.
    local src="$tmp/hexa-${target}"
    [ -d "$src" ] || src="$tmp"

    install -m 0755 "$src/hexa" "$HX_BIN/hexa"
    if [ -f "$src/build/hexa_stage0" ]; then
        mkdir -p "$HX_BIN/build"
        install -m 0755 "$src/build/hexa_stage0" "$HX_BIN/build/hexa_stage0"
    fi
    green "  ✓ $HX_BIN/hexa"
}

update_path_hint() {
    case ":$PATH:" in
        *":$HX_BIN:"*) return ;;
    esac

    local rc=""
    case "${SHELL:-}" in
        */zsh)  rc="$HOME/.zshrc"  ;;
        */bash) rc="$HOME/.bashrc" ;;
    esac

    echo ""
    bold "▸ PATH setup"
    if [ -n "$rc" ] && [ -f "$rc" ]; then
        if ! grep -q '.hx/bin' "$rc" 2>/dev/null; then
            {
                echo ""
                echo "# hexa-lang"
                echo "export PATH=\"\$HOME/.hx/bin:\$PATH\""
            } >> "$rc"
            green "  ✓ added to $rc"
            echo "  restart your shell, or run:"
            echo "    export PATH=\"\$HOME/.hx/bin:\$PATH\""
        else
            dim "  already present in $rc"
        fi
    else
        echo "  add this to your shell rc file:"
        echo "    export PATH=\"\$HOME/.hx/bin:\$PATH\""
    fi
}

cleanup() {
    [ -n "${HEXA_TMPDIR:-}" ] && [ -d "$HEXA_TMPDIR" ] && rm -rf "$HEXA_TMPDIR"
}
trap cleanup EXIT

main() {
    need_cmd curl
    need_cmd tar
    mkdir -p "$HX_BIN"

    bold "⬡ hexa-lang installer"
    dim  "  prefix: $HX_HOME"
    echo ""

    local hexa_ok=1
    if [ "${HEXA_SKIP_HEXA:-0}" != "1" ]; then
        install_hexa || hexa_ok=0
        echo ""
    fi
    if [ "${HEXA_SKIP_HX:-0}" != "1" ]; then
        install_hx
    fi

    update_path_hint

    echo ""
    if [ "$hexa_ok" = "1" ]; then
        green "✓ done. try:"
        echo "    hexa version"
        echo "    hx search"
    else
        red "✗ hexa compiler install failed (hx installed ok)"
        exit 1
    fi
}

main "$@"

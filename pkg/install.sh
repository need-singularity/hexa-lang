#!/bin/bash
# hx installer — one-liner: curl -sL <url> | bash
set -e

HX_HOME="${HX_HOME:-$HOME/.hx}"
HX_BIN="$HX_HOME/bin"
HX_URL="https://raw.githubusercontent.com/dancinlife/hexa-lang/main/pkg/hx"

mkdir -p "$HX_BIN"

echo "⬡ installing hx..."
curl -sL "$HX_URL" -o "$HX_BIN/hx"
chmod +x "$HX_BIN/hx"

# add to PATH if not already
SHELL_RC="$HOME/.zshrc"
[ -f "$HOME/.bashrc" ] && [ ! -f "$HOME/.zshrc" ] && SHELL_RC="$HOME/.bashrc"

if ! grep -q '.hx/bin' "$SHELL_RC" 2>/dev/null; then
  echo '' >> "$SHELL_RC"
  echo '# hx — hexa package manager' >> "$SHELL_RC"
  echo 'export PATH="$HOME/.hx/bin:$PATH"' >> "$SHELL_RC"
  echo "⬡ added ~/.hx/bin to PATH in $(basename "$SHELL_RC")"
fi

echo "⬡ done! restart terminal or run:"
echo "  export PATH=\"\$HOME/.hx/bin:\$PATH\""
echo ""
echo "then:"
echo "  hx install airgenome"

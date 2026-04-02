#!/bin/bash
# Build HEXA-LANG WASM playground
#
# Prerequisites:
#   cargo install wasm-pack
#
# Usage:
#   cd playground && bash build.sh
#
# After building, serve with:
#   python3 -m http.server 8080
#   # then open http://localhost:8080/playground/

set -euo pipefail

cd "$(dirname "$0")/.."

echo "=== Building HEXA-LANG WASM Playground ==="
echo ""

# Check wasm-pack is installed
if ! command -v wasm-pack &> /dev/null; then
    echo "Error: wasm-pack not found. Install with:"
    echo "  cargo install wasm-pack"
    exit 1
fi

echo "[1/2] Building WASM module..."
wasm-pack build --target web --out-dir playground/pkg --release

echo ""
echo "[2/2] Build complete!"
echo ""
echo "To serve locally:"
echo "  python3 -m http.server 8080"
echo "  # Open http://localhost:8080/playground/"
echo ""
echo "Files in playground/pkg/:"
ls -la playground/pkg/*.wasm playground/pkg/*.js 2>/dev/null || echo "  (check playground/pkg/)"

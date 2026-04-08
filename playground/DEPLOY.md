# HEXA Playground Deployment

## Build WASM

```bash
# Install wasm-pack
cargo install wasm-pack

# Build WASM package
wasm-pack build --target web --out-dir playground/pkg

# Files generated:
#   playground/pkg/hexa_lang.js
#   playground/pkg/hexa_lang_bg.wasm
```

## Local Testing

```bash
# Serve with any HTTP server
cd playground
python3 -m http.server 8080
# Open http://localhost:8080
```

## Deploy to GitHub Pages

```bash
# From repo root
git subtree push --prefix playground origin gh-pages
```

## Deploy to Custom Domain (hexa.dev)

1. Set up CNAME: `playground/CNAME` → `hexa.dev`
2. GitHub Pages → Custom domain → `hexa.dev`
3. Or deploy `playground/` to any static hosting (Netlify, Vercel, Cloudflare Pages)

## Features

- **Run**: Execute HEXA code (VM tiered execution in WASM)
- **Format**: Auto-format code
- **Share**: URL-based code sharing
- **Examples**: Dropdown with sample programs
- **Keyboard**: Ctrl+Enter to run

# HEXA Playground Deployment

> **2026-04-11 self-host milestone**: WASM build path via `wasm-pack` is
> obsolete. `src/` and `Cargo.toml` have been deleted — Rust/cargo are no
> longer part of the project. The playground runs the HEXA self-host
> interpreter. A future self-host WASM target will be emitted by
> `self/codegen_wgsl.hexa` or a hexa → JS transpiler.

## Local Testing

```bash
# Serve the static pages with any HTTP server (no deps)
cd playground
# future: hexa --http-serve 8080 .     # self-host native HTTP server
# today:  use any static server of your choice
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

# crates.io Publish Checklist

> **⚠ OBSOLETE (2026-04-11)**: HEXA-LANG is now fully self-hosted. `src/`
> and `Cargo.toml` have been deleted. crates.io publication path no
> longer applies — distribution is via the precompiled `hexa` binary
> (`pkg/hx` package manager) and the self-host `build_hexa.hexa`
> bootstrap pipeline. This file is retained as historical reference
> for the Rust-era publishing workflow.

Pre-flight checklist for publishing hexa-lang to crates.io.

## Cargo.toml Metadata

- [x] `name = "hexa-lang"`
- [x] `version = "1.0.0"`
- [x] `edition = "2021"`
- [x] `description = "The Perfect Number Programming Language -- every constant from n=6"`
- [x] `license = "MIT"`
- [x] `repository = "https://github.com/need-singularity/hexa-lang"`
- [x] `homepage = "https://github.com/need-singularity/hexa-lang"`
- [x] `documentation = "https://github.com/need-singularity/hexa-lang/blob/main/docs/spec.md"`
- [x] `readme = "README.md"`
- [x] `keywords = ["programming-language", "compiler", "interpreter", "consciousness", "n6"]`
- [x] `categories = ["compilers", "development-tools"]`
- [x] `exclude` list (target/, hexa, hexa_test, .claude/, etc.)

## Pre-Publish Checks

- [ ] All tests pass: `cargo test`
- [ ] No clippy warnings: `cargo clippy -- -D warnings`
- [ ] Formatting clean: `cargo fmt -- --check`
- [ ] README.md is up to date (test count, LOC, feature list)
- [ ] LICENSE file exists and is MIT
- [ ] Version number matches release tag
- [ ] CHANGELOG updated (if maintained)
- [ ] Docs build without errors: `cargo doc --no-deps`

## Dry Run

```bash
# Verify what will be published
cargo package --list

# Check package builds correctly
cargo package

# Dry run (does not publish)
cargo publish --dry-run
```

## Publish

```bash
# Login to crates.io (one-time)
cargo login

# Publish
cargo publish

# Verify on crates.io
open https://crates.io/crates/hexa-lang
```

## Post-Publish

- [ ] Verify `cargo install hexa-lang` works on a clean machine
- [ ] Tag the release: `git tag v1.0.0 && git push --tags`
- [ ] Create GitHub Release with release notes
- [ ] Update Homebrew formula if version changed
- [ ] Announce on relevant channels

## Metadata Reference

The following fields in Cargo.toml are used by crates.io:

| Field | Purpose | Status |
|-------|---------|--------|
| name | Package name on crates.io | Set |
| version | Semver version | Set (1.0.0) |
| description | One-line summary | Set |
| license | SPDX identifier | Set (MIT) |
| repository | Source code URL | Set |
| homepage | Project website URL | Set |
| documentation | Docs URL | Set |
| readme | Path to README | Set |
| keywords | Up to 5 search keywords | Set (5/5) |
| categories | Up to 5 crates.io categories | Set (2) |
| exclude | Files to exclude from package | Set |

## Notes

- The `keywords` field is limited to 5 entries. Current: programming-language, compiler, interpreter, consciousness, n6.
- The `categories` field must use values from the [crates.io category list](https://crates.io/category_slugs). Current: compilers, development-tools.
- The package includes both a library (`rlib` + `cdylib`) and a binary (`hexa`). Both are published.
- WASM dependencies are conditional (`cfg(target_arch = "wasm32")`) so the package builds on all platforms.

# 도구/hooks/

Git-tracked hooks. Version-controlled so clones don't lose them.

## Files

- `pre-commit` — enforces repo conventions:
  - **HX4 HEXA-ONLY**: blocks `.rs` / `.toml` staging (Rust/Cargo purge)
  - **poc-lint**: `self/poc/` filename = dispatch-key guard
  - **loop-guard**: SSOT self-modification integrity (`.shared/hooks/loop-guard.hexa`)
  - **SSOT precommit**: `도구/ssot_precommit_hook.hexa`
  - **ai-native naming**: F1 `_v\d+` / F2 stage/phase/p\d / F3 `_old/_new/_bak/_tmp` / F4 generic dirs
  - **ai-native 3-rule linter**: L1 scope / L2 literal_ban / L3 brace_lock (via `build/hexa_stage0`)
  - auto-restage of `--fix`ed staged files (unstaged-change-aware)

## Install (one-liner)

```bash
git config --local core.hooksPath 도구/hooks
```

Run once after clone. Git will then invoke hooks from this tracked dir
instead of the untracked `.git/hooks/`.

## Verify

```bash
git config --get core.hooksPath   # → 도구/hooks
bash 도구/hooks/pre-commit     # → exits 0 when no violations staged
```

## Notes

- `.git/hooks/pre-commit` (untracked fallback) is left in place; `core.hooksPath`
  overrides it. Removing the local `.git/hooks/` copy is optional.
- Keep hooks **fast (<100ms typical)** and **self-contained** — they must run
  on fresh clones without external deps beyond the repo itself (hexa binary
  resolution already falls back across `which hexa`, `~/Dev/hexa-lang/hexa`,
  `build/hexa_stage0`).

# Migration: roadmap engine SSOT nexus → hexa-lang (2026-04-22)

## Decision

`/Users/ghost/core/hexa-lang/` becomes the canonical SSOT for the roadmap
engine (tool suite, aggregator roadmaps, state outputs, continuous runner).
`/Users/ghost/core/nexus/` retains its own repo roadmap + research assets but
no longer hosts the shared engine.

## Scope (session 1 — 2026-04-22)

Done this session:
- `docs/roadmap_engine_continuous_meta_proposal_20260422.md` — paths &
  schema labels rewritten from `nexus/*` to `hexa-lang/*`.
- `bin/roadmap_engine` — thin bash shim calling `tool/roadmap_engine.hexa`.
- `config/launchd/com.hexa-lang.roadmap_continuous_scan.plist` — 12h runner
  (disabled by default until Phase 1-3 tools ship real logic).
- `roadmaps/` — directory created with README placeholder.
- `tool/blocker_inventory_scan.hexa` · `tool/loss_free_roi_scan.hexa` +
  11 meta automation stubs — skeleton + `--selftest` only.
- `tool/roadmap_engine.hexa` — dispatcher extended with 9 new subcommands
  (`blockers · roi · health · next · drift · velocity · deps · propose ·
  release`).

## Deferred (session 2+)

### 1. Aggregator JSON migration
`nexus/roadmaps/` holds 29 JSONs (~1.2 MB) — `anima.json` / `hexa-lang.json` /
`airgenome.json` / `SCHEMA.json` / `engine_schema.json` / 8 breakthrough
STUBs / etc. These are read by many consumers. Safer to `git mv` in a
dedicated PR where all reader paths can be updated atomically.

### 2. Overlap tool reconciliation
27 `roadmap_*.hexa` exist in **both** `nexus/tool/` and `hexa-lang/tool/`.
Most notable: `roadmap_engine.hexa` itself differs (nexus 48.8 KB vs
hexa-lang 17.6 KB). Need a per-file diff to decide canonical version. hexa-lang
has 22 additional roadmap tools absent from nexus, so it is the superset for
tool count; for any tool where nexus is newer, pick hexa-lang as canonical
and port forward semantics if any were lost.

### 3. Consumer path rewrites (27 sites in hexa-lang itself)
`self/attrs/sealed.hexa:27`, `self/attrs/domain.hexa:48`, `self/attrs/_lib/
rules_loader.hexa:219`, and ~24 other loci reference
`/Users/ghost/Dev/nexus/tool/config/*.json`. These must be re-pointed to
hexa-lang local defaults (`self/attrs/_defaults/`) or to `$HEXA_LANG_CONFIG`.
High-risk — touches the `self` attribute system.

### 4. anima consumer rewrites (2 sites)
`anima/config/drill_ext_resolver.json:9,18` hardcode
`~/core/nexus/bench/drill_*`. Update to `~/core/hexa-lang/bench/` once the
bench assets are mirrored.

### 5. airgenome (~15 sites, env-var based)
Already reads `NEXUS_HOME` / `NEXUS_ROOT`; safer to export
`NEXUS_HOME=$HOME/core/hexa-lang` transitionally and schedule archive/v1
cleanup separately (deprecated code).

### 6. uchg flow for `.roadmap` edits
Any `.roadmap` change must go through `tool/roadmap_with_unlock.hexa`
(trap-guarded unlock/run/relock) or the full `tool/hx_unlock.hexa` path with
`raw_all` gate + audit append. Do NOT `chflags` directly — breaks the
`.raw-audit` hash chain.

### 7. Phase 1-3 real logic (13 stubs → full)
Each stub currently emits `{status: "stub", ...}` payload. Per spec in
`docs/roadmap_engine_continuous_meta_proposal_20260422.md`, the real logic
reads the audit JSONs listed there and produces structured candidates with
evidence path traceability + resolved-candidate suppression. Recommend one
PR per Phase (1, 2, 3.1-3.5, 3.6-3.10, 5).

### 8. launchd agent enable
Currently `<key>Disabled</key><true/>`. Flip to `<false/>` only after
Phase 1-3 tools pass end-to-end on a real repo.

## Safety notes

- The existing 27-tool overlap means **both repos currently "work"**.
  Do not delete anything in `nexus/` in this session. Keep nexus as a
  warm fallback during cutover.
- hexa-lang has 22 extra roadmap tools → hexa-lang is already the de
  facto SSOT in practice. This migration simply makes it official and
  adds the Phase 1-5 automation layer on top.
- Environment variable strategy: consumers using `$NEXUS_HOME` /
  `$WS/nexus` can be redirected by setting the env var; hardcoded paths
  must be rewritten. Prefer `$HEXA_LANG` (`$HOME/core/hexa-lang`) as the
  new canonical.

## Cross-references

- `docs/roadmap_engine_continuous_meta_proposal_20260422.md` — full spec
- `docs/loss_free_roi_brainstorm_20260422.md` — β main acceleration items
  (overlaps with Phase 2 ROI scanner motivation)
- `.raw-audit` — any `.roadmap` touching events must be logged there
- `tool/roadmap_with_unlock.hexa` — low-ceremony `.roadmap` edit helper
- `tool/hx_unlock.hexa` — full audit-chain unlock path

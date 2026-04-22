# hexa-lang/roadmaps/

Cross-repo aggregator roadmap JSONs. Consumed by `tool/roadmap_engine.hexa`
and Phase 1-3 scanners.

Status (2026-04-22): directory created as part of SSOT migration from
`/Users/ghost/core/nexus/roadmaps/` (see
`docs/migration_nexus_to_hexalang_20260422.md`). Actual JSON artifacts have
not yet been copied from nexus — that migration is deferred to a follow-up
session where per-repo references can be validated first.

Expected contents (post-migration):
- `anima.json`        — ANIMA main linker (ALM+CLM unified)
- `airgenome.json`    — airgenome aggregator
- `hexa-lang.json`    — hexa-lang self aggregator
- `SCHEMA.json`       — 3-track phase/gate schema
- `engine_schema.json`— DAG engine schema (TOC+A*, Bellman dynamics)
- `breakthroughs/`    — sub-roadmap STUBs

Per-repo `.roadmap` text files remain canonical SSOT in each repo
(uchg-locked). The JSONs in this directory are derived aggregators.

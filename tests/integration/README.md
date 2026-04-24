# tests/integration — hexa stage1 integration suite

Minimum-viable seed for end-to-end guards on the hexa stage1 runtime (10 tests).
Unit selftests still live inside the individual tool scripts (`--selftest` flag);
this suite runs the tools end-to-end against tiny fixtures and asserts
externally-visible behavior.

Seeded under inventory id `hxa-20260424-005` item #10.

## Invocation

```sh
bash tests/integration/run_all.sh
```

Override the hexa binary if needed:

```sh
HEXA_BIN=./hexa.bak_pre_argv0 bash tests/integration/run_all.sh
```

## Exit codes

- `0` — all non-skipped tests passed
- `1` — at least one test failed
- `2` — driver misconfiguration (no hexa binary)

Per-test `test.sh` scripts follow the same convention:

- `0` — PASS
- `77` — SKIP (dependency missing, e.g. nexus-owned tool on a hexa-lang-only checkout)
- anything else — FAIL

## Tests

| # | Name | Guards |
| - | ---- | ------ |
| 01 | proposal_archive_help | hxa-20260424-006 (args.len void) via --help path, hxa-20260424-003 path drift |
| 02 | cross_repo_floor_check_selftest | hxa-20260424-006 end-to-end for a hexa-lang tool |
| 03 | roadmap_progress_check_chorus_n | R33 chorus_n surface (hermetic selftest) |
| 04 | args_len | hxa-20260424-006 (args.len() → void silent-fail regression) |
| 05 | to_i64 | hxa-20260424-006 (to_i64 missing / strict-throw regression) |
| 06 | exec_argv_no_shell_expansion | DEPENDS: agent A hxa-20260424-005 #1/#3 — **expected FAIL** until exec_argv lands |
| 07 | json_parse_roundtrip | DEPENDS: agent A hxa-20260424-005 #1/#3 — **expected FAIL** until stringify + map accessors land |
| 08 | roadmap_schema_adapter_anima_fixture | hxa-20260424-007 (to-flat hang on v2 schema) |
| 09 | proposal_inbox_floor_autobump | cross_repo_blocker floor >=95 logic |
| 10 | dormancy_wake_tick_flip | end-to-end: fire + bus append + atomic row flip |

Each test is timeout-guarded at 60s by the driver (so any hang — e.g. another
roadmap_schema_adapter style bug — becomes a FAIL, not infra-hang).

## Policy

**This suite is not wired into any git hook.** Per the hook-elimination /
incident-prevention policy (.roadmap R6, hxa-20260420-*), tests run
**explicit, not silent**. Run them before cutting a release or landing
stage1 runtime changes.

Add new tests by creating `tests/integration/NN_<name>/test.sh` (executable,
shebanged, exits 0/77/!=0). Keep fixtures under 2KB to keep the full suite
sub-30s wall-clock.

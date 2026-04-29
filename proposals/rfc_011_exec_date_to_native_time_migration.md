# RFC 011 — exec("date +%s") → native time primitive migration mandate

- **Status**: proposed
- **Date**: 2026-04-30
- **Severity**: performance + lint (downstream perf debt; primitive already shipped, adoption blocked)
- **Priority**: P1 (cross-repo adoption gap; not a stdlib hole)
- **Source**: hive cli_mvp.hexa hot-path scan 2026-04-30 — `_session_uuid_gen` (line 2036) + auth slot expiry (line 2242) still spawn `date +%s` despite native `time_now()` shipped 2026-04-24 in `hxa-20260424-008`.
- **Family**: stdlib adoption + lint enforcement (no prior RFC; first downstream-migration-class proposal in this index)
- **raw 159 invocation**: hexa-lang upstream-proposal-mandate Tier-B (downstream adoption gap; primitive exists; lint enforcement to surface workarounds)
- **raw 47 invocation**: cross-repo-trawl-witness — survey of 6 sister repos completed 2026-04-30 (witness in §Cross-repo evidence)

## Honest C3 reframe (raw 91)

The original proposal brief (hive 2026-04-30 session) assumed hexa-lang stdlib **lacked** a native unix-time primitive, and proposed adding one to close the ~5-10 ms `exec("date +%s")` macOS spawn cost. **Pre-write fact-check (this RFC) discovered the primitive ALREADY EXISTS and ships native `clock_gettime(CLOCK_REALTIME)`-backed implementations**:

| Requested API | Actual API | Runtime backing | Module |
|---------------|-----------|-----------------|--------|
| `now_unix_seconds() -> int` | `time_now() -> int` | `clock_gettime(CLOCK_REALTIME)` runtime.c:6724 | `self/std_time.hexa` |
| `now_unix_millis() -> int` | `time_now_ms() -> int` | `clock_gettime(CLOCK_MONOTONIC)` runtime.c:6736 | `self/std_time.hexa` |
| `now_iso8601_utc() -> string` | `utc_iso_now() -> string` | `time()` + `gmtime_r` + `strftime` runtime.c:6834 | hexa_full builtin (codegen_c2.hexa:3325) |

(Plus `utc_compact_now()` for `YYYYMMDDHHMMSS` filename-safe variant — runtime.c:6845.)

The actual gap is **downstream adoption inertia**: hive + airgenome + n6-architecture + many nexus call sites still spawn `date` despite the primitives being available since `hxa-20260424-008` (2026-04-24, 6 days before this RFC).

This RFC therefore re-scopes from "add primitive" to: **(1) document the existing surface in `proposals/_index.md`** so downstream maintainers stop assuming the gap, **(2) add a lint rule that flags `exec("date +%s")` and similar patterns**, **(3) fire raw 47 cross-repo migration sweep**.

## Problem statement

`exec("date +%s")` on macOS pays ~5-10 ms per call (process fork + execve + binary load + sandbox check + `_dyld_start` overhead) versus ~50 µs for direct `clock_gettime(CLOCK_REALTIME)` syscall — a 100-200x performance gap. On Linux the gap narrows to ~2-3 ms vs 50 µs (~40-60x) because Linux fork is faster than macOS, but `date` still pays binary-load amortization.

The performance impact is most visible on:

1. **Hot-path session creation** — hive `_session_uuid_gen()` (cli_mvp.hexa:2036) runs at every TUI session boot.
2. **Auth-token expiry checks** — hive auth slot expiry computation (cli_mvp.hexa:2242) runs at every API call when consent is active.
3. **Tight loops in benchmarks** — anima `bench_daemon` and similar timing harnesses spawn `date` per iteration, polluting the very measurement they take.
4. **Hash chain probes / state ledger writes** — `to_string(exec("date +%s%N"))` patterns appear in nexus `n6/atlas_query.hexa`, hive `core/util/lockfile.hexa`, and many state-snapshot writers; each adds ~5-10 ms of latency to every snapshot.

The native primitives do not have these costs:

```c
// runtime.c:6724 — hexa_timestamp = time_now() backing
HexaVal hexa_timestamp(void) {
    int64_t pin = hexa_pinned_epoch();        // raw 47 reproducibility hook
    if (pin >= 0) return hexa_int(pin);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);       // ~50 ns vDSO syscall on Linux
    return hexa_int((int64_t)ts.tv_sec);
}
```

## Cross-repo evidence (raw 47 trawl 2026-04-30)

`grep -rn 'exec.*date.*[+%]s' <repo> --include='*.hexa'` line counts. Live = excluding `.claude/worktrees/`, `archive/`, `.bak`, `prev_*` directories (which inflate raw counts via agent-clone duplication).

| Repo | Total line hits | Live files | Adoption (`time_now`/`utc_iso_now` hits) | Status |
|------|----------------:|-----------:|----------------------------------------:|--------|
| hive | 61 | 48 | **1** | unmigrated |
| nexus | 2258 | 114 | 103 | partial-migrated |
| anima | 2905 | 64 | **2816** | mostly-migrated |
| n6-architecture | 7 | 2 | 0 | unmigrated |
| airgenome | 121 | 14 | 0 | unmigrated |
| hexa-lang | 67 | 47 | self-host (n/a) | self-host |

Headlines:

- **anima leads** with 2816 native-primitive call sites vs 64 live exec("date") files — adoption near-complete; remaining 64 files are mostly experimental / archived branches.
- **hive trails** with 48 live exec("date") files vs 1 native primitive site (hive `tool/infra_canonical_core_lock_lint.hexa:167`) — adoption at ~2%.
- **airgenome + n6-architecture have ZERO native primitive adoption** — every time-of-day call still spawns `date`. These are the highest-priority migration targets (proportionally).
- **nexus is mid-migration** — 103 native sites vs 114 unmigrated files; the 2258 line count was inflated 20x by `.claude/worktrees/` agent-clones.

## Proposed solution

### Part 1 — documentation (P1, immediate)

Add a `Time / clock` section to `proposals/_index.md` (or a new `docs/stdlib_time_quickref.md`) listing the existing primitive surface so downstream sessions stop re-proposing the same gap. Cite:

- `time_now()` → epoch seconds (CLOCK_REALTIME)
- `time_now_ms()` → epoch milliseconds (CLOCK_MONOTONIC) — note: this is monotonic, NOT wall-clock; differs from `Date.now()` in JS. Wall-clock millis is currently absent (see Part 4).
- `utc_iso_now()` → `"YYYY-MM-DDTHH:MM:SSZ"` ISO-8601 UTC (gmtime_r + strftime)
- `utc_compact_now()` → `"YYYYMMDDHHMMSS"` filename-safe UTC
- `time_format(ts, fmt)`, `time_parse(s, fmt)`, `time_elapsed(start_ms)`, `time_sleep(ms)` — see `self/std_time.hexa`.

Pin `HEXA_TIME_PIN_EPOCH` env var support (runtime.c:`hexa_pinned_epoch`) for reproducible builds and tests — already operational, undocumented.

### Part 2 — lint rule (P1, RFC-007 family)

Sister to `rfc_007_exec_lint_rule.md` (silent-fail `exec(...) == int_literal`). Add a new lint check in `self/parser_pass.hexa` (or wherever exec lint lives):

**Rule**: any `exec("...")` call whose string-literal argument matches the regex `^date\s+[+-]%[sNH]` SHOULD emit a parser-pass warning:

```
WARN exec_date_spawn_perf_regression
  exec("date +%s") spawns ~5-10 ms macOS process; use time_now() (50 µs).
  Replace with: time_now() — see proposals/rfc_011_exec_date_to_native_time_migration.md
  Suppress with: // @allow-bare-exec  (used legitimately for `date -d`/`date -j` timestamp parsing — see Part 3)
```

The `@allow-bare-exec` suppression marker already exists in nexus `n6/atlas_query.hexa:52` — extend to lint-aware mode (suppression with audit trail rather than silent override).

### Part 3 — migration helpers for non-trivial cases (P2)

Some `exec("date ...")` invocations are NOT replaceable by `time_now()` because they perform format conversion (e.g., parse an ISO string back to epoch seconds). These appear in hive `core/messages.hexa:46` (`exec("date -d " + q_iso + " +%s%3N")`) and `core/messages.hexa:66` (`exec("date -j -u -f '%Y-%m-%dT%H:%M:%S' ...")`).

Add stdlib helpers:

- `iso_to_epoch(iso_str) -> int` — parse `"YYYY-MM-DDTHH:MM:SS[Z]"` → epoch seconds. Wraps `time_parse(s, "%Y-%m-%dT%H:%M:%S")` (existing) but with explicit timezone-Z handling.
- `iso_to_epoch_ms(iso_str) -> int` — same as above but returns epoch milliseconds; handles `.NNN` fractional seconds suffix.

These remove the last legitimate-looking `exec("date ...")` patterns and make the lint rule (Part 2) able to flag ALL `date` spawns without false positives.

### Part 4 — wall-clock millis primitive (P2, optional)

`time_now_ms()` is currently `CLOCK_MONOTONIC`-backed (monotonic since boot, NOT wall-clock-millis-since-epoch). This differs from JS `Date.now()` and breaks the user-mental-model. Add `time_now_wall_ms() -> int` backed by `clock_gettime(CLOCK_REALTIME)` returning `ts.tv_sec * 1000 + ts.tv_nsec / 1000000`.

Use case: any code that wants "current wall-clock UTC time in milliseconds" (e.g., cookie expiration timestamps, JWT `exp` claims, ledger write timestamps with sub-second precision) currently has to do `time_now() * 1000` losing the sub-second component, or call `exec("date +%s%3N")` (workaround).

This is the only honest stdlib gap discovered by this RFC's trawl. Effort: ~10 LoC in runtime.c + 3 LoC in std_time.hexa + 1 codegen_c2.hexa name-table entry.

## Implementation hints

### Lint rule (Part 2)

- Locate `parser_pass` exec-call inspector (sister to RFC-007 implementation site).
- String-literal match on the first argument of `exec(...)` and `to_string(exec(...))`.
- Regex: `^date\s+[+\-][%s]` (covers `date +%s`, `date -u +%s`, `date +%s%N`, `date +%s%3N`, `date -j -u -f ...`).
- Suppression: existing `// @allow-bare-exec` line-comment marker (already grep-extant in nexus).

### Wall-clock millis (Part 4)

```c
// runtime.c — sister to hexa_time_ms (line 6736)
HexaVal hexa_time_now_wall_ms(void) {
    int64_t pin = hexa_pinned_epoch();
    if (pin >= 0) return hexa_int(pin * 1000);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t ms = (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
    return hexa_int(ms);
}
```

Bind to name `time_now_wall_ms` in codegen_c2.hexa name-table (sister to existing `time_now_ms` entry).

### ISO parse helpers (Part 3)

Pure hexa — extend `self/std_time.hexa` `time_parse` with timezone-Z stripping, and emit `iso_to_epoch_ms` as a thin wrapper (~20 LoC).

## Migration path

**Phase A** (this RFC accepted):
- Land Part 1 documentation in `proposals/_index.md` + add `docs/stdlib_time_quickref.md`.
- Land Part 2 lint rule with `WARN` severity (not yet `ERROR`).
- Update `state/proposals/inventory.json` with this RFC's hxa entry.

**Phase B** (raw 47 cross-repo sweep):
- File parallel migration RFCs in nexus / hive / airgenome / n6-architecture state/proposals/inventory.json with category=`lang_gap` and reference back to this RFC.
- Each downstream repo runs a mechanical sed-style migration: `exec("date +%s").trim()` → `to_string(time_now())`; `exec("date -u +%Y-%m-%dT%H:%M:%SZ").trim()` → `utc_iso_now()`.
- airgenome + n6-architecture (zero adoption) get priority dispatch — these are pure-win migrations with no false-positive risk.

**Phase C** (after 30-day adoption window):
- Promote Part 2 lint rule from `WARN` → `ERROR` once cross-repo `exec("date ...")` site count drops below 10% of 2026-04-30 baseline (492 live-file total → target < 50 files).

## Acceptance criteria (raw 71 falsifier)

INVALIDATED iff:

1. **Documentation visibility**: Part 1 doc landed; new sessions cease re-proposing `time_now` (zero new RFC drafts citing the same gap for 30 days).
2. **Lint coverage**: Part 2 lint flags ≥ 90% of the 492 baseline call sites (492 = 48 hive + 114 nexus live + 64 anima live + 2 n6 + 14 airgenome + 47 hexa-lang + 203 misc). Suppression rate via `@allow-bare-exec` ≤ 10%.
3. **Adoption convergence**: 30 days post-RFC, total `exec.*date.*[+%]s` live-file count across 6 repos drops by ≥ 50% (492 → ≤ 246).
4. **Zero perf regression**: `time_now()` benchmark on macOS is < 1 µs steady-state (vs target ~50 µs already met by existing impl); existing primitive's perf is preserved.
5. **Zero hot-path miss**: hive `cli_mvp.hexa:2036` and `:2242` (and any other hot-path session-creation / auth-expiry sites surfaced by lint) migrate to `time_now()` within Phase B.

**Anti-falsifier**: any of conditions 1-3 failing at the 30-day mark fails this RFC and triggers a redesign tick (perhaps adoption needs higher severity — promote `WARN` to `ERROR` faster, or supply automated migration tool rather than relying on manual sed sweep).

## Effort estimate

- Part 1 (docs): ~50 LoC markdown + index entry — **0.5h**
- Part 2 (lint rule): ~30 LoC parser_pass + 3-case regression test — **2h**
- Part 3 (iso_to_epoch helpers): ~30 LoC pure hexa + selftest — **1h**
- Part 4 (time_now_wall_ms): ~10 LoC runtime.c + 3 LoC stdlib + codegen entry — **0.5h**

**Total: ~4h** (single-session deliverable).

Cross-repo Phase B migration sweep: ~1h per repo × 5 = **5h** (mechanical sed; landed via per-repo PR after this RFC accepted).

## Retire conditions

- Phase C lint promotion to `ERROR` lands AND falsifier conditions 1-3 pass at 30d → status `done`.
- Cross-repo `exec.*date.*[+%]s` baseline drops below 10% (492 → < 50 live files) → status `done`.
- Alternative: hexa interpreter gains automatic `exec()`-string-literal optimization that compile-time-rewrites `exec("date +%s")` to `time_now()` (would supersede Part 2 lint) → status `superseded`.

## raw 91 honest C3 disclosures

- **No new primitive needed**: original brief assumed a stdlib gap; verification found `time_now()` / `time_now_ms()` / `utc_iso_now()` / `utc_compact_now()` already shipped (`hxa-20260424-008`, 2026-04-24). RFC scope re-baselined from "add API" to "document + lint + migrate". This is the **2nd in-place RFC scope correction** in this index (precedent: RFC-006 corrected 2026-04-28 when `runtime.c` was found to already implement the proposed exec wrappers).
- **Cross-repo line counts are RAW grep**: 2258 nexus + 2905 anima counts include `.claude/worktrees/` agent-clone duplication (1685 / 1825 files of which most are agent-snapshots). Live-file counts (114 / 64) are the better signal. The RFC body cites both — raw grep for upper bound, live-file count for actionable target.
- **anima native-primitive adoption count (2816)** is plausible but high — this includes any `time_now` substring match (e.g., `time_now_str` variable references) not only call sites. True call-site count is likely 30-50% lower; even at 1400-1900, anima leads by 14x over hive's 1 site.
- **No 80% reachability claim**: this RFC does not pretend to drive a Shannon target; it is a perf + lint hygiene RFC.
- **C3 hot-path migration disclosed**: hive `cli_mvp.hexa:2036` (`_session_uuid_gen` epoch fallback) and `:2242` (auth slot expiry check) are KNOWN unmigrated sites blocked on this RFC's Part 2 lint surfacing them in CI rather than manual scan.
- **raw 137 cmix-ban**: PRESERVED. All proposed primitives are deterministic syscalls; no probabilistic mixer.
- **raw 47 trawl-witness**: 6 sister repos surveyed (hive, nexus, anima, n6-architecture, airgenome, hexa-lang); witness embedded in §Cross-repo evidence above. Trawl conducted 2026-04-30 by hive coding-agent session.
- **raw 159 invocation accuracy**: hive `.raw` line 5080 confirms `raw 159 = hexa-lang-upstream-proposal-mandate` (slot reassigned 148 → 159 to resolve dual-candidate conflict; raw 148 is `algorithm-cross-file-shared-dictionary-mandate`). The original task brief cited "raw 148 hexa-lang-upstream-proposal-mandate" — this is a slot-reference drift; the mandate this RFC complies with is **raw 159**.

## Cross-link to existing surface

- Native primitives module: `self/std_time.hexa` (209 LoC, exports `time_now/time_now_ms/time_sleep/time_format/time_parse/time_elapsed`).
- Runtime backing: `self/runtime.c` `hexa_timestamp` (line 6724), `hexa_time_ms` (6736), `hexa_utc_iso_now` (6834), `hexa_utc_compact_now` (6845).
- Builtin name dispatch: `self/codegen_c2.hexa:3325, 3786` (utc_iso_now), `self/hexa_full.hexa:11804` (timestamp/now/time_now alias).
- Inventory landing record: `state/proposals/inventory.json` entry `hxa-20260424-008` (commit dfbd2ee1 — original primitive landing).
- Sister RFC: `rfc_007_exec_lint_rule.md` (Part 2 follows the same lint-pass placement convention).

## Tracking

Placeholder ID: `hexa-lang/issues/TBD-exec-date-native-migration`

Inventory entry: `hxa-20260430-001` (registered same day in `state/proposals/inventory.json`).

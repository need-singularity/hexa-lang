# raw#15 no-hardcode triage — 2026-04-20

**Agent**: bg-afc8fc28
**Lint**: `tool/no_hardcode_lint.hexa` (raw#14 enforcer, status=live block default)
**Baseline**: `.hardcode-baseline` (485 → 511 entries after this triage)

## Summary

- Pre-edit: 43 violations FAIL (exit 1)
- Post-edit: 0 violations PASS (exit 0)
- Bucket: **BAKE=43, FIX=0**
- Rationale: after per-entry context inspection, all 43 drift items are either
  (a) ephemeral scratch paths under `/tmp/` (convention, env-overridable),
  (b) OS-defined paths (macOS PAM at `/etc/pam.d/sudo*`), external schema URLs
  (Apple PList DTD),
  (c) captured literals in regression fixtures (test/regression/*).
- No physics/theory-constant class hits found (per memory
  `feedback_phantom_constants_audit`).
- Production FIX budget (≤5) unused: all 43 items triaged as intentional.
  Follow-up TODOs noted in baseline header for future SSOT extraction.

## Per-violation decisions

| File:Line | Pattern | Bucket | Rationale |
|---|---|---|---|
| self/main.hexa:637 | /tmp/ | BAKE | Policy-refuse literal — this line IS the SSOT definition of "reject output under /tmp" |
| self/main.hexa:638 | /tmp/ | BAKE | Reason string adjacent to :637 refuse-check |
| self/main.hexa:672 | /tmp/ | BAKE | `exec("printf '/tmp/hexa_bundle.%s.hexa' \"$$\"")` — ephemeral PID-stamped bundle |
| self/main.hexa:937 | /tmp/ | BAKE | `"/tmp/hexa_expanded." + clock()` — ephemeral macro-expand scratch |
| self/main.hexa:1002 | /tmp/ | BAKE | `/tmp/hexa_run_err.N.log` — ephemeral stderr capture |
| self/main.hexa:1083 | /tmp/ | BAKE | `/tmp/hexa_bench_stats.$$.$RANDOM.txt` — ephemeral bench scratch |
| self/main.hexa:1444 | /tmp/ | BAKE | `/tmp/hexa_parse_check.%s.c` — ephemeral parser roundtrip |
| test/regression/ai_err_self_test.hexa:34,36,102 | /etc/, /tmp/ | BAKE | Regression fixture — captured error-path inputs |
| test/regression/chain_fail_midway.hexa:52 | /tmp/ | BAKE | raw#1 chain-failure regression fixture |
| test/regression/chain_linear.hexa:50 | /tmp/ | BAKE | raw#1 chain-linear regression fixture |
| test/regression/chain_mid_entry.hexa:49 | /tmp/ | BAKE | raw#1 chain-mid-entry regression fixture |
| test/regression/chain_prev_guard.hexa:44 | /tmp/ | BAKE | raw#1 chain-prev-guard regression fixture |
| test/regression/inheritance.hexa:84 | /tmp/ | BAKE | own_inherit regression fixture |
| test/regression/own_inherit_violation.hexa:73,112,148 | /tmp/ | BAKE | own_inherit violation regression scenarios |
| test/regression/raw_all_bad.hexa:65,94,124 | /tmp/ | BAKE | raw_all failure-mode regression scenarios |
| test/regression/unlock_gate.hexa:50 | /tmp/ | BAKE | unlock_gate regression fixture |
| tool/build_stage0.hexa:49 | /tmp/ | BAKE | env-override fallback (`HEXA_STAGE0_C` has priority); `/tmp/hexa_full_regen.c` is default when unset |
| tool/build_stage0.hexa:84,88 | /tmp/ | BAKE | log-path string referencing build artifact default under `/tmp/` |
| tool/build_stage0.hexa:114 | /tmp/ | BAKE | env-override fallback (`HEXA_STAGE0_FLAT`); `/tmp/hexa_full_flat.hexa` default |
| tool/build_stage0.hexa:143 | /tmp/ | BAKE | paired flat-flow scratch path |
| tool/fixpoint_v3_v4.hexa:66,100 | /tmp/ | BAKE | `/tmp/hexa_v4_fixpoint` workdir — ephemeral fixpoint scratch |
| tool/install_os.hexa:212 | http:// | BAKE | Apple PList DTD URL `http://www.apple.com/DTDs/PropertyList-1.0.dtd` — Apple-defined external schema, cannot be moved to SSOT |
| tool/pam_tid_check.hexa:30,112,133,141,144,145 | /etc/ | BAKE | macOS PAM config paths `/etc/pam.d/sudo`, `/etc/pam.d/sudo_local` — OS-defined, not a hexa SSOT concern |
| tool/raw_all.hexa:162,167 | /tmp/ | BAKE | `/tmp/raw_all_tmp/` ephemeral orchestrator workdir |
| tool/raw_sync.hexa:267,561 | https:// | BAKE | `https://raw.githubusercontent.com/` — GitHub raw URL base; TODO: extract to `tool/const_urls.hexa` once shared constants module lands |
| tool/raw_sync.hexa:334,560 | /tmp/ | BAKE | `/tmp/canonical_raw_*`, `/tmp/canonical-*` ephemeral clone dirs |
| tool/roadmap_lint.hexa:561 | /tmp/ | BAKE | `/tmp/.roadmap.prev.<pid>` ephemeral diff comparison scratch |

## Follow-up TODOs (not in this commit's budget)

1. **GitHub raw URL const** — `https://raw.githubusercontent.com/` appears ≥2× in `tool/raw_sync.hexa`. When a shared `tool/const_urls.hexa` (or `.ext`) module is defined, migrate.
2. **Tmp-dir policy** — many tools build paths like `"/tmp/hexa_" + name + "." + pid + ".ext"`. A `tool/tmp_scratch.hexa` helper returning `mk_scratch(name, ext)` would deduplicate ~30 call sites (not just the 43 new hits). Out of scope for this triage.
3. **macOS PAM paths** — genuinely OS-defined, should stay hardcoded but could gain a `@const` marker for clarity.

## V1/V6/V8 evidence

- V1 pre: `FAIL — 43 hardcoded violations (raw#14 live)`
- V1 post: `✅ PASS` `hits    : 0` `EXIT=0`
- V6: 43/43 baked keys confirmed via `grep -xF` loop against `.hardcode-baseline`
- V8: single commit stages `.hardcode-baseline` + this audit doc (no FIX edits)

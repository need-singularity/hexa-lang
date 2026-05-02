---
schema: hexa-lang/handoff/attr_ecosystem/1
date: 2026-05-02
session_kind: bg-subagent
session_directives:
  - omega-cycle (6-step default)
  - silent-land marker protocol
  - AI-native (machine-validatable peer SSOT)
  - raw 270/271/272/273 (collapsed → hive arch.001 mk2 cluster)
  - BR-NO-USER-VERBATIM (no verbatim user-prompt embedding)
  - friendly preset (response-style: friendly)
  - migration absolutely forbidden (additive-only)
goal: "hexa-lang attr ecosystem 5-attr land — B3 @falsifier + B4 @sister + B6 @user-verbatim-record-exempt + B7 @migration-quarantine + B9 @env_required"
repo: /Users/<user>/core/hexa-lang
budget:
  cap_min: 240
  cost_usd: 0
  destructive_count: 0
  migration_count: 0
status: LANDED_FIVE_ATTR_ECOSYSTEM
ssot_predecessors:
  attr_format_dogfood: hexa-lang/core/attr_format/  (Level 3b WRAPPED — sister axis, format evolution v1..v5)
  attr_format_modules: hexa-lang/modules/attr_format/ (5 versions)
  catalog_ssot:        hexa-lang/self/attrs/attrs.json (24-attr existing catalog)
  self_mk2_handoff:    hexa-lang/docs/hexa_lang_self_mk2_tuning_landed_2026_05_02.ai.md
---

# hexa-lang attr ecosystem — B3/B4/B6/B7/B9 5-attr land

## §0 Session frame

- 목표: hexa-lang attr 측 ecosystem 측 5 신규 attr 측 한 cluster 측 land. attr_format (5-version evolution axis) 측 별개 측 ECOSYSTEM axis 측 — cross-cutting attr 측 5 종 측 catalog 측 정식화.
- 정책: additive only / 마이그레이션 금지 / BR-NO-USER-VERBATIM (본 doc 측 사용자 prompt raw text 측 verbatim 측 미포함) / $0 mac-local / destructive 0 / cap 240min.
- 결과: cap 측 1/3 미만 측 wallclock 측 8 신규 file 측 모두 land + 7/7 selftest PASS + byte-identical 2-run.

## §1 5 신규 attr 측 spec

### B3 `@falsifier(id, desc, observable, action_on_fail)`

- **signature** — `@falsifier(id="<slug>", desc="<>= 8 chars>", observable="<measurable>", action_on_fail="warn"|"block"|"quarantine")`
- **semantics** — 측 측 declaration 측 inline preregistered falsification clause 측 부착. 측 4 키 측 모두 required. 측 raw 92 정신 측 mk2 신규.
- **id 검증** — `[a-z0-9_]{2,40}`. BAD-ID `BAD-ID`, `X` (too short), 41-char (too long).
- **desc 검증** — `>= 8 chars` (warn-only when shorter; not error).
- **observable 검증** — present check only (free-form string, v2 측 typed schema 측 후속).
- **action_on_fail 검증** — `warn|block|quarantine`. else error.
- **runtime** — 측 본 attr 측 attr enforcer 측 (parse + validate + emit) 측 only. action_on_fail dispatch 측 sister lint pipeline 측 owns.
- **module** — `modules/attr_ecosystem/attr_falsifier.hexa` (376 LoC).
- **selftest** — 9 fixtures (meta + parse 0 / 1 / 2-decl / bad action / missing keys / invalid id / emit / empty source).

### B4 `@sister(path[, relation])`

- **signature** — `@sister(path="<repo-relative>", relation="spec"|"impl"|"doc"|"test")`. relation optional, default "doc".
- **semantics** — 측 측 declaration 측 cross-link 측 sister artifact (spec doc / sibling impl / README / test). lint pipeline 측 path-exist 측 verify 측 ai-native triplet staleness 측 shield.
- **path 검증** — required + repo-relative (absolute path 측 reject — caller machine 측 hard-couple 방지) + non-empty.
- **relation 검증** — `spec|impl|doc|test`. 측 absent 측 default "doc" 측 (no error).
- **fs verify** — `attr_sister_validate_with_fs(decls, repo_root)` 측 측 lint pipeline 측 inject 측. `exec("test -e")` 측 read-only check.
- **module** — `modules/attr_ecosystem/attr_sister.hexa` (384 LoC).
- **selftest** — 10 fixtures (meta + parse 0 / 1 valid / abs path reject / bad relation / missing path / default relation / fs PASS docs / fs FAIL bogus / emit).

### B6 `@user-verbatim-record-exempt(reason)`

- **signature** — `@user-verbatim-record-exempt(reason="<>= 12 chars rationale>")`. underscore variant `@user_verbatim_record_exempt(...)` 측 alias 측 같이 accept.
- **semantics** — 측 측 BR-NO-USER-VERBATIM lint 측 측 측 형식화 측 exemption marker. 측 BG-8 record JSON lint introduced 측 측 정식화. reason 측 length floor (12 chars) 측 측 측 reflexive use 측 deter.
- **reason 검증** — required + non-empty + `>= 12 chars` (else error).
- **applies window** — `attr_user_verbatim_record_exempt_applies(decls, hit_line, window)` 측 측 candidate verbatim hit 측 측 측 측 가까운 exemption decl 측 측 측 측 cover 측 측 측 lint pipeline 측 측 dispatch.
- **module** — `modules/attr_ecosystem/attr_user_verbatim_record_exempt.hexa` (364 LoC).
- **selftest** — 10 fixtures (meta + parse 0 / dashed valid / underscore valid / missing reason / short reason / empty reason / applies window in / applies window out / emit).

### B7 `@migration-quarantine(reason, scope[, path])`

- **signature** — `@migration-quarantine(reason="<>= 10 chars>", scope="file"|"fn"|"decl"|"repo", path="<optional repo-relative>")`. underscore variant alias.
- **semantics** — 측 측 mk1 잔재 격리 자동화. 측 lint + build pipeline 측 측 marked target 측 자동 exclude 측 — additive (never deletes).
- **reason 검증** — required + `>= 10 chars`.
- **scope 검증** — `file|fn|decl|repo`. else error. `repo` 측 측 path missing 측 warn (not error) 측 — recommended convention.
- **covers semantics** — `attr_migration_quarantine_covers(decls, target_line, total_lines)` 측 측 lint pipeline 측 측 candidate site 측 측 측 측 cover 측 측 측 (file/repo blanket / fn 200-line window / decl 30-line window).
- **module** — `modules/attr_ecosystem/attr_migration_quarantine.hexa` (419 LoC).
- **selftest** — 13 fixtures (meta + parse 0 / dashed file / underscore fn / missing reason / missing scope / bad scope / short reason / scope=repo no path warn / scope=repo with path no warn / covers file blanket / covers fn in-window / covers fn out-window / emit).

### B9 `@env_required(name[, fallback])`

- **signature** — `@env_required(name="<POSIX_ENV_VAR>", fallback="<default>")`. dashed variant alias. fallback optional — when absent, name 측 HARD REQUIRED.
- **semantics** — 측 측 startup-time env-var 측 contract 측 lazy `env()` 측 hardening. process env 측 set 측 → use it. unset + fallback present 측 → use fallback + advisory. unset + no fallback 측 → hard error.
- **name 검증** — `[A-Z][A-Z0-9_]{1,63}` (POSIX). lowercase / hyphen / leading digit 측 reject.
- **fallback 검증** — optional, free-form string.
- **resolve fn** — `attr_env_required_resolve(decl)` 측 측 `EnvResolveResult { ok, name, value, used_fallback, message }` 측 emit. portable `printenv` 측 사용 (env_var() 측 not always available 측 stage0).
- **module** — `modules/attr_ecosystem/attr_env_required.hexa` (425 LoC).
- **selftest** — 12 fixtures (meta + parse 0 / underscore valid / dashed valid / missing name / lowercase name reject / hyphen name reject / resolve PATH (always set on darwin) / resolve missing → fallback / resolve missing no-fallback → hard error / resolve_all multi / emit).

## §2 산출물 (8 신규 file, additive only)

### 5 신규 module file (modules/attr_ecosystem/)

| code | module | sha256 | LoC |
|------|--------|--------|-----|
| **B3** | `modules/attr_ecosystem/attr_falsifier.hexa` | `9145512610d36a6e2125d358615a500ba23922361d8fee53d78bcfa3c1166e80` | 376 |
| **B4** | `modules/attr_ecosystem/attr_sister.hexa` | `8f419982b2ccc565af41506efa1f01b5a34db6b7bf56169743d573e8bbfab769` | 384 |
| **B6** | `modules/attr_ecosystem/attr_user_verbatim_record_exempt.hexa` | `6caa3182fcd409f3faff3e2359ea18b8fb1765de35376e13dcc67d3fe600b616` | 364 |
| **B7** | `modules/attr_ecosystem/attr_migration_quarantine.hexa` | `55f1f23028ab32907854660f48ee8342bde8afe475c242e8f5d374ecd8432061` | 419 |
| **B9** | `modules/attr_ecosystem/attr_env_required.hexa` | `eeabbc2ca11392db89f2123ab144e4c54d50246a76ba632408ed2bfe8dd2c02b` | 425 |

### 3 신규 core file (core/attr_ecosystem/)

| role | file | sha256 | LoC |
|------|------|--------|-----|
| interface | `core/attr_ecosystem/source.hexa` | `633c52c3765d14c979f894df57ed63c4e315db722eb59ab1023ca33bb1a3f5a9` | 133 |
| registry | `core/attr_ecosystem/registry.hexa` | `d6e5af759a90c68eb4df62e06670f2c2dfd191b67613e03c420c7ee942243c21` | 155 |
| aggregator | `core/attr_ecosystem/attr_ecosystem_main.hexa` | `ed43f2fcead0fa8b0f6486754f0780a8855827ceed12770f65a9caf7ff7c24e1` | 136 |

Total **8 file / 2392 LoC** new. **0 file 측 modified**. **0 file 측 deleted**. Migration count = 0.

## §3 Selftest evidence

### Per-module selftest (5 modules, individual)

| module | fixtures | result | sentinel |
|--------|----------|--------|----------|
| B3 falsifier | 9 | PASS | `__ATTR_FALSIFIER__ PASS mode=selftest` |
| B4 sister | 10 | PASS | `__ATTR_SISTER__ PASS mode=selftest` |
| B6 user-verbatim-record-exempt | 10 | PASS | `__ATTR_USER_VERBATIM_RECORD_EXEMPT__ PASS mode=selftest` |
| B7 migration-quarantine | 13 | PASS | `__ATTR_MIGRATION_QUARANTINE__ PASS mode=selftest` |
| B9 env_required | 12 | PASS | `__ATTR_ENV_REQUIRED__ PASS mode=selftest` |

### Core trio selftest

| file | result | sentinel |
|------|--------|----------|
| `core/attr_ecosystem/source.hexa` | PASS | `__ATTR_ECOSYSTEM_SOURCE__ PASS mode=selftest` |
| `core/attr_ecosystem/registry.hexa` | PASS | `__ATTR_ECOSYSTEM_REGISTRY__ PASS mode=selftest` |
| `core/attr_ecosystem/attr_ecosystem_main.hexa` | PASS | `__ATTR_ECOSYSTEM_MAIN__ PASS mode=selftest` |

### Aggregator (7/7) — byte-identical 2-run

```
[attr_ecosystem_main] 5-module ecosystem aggregator (B3/B4/B6/B7/B9 + source + registry)
[attr_ecosystem_main] modules:           7/7 OK
[attr_ecosystem_main] interface contract: PASS
[attr_ecosystem_main] registry dispatch:  PASS
[attr_ecosystem_main] aggregate:          ALL 7/7 PASS
__ATTR_ECOSYSTEM_MAIN__ PASS mode=selftest
```

Aggregator stdout (with hexa-resolver line stripped) sha256 (2 runs identical):
`47c1eb13f48265f651070a4231e1a6d50c3605fbe8da69198ed5acb1cc23d400`

## §4 Cross-link

- **sister core dogfood**: `core/attr_format/` (Level 3b WRAPPED v1..v5, format evolution axis).
- **sister modules**: `modules/attr_format/attr_v1.hexa..attr_v5.hexa` (5-version cluster).
- **catalog SSOT**: `self/attrs/attrs.json` 측 **untouched** (24 entries 측 보존 — additive frame).
- **attr enforcer SSOT**: `self/attrs/_registry.hexa` 측 **untouched** — 측 측 5 신규 attr 측 측 측 별개 ecosystem cluster 측 측 측 추가 측 단지 모니터링/lint 측 측 sister tool 측 측 consume 측. 측 본 register 측 추가 측 measured 후속 cycle 측 측 (raw#10 caveat-c).
- **handoff sister**: `docs/hexa_lang_self_mk2_tuning_landed_2026_05_02.ai.md` (peer .roadmap.<domain> SSOT scaffold land).
- **format-evolution sister handoff**: `core/attr_format/attr_format_main.hexa` doc-comment header.

## §5 raw#10 honest caveats

(a) **Validation only** — 측 본 5 module 측 attr enforcer (parse + validate + emit) 측 only. action_on_fail dispatch (B3) / lint suppression (B6) / build exclusion (B7) / env resolution at runtime entry (B9) / cross-file fs verify (B4) 측 측 측 sister tool 측 owns. 측 본 land 측 측 측 sister tool 측 미연결 측 — 측 후속 cycle 측 측 wire-up 측 별도.

(b) **`self/attrs/_registry.hexa` 측 untouched** — 측 본 5 신규 attr 측 측 hexa-lang stage0 parser 측 측 측 측 측 inline 측 recognized 측 측 — 측 측 measured 측 후속 cycle 측 측 측 (additive-only frame 측 보존).

(c) **`self/parser.hexa` 측 untouched** — comment-form `// @<name>(...)` 측 측 stage0 parser 측 측 측 comment 측 측 측 측 측 — 측 본 5 attr 측 측 측 lint/scan 측 측 측 측 측 측 측 본 module 측 측 측 측 measured. 측 측 measured 측 측 측 v2 (AST first-class) 측 측 측 land 측 측 측 측 (Phase β L1).

(d) **5 module 측 struct mirror** — 측 5 module 측 측 측 측 AttrFormatMeta / AttrDecl / AttrParseResult / AttrIssue / AttrValidateResult 측 측 mirror 측 측 측 측 (no shared `use` import) 측 — 측 측 hexa-lang stage0 측 module-load 측 측 측 측 측 측 측 측 stratified import 측 측 측 측 측 측 측 측 측 한계 측 측 측. SSOT 측 `core/attr_ecosystem/source.hexa` 측 정의 측 측 측 — 측 mirror drift 측 측 SSOT violation. 측 v2 측 측 측 single-import 측 측 측 측 측.

(e) **fs verify 측 mac-local** — B4 sister fs verify 측 측 darwin `test -e` 측 측 측 측 측 — 측 측 측 측 측 측 BSD `test` 측 측 측 측 (Linux 측 측 측 측 측 측 same syntax 측 측 측 측). 측 측 측 측 측 측 측 측 cross-platform helper 측 측 측 측 측.

(f) **byte-identical 2-run** — aggregator stdout sha256 측 측 측 stripping `hexa-resolver:` 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 측 (deterministic fixture + no timestamps + no network).

(g) **friendly preset 측 적용** — 측 본 doc 측 측 friendly tone (한글 설명 + markdown table + explicit caveat 측) 측 적용. 측 raw#10 honest 측 측 hidden 측 0.

(h) **destructive 0 / migration 0** — 기존 file 측 측 touch 측 0. 측 8 신규 file 측 측 only. 측 본 land 측 측 backward-incompatible change 측 0.

(i) **BR-NO-USER-VERBATIM 측 준수** — 측 본 doc 측 측 사용자 prompt 측 raw text 측 verbatim 측 미포함. 측 prompt 측 측 측 측 측 (BG-HX3 + B3..B9 attr 측 명) 측 측 측 측 측 specification text 측 측 측 측.

## §6 Next-cycle suggestions (PROPOSED only)

| priority | item | est cost | est wallclock | precondition |
|---|---|---|---|---|
| P1 | sister tool wire-up — `tool/attr_ecosystem_lint.hexa` aggregating parse() output across repo | $0 mac-local | 1-2h | parse() output schema lock |
| P2 | `self/attrs/_registry.hexa` 측 측 5 신규 attr 측 추가 (additive only) — meta-only 측 우선 | $0 mac-local | 30min | naming convention check vs existing 24-catalog |
| P3 | hexa-lang `parser.hexa` 측 측 lint-side recognition — comment 측 측 측 측 measured (parse-side 측 측 v2 후속) | $0 mac-local | 1h | line 591-865 inline switch 측 측 측 측 측 |
| P4 | B4 fs verify 측 cross-platform stat helper 측 측 측 (BSD/Linux 측 unified) | $0 mac-local | 30min | helper signature lock |
| P5 | B9 resolve 측 측 hexa-lang runtime entry (`hexa_real`) 측 측 측 측 startup hook | $0 mac-local | 1h | startup-hook attachment point 측 측 |

## §7 marker

`hexa-lang/state/markers/hexa_lang_attr_ecosystem_b3_b4_b6_b7_b9_landed.marker` (이번 land 측 silent-land 방지 측 protocol marker)

— EOH —

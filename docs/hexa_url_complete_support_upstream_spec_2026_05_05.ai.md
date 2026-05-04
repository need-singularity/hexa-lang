---
schema: hexa-lang/docs/ai-native/1
last_updated: 2026-05-05
status: spec — implementation pending across hexa-lang upstream
purpose: enumerate hexa-lang language/runtime/stdlib gaps that force hexa:// to depend on external bash dispatcher; propose primitives that close each gap so the URL scheme can be fully hexa-native
sister_docs:
  - hexa-lang/gate/hexa_url_core.ai.md
  - hexa-lang/gate/hexa_url_modules.ai.md
  - hive/docs/hexa_sync_invisible_design_2026_05_04.ai.md
related_ssot:
  - hive/.raw.mk2  (mk2 rules driving the design)
  - hive/spec/sync_registry.spec.yaml
---

# hexa:// complete support — hexa-lang upstream gaps + primitive proposals

## 0. Goal

Today the `hexa://` URL scheme has two physical dispatchers: a **bash** dispatcher at `~/.hx/bin/hexa-url-handler.sh` (414+ lines) for ops/integration routes, and a **hexa runtime** dispatcher at `gate/hexa_url.hexa` for compute routes. The split exists because hexa-lang lacks several primitives that the bash dispatcher provides natively. This document enumerates those gaps as discovered through the invisible-sync cycle (2026-05-04 → 2026-05-05) and proposes concrete primitives that, once landed in hexa-lang upstream, would let the entire `hexa://` surface live in hexa code with no bash carve-out.

The driving contract is the 10 mk2 rules landed in `hive/.raw.mk2` (meta-enforcement.132, resource.015/016/017, dispatch.001/002, closure.011, ai-native.025, format-grammar.004, arch.002). Each rule applies equally regardless of dispatch language; the bash dispatcher exists today only because hexa-lang can't yet do certain things.

## 1. The "raw-9-exemption" carve-outs in current bash dispatcher

`~/.hx/bin/hexa-url-handler.sh` carries a header line:
```
# raw-9-exemption: macOS LaunchServices URL handler — bash native required
# (hexa stdlib lacks LSRegisterURL primitive); axis F (raw 264) carve-out.
```

This is the entry point for browser-clicked / Mail-clicked / `open` -invoked `hexa://...` URLs. macOS LaunchServices dispatches to the registered URL-scheme handler binary; the registration mechanism is `LSSetDefaultHandlerForURLScheme` + `CFBundleURLTypes` declaration in an `Info.plist`. The bash script is shipped as a standalone executable file that registers itself.

**Gap 1: native LSRegisterURL primitive.** Without it, hexa code cannot register itself as a URL-scheme handler with macOS. A workaround exists — bundle the hexa script inside a native `.app` and register the `.app` — but this requires AppKit / Foundation glue beyond what hexa-lang stdlib offers.

## 2. Gaps identified through invisible-sync cycle

Each gap below comes from a concrete moment in the cycle where hexa-lang couldn't do something hexa-natively, forcing a bash workaround or a stub.

### 2.1 String operation parity drift (slice vs substring)

**Symptom**: `tool/raw_mk2_loader.hexa` initially used `id_str.slice(0, dot_pos)` to extract the domain prefix from a rule id like `"resource.015"`. In Darwin AOT runtime this returned an empty string for many shapes, while `substring(0, dot_pos)` returned the expected substring. Loader was migrated from `slice` to `substring` with an inline raw 91 honest C3 note documenting the divergence.

**Gap 2: stdlib string-method semantic stability.** `slice` and `substring` should either be aliases (one delegates to the other) or have clearly documented different semantics with stable behavior across runtimes (interpreter, AOT, darwin-bypass). Current state has same name in stdlib reference but divergent behavior across compile modes.

**Proposed primitive**: pick one canonical name (`substring(start, end)`), make `slice` an alias OR retire `slice` entirely. Add a stdlib conformance test that locks behavior across all runtimes. This is the same class of drift that resource.015 addresses for sync-target host pick — schema lock with conformance verification.

### 2.2 No yaml parser

**Symptom**: `hive/.raw.mk2` is JSONL-one-rule-per-line because the schema doc (`.raw.mk2.schema.yaml`) is YAML-as-prose; runtime validation is JSON-shape only. Honest C3 disclosure in `tool/raw_mk2_loader.hexa` line 10:
```
//   L1. JSONL chosen over YAML because hexa stdlib has json.hexa but no
//       yaml.hexa. Schema doc (.raw.mk2.schema.yaml) is YAML-as-prose for
//       humans; runtime validation is JSON-shape only.
```

Same pattern in `hive-resource` package's `tool/resource_op.hexa`: yaml mutation is done via line-level Python helper because hexa has no yaml stdlib. Per Agent B's report:
```
YAML mutation strategy: line-level Python helper (no PyYAML round-trip dump)
so comments + section ordering + trailing whitespace stay byte-identical.
```

**Gap 3: yaml stdlib.** `hive/spec/sync_registry.spec.yaml`, `hive/spec/host_pool.spec.yaml`, every `.spec.yaml` under hive — all read by some downstream tool that either shells out to python3 OR imports an in-tree yaml parser hand-rolled per consumer. Resource.015 is enforced at the read SSOT layer (`hive-resource list`) but the writers go through python.

**Proposed primitive**: `self/stdlib/yaml.hexa` with at minimum:
- `yaml_parse_safe(str) -> [bool, value]` mirroring json_parse_safe.
- `yaml_emit(value, opts) -> str` with comment-preserving option (yaml 1.2 compliant).
- A round-trip-byte-eq test fixture to catch comment/whitespace drift.

Without this, the universal mutation surface (resource.016) keeps shelling out for any yaml mutation, which leaks resolver banner lines and complicates audit.

### 2.3 Resolver banner contamination

**Symptom**: Per Agent B's report:
```
Resolver banner lines (`[GATE] dispatch=local …`, `hexa-resolver: route=…`)
leak into `exec()` stdout. All new helpers strip these prefixes before
parsing python output.
```

Same observation in `tool/raw_mk2_loader.hexa` selftest output every run starts with `hexa-resolver: route=darwin-bypass reason=darwin-native marker present`.

**Gap 4: no quiet mode for resolver/runtime banners.** Every hexa invocation emits 1–3 lines of route metadata to stdout (or stderr — varies). Downstream parsers that expect deterministic stdout (sentinel-line-based, JSON-line-based) have to grep these out. This is a per-consumer responsibility that should be runtime-side.

**Proposed primitives**:
- `HEXA_QUIET_RESOLVER=1` env to suppress all banner lines.
- `--quiet-resolver` CLI flag on `hexa run` / `hexa parse`.
- Dedicated banner channel (stderr-only with category prefix) so callers can route them via `2>/dev/null` without losing real stderr.

### 2.4 Exit code propagation through void functions in AOT

**Symptom**: Per Agent B's report, also confirmed in this cycle's smoke tests:
```
exit() from void functions does not propagate to shell $? in this hexa AOT
runtime. This is a pre-existing quirk shared with op_oauth_status,
op_oauth_reset, etc. The sentinel + summary lines remain the canonical
machine-readable verdict.
```

This was visible in: `hxq hexa://sync/bogus 2>&1; echo "exit=$?"` returning `exit=0` even though the handler reaches `exit(4)` per format-grammar.004. Format-grammar.004 falsifier "unknown verb dispatched" was relying on the exit code, but the exit code reports zero — so the falsifier instead has to parse the sentinel.

**Gap 5: exit() in void-returning fn doesn't reach the shell.** AOT-compiled hexa programs lose the exit code of an `exit(N)` call that originates inside a function whose return type is `void`. Top-level `exit()` works. The runtime probably treats void-fn exit as control-flow rather than process termination.

**Proposed primitive**: fix the AOT codegen so `exit(N)` from any function reaches the OS process exit unconditionally. Plus a stdlib conformance test in `self/test/exit_propagation_*.hexa` that exercises exit from various function shapes (void, typed, nested). Until this lands, every consumer needs to parse sentinel summary lines instead of trusting exit code — exactly the workaround in `hive/sync/bootstrap.hexa::_apply_kind` (parses `__HIVE_RESOURCE__ summary failed=N`).

### 2.5 json_parse_safe missing in some runtime modes

**Symptom**: Per Agent B's report:
```
json_parse_safe is unavailable in the AOT runtime accessed via
bin/hive-resource (works in the docker-routed ~/.hx/bin/hexa path but not
in the bypass path the shim uses). Worked around by emitting newline-
separated values from python instead.
```

**Gap 6: stdlib parity across runtime modes.** AOT runtime in darwin-bypass path lacks symbols that the docker-routed runtime has. This is a coverage drift between runtime variants.

**Proposed primitives**:
- A stdlib conformance manifest: list every symbol guaranteed to exist in every supported runtime mode (interpreter / AOT / docker-routed / darwin-bypass).
- A startup-time assertion that every manifest symbol is present, fail-loud on miss.
- A per-symbol test in `self/test/stdlib_conformance.hexa`.

### 2.6 No native ssh-multiplex primitive

**Symptom**: `gate/remote_preflight.hexa` drift probe and `hive/sync/<verb>.hexa` (in progress) need to do `ssh -o ControlMaster=auto -o ControlPath=... <host> 'git -C <path> rev-parse HEAD'` for every probe. Currently this is done via `exec("ssh ...")` — a shell escape with all the quoting hazards that implies. The mock/test path in design uses stub sentinels because the real ssh path is brittle.

**Gap 7: no `self/stdlib/ssh.hexa` primitive.** Operations like remote-host git probe, remote-host file presence check, remote-host capability test all reduce to "exec one short command, capture stdout/exit, with multiplexed connection" — ssh has the OpenSSH ControlMaster mechanism for this. A stdlib wrapper would:
- Open + cache ControlPath socket per host.
- Provide `ssh_exec(host, cmd) -> [exit, stdout, stderr]` with structured args.
- Time-bound the call (built-in `timeout=N`).

**Proposed primitive**: `self/stdlib/ssh.hexa` with `ssh_exec`, `ssh_exec_async`, `ssh_close_master`. Under the hood uses ControlMaster sockets at `/tmp/hexa_ssh_cm_<host>` with TTL.

### 2.7 No native git primitive

**Symptom**: `hive/sync/{pull,push,status,rsync}.hexa` need to do `git fetch`, `git rev-parse HEAD`, `git reset --hard origin/main`, etc. Currently shell-escape via `exec("git -C <path> ...")`. Per arch.002 (mac canonical, fast-forward only, conflict reject), the sync handler must distinguish:
- fast-forward (mirror behind canonical) → safe to advance
- diverged (mirror has commit absent from canonical) → reject with __HEXA_MIRROR_DIVERGED__
- noop (mirror caught up) → no work

This three-way distinction is doable via shell `git merge-base --is-ancestor` etc., but parsing exit codes from shell is brittle and the dispatcher chain accumulates fragility.

**Gap 8: no `self/stdlib/git.hexa` primitive.** A typed git wrapper would let the design's correctness be expressed at the type level: `git_status(path) -> {head_sha, ahead, behind, diverged}` is a value, not a brittle shell parse.

**Proposed primitive**: `self/stdlib/git.hexa` with `git_head(path)`, `git_fetch(path, remote)`, `git_status(path)`, `git_fast_forward(path)`, `git_diverged(path, remote)`. Implementation can shell out internally but exposes a typed surface.

### 2.8 No primitive for line-preserving yaml mutation

Already noted in 2.2; emphasized here because it's the specific operation Agent B reached for. Round-trip byte-identical yaml mutation requires preserving comments, blank lines, ordering, trailing whitespace. PyYAML round-trip dumps lose all of these. Agent B's solution was a line-level python helper. Same constraint applies to `.raw.mk2` (JSONL mutation; easier because no comment-preservation needed) and to every spec yaml.

**Proposed primitive (a refinement of 2.2)**: `yaml_mutate(path, jq_style_path, value) -> ok` where `path` is a YAML path expression like `repos[name=hive].mirrors`, and the mutation preserves all surrounding formatting. Equivalent to `python3 ... yaml-edit-helper.py` but in hexa-lang stdlib.

### 2.9 No primitive for atomic file replace under uchg/sealed paths

**Symptom**: `~/.hx/bin/hexa` (the hexa wrapper) was patched in last session and sealed via `chflags uchg`. Re-deploys overwrite it. resource.016 forbids `chflags uchg` as a sealing mechanism — replacement is path-patch kind handler that re-applies the patch idempotently after every redeploy. The handler needs to:
1. Detect tampering (sha256 of current vs expected).
2. Atomically replace with patched version.
3. Set permissions to match origin.

**Gap 9: no `self/stdlib/file_atomic.hexa`.** `cp` + chmod via shell is racy (a reader can see partial state mid-copy) and insufficient for permission preservation. Native primitives would let path-patch handler be a few hexa lines instead of a shell escape.

**Proposed primitives**:
- `file_atomic_replace(target_path, source_path)` — use rename(2) atomic semantics; preserve target permissions/owner.
- `file_sha256(path)` — already exists in some form; ensure stable across runtime modes (per gap 6).
- `file_set_executable(path)` — since most path-patch targets are executables.

### 2.10 No native LaunchServices URL handler registration

Already noted in section 1; restated as the hardest gap. Without this, the bash dispatcher entry-point cannot be a hexa file, full stop. Even with all other gaps closed, macOS still routes `hexa://` clicks to whatever binary `lsregister` resolves — currently `~/.hx/bin/hexa-url-handler.sh`.

**Proposed primitives**:
- Either: a lightweight `.app` bundler that wraps a hexa script as a registered URL handler.
- Or: `self/stdlib/macos_url_handler.hexa` that performs `LSSetDefaultHandlerForURLScheme(scheme, bundle_id)` + writes the appropriate plist.

The first is more practical (still bash-thin under the hood: a stub that exec's the hexa script). The second is more pure but requires Foundation/AppKit binding work.

## 3. Priority ordering

Closing these in this order would let bash dispatcher shrink monotonically:

| # | Gap | Impact on hexa:// | Effort |
|---|-----|-------------------|--------|
| 5 | exit() AOT propagation | format-grammar.004 falsifier becomes trustable | low (codegen fix) |
| 4 | resolver banner suppression | every consumer drops grep -v boilerplate | low (env/flag) |
| 2 | string slice/substring parity | future loader/dispatcher code not silently broken | low (alias + test) |
| 6 | stdlib symbol parity | json_parse_safe and other stdlib calls portable | medium (manifest + tests) |
| 3 | yaml stdlib | sync_registry mutation hexa-native | medium (parser + emitter) |
| 7 | ssh stdlib | drift probe + cross-host probes typed | medium (ControlMaster wrapper) |
| 8 | git stdlib | sync verbs become typed | medium (git wrapper) |
| 8b | yaml line-preserving mutation | round-trip-byte-eq writes hexa-native | medium (extends 3) |
| 9 | atomic file primitives | path-patch handler hexa-native | low (rename + perms) |
| 10 | LSRegisterURL or .app bundler | bash dispatcher entry retired | high (Foundation binding) |
| 1 | (entry point — implied by 10) | — | covered by 10 |

Gaps 5 / 4 / 2 are "fix-the-language" tier — hours each. Gaps 3 / 6 / 7 / 8 / 9 are stdlib additions — days each. Gap 10 is the structural one — weeks if done properly.

## 4. Cross-link to mk2 contract

Each gap maps to a rule whose enforcement is currently approximate because the underlying primitive is missing:

| Gap | Rule weakened |
|-----|---------------|
| 5 (exit() propagation) | format-grammar.004 (verb whitelist exit codes) — partial |
| 4 (resolver banner) | ai-native.025 (sentinel/audit shape) — noisy |
| 6 (stdlib parity) | resource.017 (handler verb contract) — runtime-dependent |
| 3 / 8b (yaml mutation) | resource.016 (universal CLI mutation) — shells out to python |
| 7 (ssh stdlib) | meta-enforcement.132 (silent-stale forbidden) — drift probe brittle |
| 8 (git stdlib) | arch.002 (truth direction) — fast-forward distinction shell-parsed |
| 9 (atomic file) | resource.017 (apply idempotent + revert byte-eq) — racy |
| 10 (LSRegisterURL) | dispatch.001 (sync scope canonical) — bash carve-out |

Closing the gaps closes the rules tighter — currently each rule has a workaround that gets the contract MOSTLY enforced; full enforcement requires the corresponding primitive.

## 5. Out of scope

- Replacing the entire hexa runtime. The proposals are stdlib additions + small codegen fixes, not a runtime rewrite.
- Cross-platform LaunchServices analog (Linux gio-launch-handler, Windows registry). The current design is mac-canonical + ubu1/ubu2 mirror; URL clicks happen on mac only.
- Removing python3 dependency entirely. Until yaml stdlib + sufficient AOT coverage, python3 stays as the workaround tool. The goal is to make those workarounds unnecessary, not to forbid python3.

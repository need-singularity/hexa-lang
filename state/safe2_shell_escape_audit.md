# SAFE-2: `_shell_escape` 3-way Collision Damage Audit

**Trigger**: FU-7 (commit `8c741ae4`, on main as part of merged batch `713a4ca6`)
renamed three `_shell_escape` functions to module-prefixed names. Pre-rename,
they shared a name across modules with **non-identical** semantics. This audit
determines whether either the wrap-into-no-wrap or no-wrap-into-wrap silent
binding actually fired in production.

## 1. Loader order — deterministic?

Two flatten/loader paths exist:

| Path | File | Order | Collision policy |
|---|---|---|---|
| Runtime / interp | `self/module_loader.hexa` | Iterative DFS, leaves-first emit (post-order). `g_order_parts` is deterministic given a fixed entry file + dep graph. | **None.** Strips `pub`, comments imports, but does NOT detect or rename duplicate top-level `fn` names. Multiple defs of same name coexist in concat output → interpreter binds **last definition wins** under late-binding semantics. |
| AOT flatten | `tool/flatten_imports.hexa` | Same iterative DFS, leaves-first; emit is **reverse-topo (root-first, leaves-last)**. | **Has collision detection** (`fl_scan_private_decls` + `fl_word_replace`). Non-pub collided names are mangled with file-prefix (e.g. `Writer__shell_escape`). So under the AOT path, the bug is fully neutralized. |

So the late-binding hazard exists **only on the interp / module_loader path**.
Within that path, order *is* deterministic per entry-point: a fixed graph
produces a fixed emission order.

## 2. Co-import reality check (the load-bearing finding)

A grep across all `*.hexa` for any file that imports `stdlib/cert/*` AND any
of `stdlib/http` / `stdlib/net/http_client` (direct or transitive) returns
**zero matches**:

```
git grep -lE 'use "(self/)?stdlib/cert' -- '*.hexa' \
  | xargs grep -lE 'use "(self/)?stdlib/(http|net/http_client)"'  # → empty
```

The cert module subgraph (`mod`, `loader`, `factor`, `reward`, `selftest`,
`writer`, `meta2_*`, `verifier`) is closed under itself; `stdlib/ckpt/verifier`
imports `stdlib/cert/meta2_validator` but no http. None of its callers reach
http_client/http either.

Conversely, the only entry-points that pull two of the three are:

| Entry | Imports | Effect |
|---|---|---|
| `stdlib/test/test_http.hexa` | `stdlib/http` only | single def, no collision |
| `stdlib/test/test_http_client.hexa` | `stdlib/net/http_client` only | single def, no collision |
| `stdlib/test/test_golden.hexa` | `stdlib/http` + (`stdlib/qrng_anu` → `stdlib/net/http_client`) | **both http defs co-exist, but they are byte-identical wrap-form**. Whichever wins, callers get the right semantic. |
| `stdlib/cert/test/cert_selftest.hexa` | cert subgraph only | no http loaded |

## 3. Which helper would have won pre-FU-7?

`module_loader` post-order means dependencies emit first, root file emits last.
For `test_golden.hexa`:
- `stdlib/qrng_anu` → `stdlib/net/http_client` (leaf) emits first.
- `stdlib/http` (independent) emits next (no shared deps).
- `test_golden.hexa` itself emits last.

Both `_shell_escape` defs have identical bodies (`return "'" + s.replace("'", "'\\''") + "'"`),
so the late-binding pick is **moot**. Resulting curl invocations were correct.

For any cert entry-point: only `_cert_shell_escape` (escape-only) is loaded,
no shadow exists, callers receive the right semantic.

## 4. Per-callsite damage assessment

| File:line | Helper called | Co-loaded with conflicting def? | Verdict |
|---|---|---|---|
| `stdlib/cert/writer.hexa:31` | `_cert_shell_escape` (escape-only) | No (cert subgraph never co-imports http) | **OK** — no shadow, correct semantic |
| `stdlib/cert/writer.hexa:34` | `_cert_shell_escape` (escape-only) | No | **OK** |
| `stdlib/http.hexa:98` | `_http_shell_escape` (wrap) | Co-loaded only with `_net_http_shell_escape` (byte-identical wrap) | **OK** — even if the other shadow won, semantic is the same |
| `stdlib/http.hexa:117` | `_http_shell_escape` (wrap) | same | **OK** |
| `stdlib/http.hexa:136` | `_http_shell_escape` (wrap) | same | **OK** |
| `stdlib/net/http_client.hexa:76` | `_net_http_shell_escape` (wrap) | same | **OK** |
| `stdlib/net/http_client.hexa:96` | `_net_http_shell_escape` (wrap) | same | **OK** |

Plus `self/stdlib/http.hexa` (a separate, un-renamed copy of `_shell_escape`
with WRAP semantics) — unaffected, single-def.

**No production callsite ever received the wrong semantic.**

## 5. Historical bug-report scan

`git log --grep` over `shell|escape|sha mismatch|cert sha|curl unquoted|deterministic_sha`
surfaces no incident matching the predicted failure modes:
- No "wrong cert SHA" tickets.
- No "curl URL parse error / shell-injection" tickets.
- The closest cert-side fix (`4b50e1b8` "stage0-compat digit check + selftest
  temp-file SHA") is unrelated — a stage0 substrate quirk in
  `meta2_validator._is_digit_ch` plus a different shasum-pipe pattern in
  selftest. No sign of a corrupted SHA payload reaching disk.

## 6. PCC fixture revalidation

The repo's only cert-related fixtures are loader-input format
(`stdlib/cert/_fixtures/cert_{a,b,c}.json`) — they have no
`deterministic_sha` field. **No writer-output PCC files exist on disk** to
revalidate; `stdlib/cert/writer.hexa` is itself ~1 month old (introduced in
`b6423a69`) and its output has never been persisted under version control.

This means: even if the bug HAD fired, it could only have corrupted
ephemeral runtime artifacts (`.meta2-cert` writes), never anything
captured in the repo.

## 7. Recommended action

**Latent but never triggered.** Specifically:

1. **No deployment audit needed.** No path co-imports cert + http into the
   same translation unit; the dangerous shadow could not occur on any
   reachable entry point.
2. **No PCC re-validation needed.** No on-disk writer outputs exist to
   re-validate.
3. **The merge of FU-7 is purely preventive** — it closes a future-risk
   window where a new module under `stdlib/cert/` might add an http import,
   or a deeper aggregator (e.g. a future `self/hexa_full.hexa` extension)
   might pull both subgraphs in. Without FU-7, any such future merge would
   have silently produced wrong cert SHAs or unquoted curl URLs.
4. **Keep `tool/private_fn_collision_lint.hexa`** (commit `c741d537`) wired
   into CI / pre-commit. It is the only safety net catching this class
   before it reaches a co-import that activates it.

## 8. Summary

The bug was **structurally real but operationally inert**. The graph topology
of the stdlib at the time of FU-7 happened to keep cert and http on disjoint
import islands; the only co-load (`test_golden`) involved two byte-identical
http variants whose late-binding pick is semantically identity. No artifact
on disk, no historical incident, and no current or near-historical caller
matches the predicted failure-mode fingerprint.

FU-7 stands as a correct preventive fix; no follow-up remediation required.

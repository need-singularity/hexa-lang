# RFC-010 (i) — docker usage audit (Stage 5 deprecation prep)

**Date:** 2026-05-06
**Goal:** macOS-without-docker as last line of defense. Inventory every docker
dependence, classify, and propose macOS-native replacement.

## Headline finding

`~/.hx/bin/hexa` was reduced to **6 lines** on 2026-05-06 — direct
`hexa.real` exec, no resolver routing, no docker fallback. The previously
~700-line wrapper with docker / hetzner / local routing is **gone from the
hot path** for `hexa <args>`. Most plumbing-level docker dependence on the
runtime side is therefore already deprecated; the remaining usage is in
**build/release tooling** (image builds, cross-compile sysroot setup) and a
few **migration helpers**.

```
$ wc -l ~/.hx/bin/hexa
       6 /Users/ghost/.hx/bin/hexa
$ cat ~/.hx/bin/hexa
#!/bin/bash
# 2026-05-06 macOS docker bypass cleanup — direct hexa.real exec.
# HEXA_SHIM_NO_DARWIN_LANDING=1 — hexa.real 자체의 darwin landing refusal 우회.
# Resolver/routing/verbose 메시지 모두 제거. docker fallback 미사용.
export HEXA_SHIM_NO_DARWIN_LANDING=1
exec "/Users/ghost/.hx/packages/hexa/hexa.real" "$@"
```

## Inventory by site

### A. `~/.hx/bin/hexa` wrapper — DONE (no action)

Direct `exec hexa.real` only. No docker exec / docker run. Setting
`HEXA_RESOLVER_NO_REROUTE=1` (still done by `wraith`, `nexus`, `hxq`,
`hexa-url-handler.sh`, `hive-resource`) is a vestigial NO-OP since the
wrapper no longer has a router.

### B. Build / release tooling — KEEP (Linux build target)

| File | docker usage | Replacement |
|---|---|---|
| `tool/build_runner_image.hexa` | `docker buildx build|create|use|inspect`, `which docker`, `docker info` | **KEEP** — multi-arch container image build for the deploy runner. Has no macOS-native equivalent (need OCI image). |
| `tool/build_interp_linux.hexa:297` | print of `docker run --rm --platform linux/...` | **KEEP** — README-style emit, optional Linux smoketest. |
| `tool/build_hexa_cli_linux.hexa:278` | print of `docker runner image (multi-arch):` | **KEEP** — release help text. |
| `tool/cross_compile_linux.hexa:15-17,277-279` | `docker create / cp / rm` for sysroot extraction | **KEEP**, but make optional — sysroot can be checked-in or downloaded as tarball; docker is one supported path. |

Action: none for runtime. Builders run on demand by maintainers.

### C. Migration / sync helpers — REPLACE

| File | docker usage | Replacement |
|---|---|---|
| `tool/anima_docker_sync.hexa` | docker container sync between hosts | **REPLACE** — rsync over ssh; docker is one transport. Defer to anima-side migration. |
| `tool/workload_router.hexa` | emits `docker exec` command lines | **REWRITE** — emit local `setsid + ulimit + timeout` envelope instead. Aligns with envelope plan in RFC-010. |
| `tool/resource_scorer.hexa` | "TS only orchestrates docker exec" comment | **OBSOLETE** — TS dispatcher routing layer already deprecated by mac-bare landing (commits acf4e5e3 / bafedbcd / 788456e5 in May 2026). |
| `tool/runner_status.hexa:33` | references `docker buildx build … --tag hexa-runner:latest` | **DOC-ONLY** comment — keep for build path. |

### D. launchd plists (mac runtime) — DONE

| Plist | docker usage |
|---|---|
| `dev.hexa-lang.hexa-runtime-sweeper` | matches `docker` in process search (sweep target) — **OBSOLETE** once docker daemon stops. |
| `dev.hexa-lang.hexa-runtime-runaway-watcher` | same. |
| `dev.hexa-lang.banned-ext-watcher` | (already disabled 2026-05-06; see `.disabled.20260506` rename) |

Sweepers can be downgraded to no-op once container processes stop existing.

### E. stdlib / self/ — REPLACE refs

| File | usage | Replacement |
|---|---|---|
| `self/module_loader.hexa` | textual reference to docker | doc only |
| `self/native/native_gate.c` | docker-specific gate | review — runtime gate; verify Mac-native path covered |
| `self/stdlib/raw_toggle.hexa` | docker reference | doc only |
| `tool/private_fn_collision_lint.hexa` | docker mention in comment | none |

## Removal priority (top 5)

1. **`tool/workload_router.hexa`** — rewrite to emit envelope (`setsid +
   ulimit + timeout`) instead of `docker exec`. Makes Mac-native landing
   the default.
2. **`tool/anima_docker_sync.hexa`** — replace with rsync transport.
3. **`tool/resource_scorer.hexa`** — delete docker-exec-orchestration code path
   since TS routing is already deprecated.
4. **launchd sweepers** — once a week of post-disable runs without re-emerging
   docker procs, downgrade their pattern matches to be no-ops.
5. **wrapper-side `HEXA_RESOLVER_NO_REROUTE=1` exports** in `wraith / nexus /
   hxq / hexa-url-handler.sh / hive-resource` — purely vestigial; remove next
   pass for cleanliness (no behavior change since wrapper now ignores it).

## Items to keep (Linux build target)

- `tool/build_runner_image.hexa` — multi-arch image build. Required for
  Linux deploy, runs on demand on a maintainer machine that has docker
  installed. Absence on macOS does not prevent local hexa execution.
- Linux cross-compile bootstrap (`tool/cross_compile_linux.hexa`) — sysroot
  extraction is one-time setup; docker is one path among several.

## Conclusion

The runtime hot path no longer touches docker. Stage 5 deprecation reduces
to **rewriting workload_router + anima_docker_sync + resource_scorer**
(items 1–3 above). Build/release tooling remains docker-dependent by design
because it produces Linux container artifacts — no replacement is needed
unless the project drops Linux deploy target.

## Memory pointer

`/Users/ghost/.claude/projects/-Users-ghost-core-hexa-lang/memory/`
should record (after this RFC's L_5 closure):
> `~/.hx/bin/hexa` is a direct-exec wrapper as of 2026-05-06 — no router.
> docker remains as build-tool dependency only.

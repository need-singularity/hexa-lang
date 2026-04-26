# syntax=docker/dockerfile:1.7
# ─────────────────────────────────────────────────────────────────
# hexa-runner — minimal Linux container that hosts the static
# musl-built hexa CLI (self/main.hexa → build/hexa_linux_<arch>).
#
# Background: 2026-04-25 Mac jetsam SIGKILL incident.  Hive policy
# now forbids bare `hexa` execution on the macOS host and routes
# every workload through a Linux container with hard CPU/RAM caps.
# This image is the single landing pad.
#
# Build (multi-arch via buildx):
#   docker buildx build \
#     --platform linux/arm64,linux/amd64 \
#     -f docker/runner/Dockerfile \
#     -t hexa-runner:<shortsha> -t hexa-runner:latest .
#
# Both pre-built static ELFs must exist beneath build/ before the
# image is assembled — see tool/build_runner_image.hexa.
# ─────────────────────────────────────────────────────────────────

# ─────────────────────────────────────────────────────────────────
# Stage 0: arch-token bridge.  Our build artefacts use the tokens
# `arm64` / `x86_64` (matching the host `uname -m` lineage), but
# buildx injects TARGETARCH as `arm64` / `amd64`.  We use two tiny
# scratch stages — one per docker-arch — that COPY only the correct
# pair of ELFs into a normalized layout (/cli + /interp).  The
# final stage then `COPY --from=elf-${TARGETARCH}` pulls just the
# right pair, so wrong-arch ELFs never land as layer bytes in the
# shipped image (saves ~2.3MB per arch — see ω-runner-1).
# ─────────────────────────────────────────────────────────────────
FROM scratch AS elf-arm64
COPY build/hexa_linux_arm64        /cli
COPY build/hexa_interp_linux_arm64 /interp

FROM scratch AS elf-amd64
COPY build/hexa_linux_x86_64        /cli
COPY build/hexa_interp_linux_x86_64 /interp

# Indirection: FROM-with-ARG-interpolation IS supported (buildkit
# resolves it at graph build time), whereas `COPY --from=elf-${TARGETARCH}`
# is NOT (the ARG isn't visible at COPY-stage-name parse).  So we
# alias the matching arch stage as `elf-selected` here, and the
# runtime stage below references the alias unconditionally.
FROM elf-${TARGETARCH} AS elf-selected

# ─────────────────────────────────────────────────────────────────
# Stage 1: final runtime image.
# ─────────────────────────────────────────────────────────────────
FROM debian:12-slim

# TARGETARCH is auto-injected by buildx ("arm64" or "amd64").  Re-export
# as ARG inside this stage so any subsequent ARG-driven step can see it.
ARG TARGETARCH

LABEL org.opencontainers.image.title="hexa-runner" \
      org.opencontainers.image.description="Static musl hexa CLI on Debian 12 slim" \
      org.opencontainers.image.source="https://github.com/dancinlife/hexa-lang" \
      org.opencontainers.image.licenses="MIT"

# Minimal apt layer:
#   ca-certificates : TLS for fetch tools.
#   tini            : reaping-aware init for PID 1.  cmd_run() forks a
#                     `( watcher ) &` subshell that uses [ $PPID -eq 1 ]
#                     to detect orphaning; without proper init the
#                     orphan-of-PID-1 detection fires prematurely and
#                     SIGKILL's the workload (exit 137) before output
#                     flushes.  See 2026-04-25 hexa-runner triage.
#   curl, wget      : required by nexus bridges (oeis/gw/arxiv/simbad/
#                     wikipedia/openalex/uniprot) which exec("curl …") for
#                     HTTPS API fetches.  Absent → 7/16 bridge health FAIL
#                     with `sh: 1: curl: not found` (2026-04-26 bridge
#                     health check).  raw 73 admissibility: minimum-add
#                     only, no feature creep.
#
# perl (REMOVED 2026-04-26 ω-runner-2):  Previously installed solely to
# provide /usr/bin/shasum for main.hexa AOT cache + source fingerprinting
# (~50MB layer cost: perl + libperl5.36 + perl-modules-5.36).  self/main.hexa
# source_key() and src_fingerprint_deep() now prefer `sha256sum` (coreutils,
# present in debian:12-slim base) and fall back to `shasum -a 256` only when
# sha256sum is missing — so the macOS host build retains its perl-shasum
# code path while the Linux runner uses the coreutils path that's already
# in the base image.  See state/omega_runner_2_perl_removal.json.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        ca-certificates tini curl wget \
 && rm -rf /var/lib/apt/lists/*

# Stage in just the matching arch's pair of ELFs from the per-arch
# scratch bridge stage above.  buildx resolves `${TARGETARCH}` →
# `arm64`|`amd64` per platform, so only the correct pair lands in this
# image — no wrong-arch waste (~2.3MB saved per arch vs. the previous
# COPY-both-then-rm-wrong-arch pattern).  See ω-runner-1 image opt.
#
# The interp ELF is installed at /usr/local/bin/build/hexa_interp — the
# canonical post-rename target that resolve_interp() in self/main.hexa
# searches for via argv[0]-derived install_dir + "/build/hexa_interp".
# With /usr/local/bin/hexa as argv[0], the sibling build/ dir is the
# canonical location.
COPY --from=elf-selected /cli    /tmp/hexa_cli
COPY --from=elf-selected /interp /tmp/hexa_interp

RUN set -eux; \
    mkdir -p /usr/local/bin/build; \
    install -m 0755 /tmp/hexa_cli    /usr/local/bin/hexa; \
    install -m 0755 /tmp/hexa_interp /usr/local/bin/build/hexa_interp; \
    rm -f /tmp/hexa_cli /tmp/hexa_interp; \
    /usr/local/bin/hexa --version || true; \
    /usr/local/bin/build/hexa_interp --help >/dev/null 2>&1 || true

# Bake module_loader + stdlib so containers WITHOUT a $HEXA_LANG bind-mount
# (cron, launchd-in-container, isolated CI runners) can still resolve
# `use "stdlib/X"` and `use "self/stdlib/X"` imports loud-fail-correctly.
# Layout matches resolve_module_loader()'s argv0-relative probe (self/main.hexa
# line 778-794): install_dir = dirname(argv[0]) = /usr/local/bin, so
# /usr/local/bin/self/module_loader.hexa is auto-discovered without env.
# HEXA_STDLIB_ROOT is the env-hygiene-friendly fallback (module_loader.hexa
# lines 133-153, 173-183) — set explicitly so cron/launchd contexts that
# inherit no env still resolve stdlib paths.
COPY self/module_loader.hexa /usr/local/bin/self/module_loader.hexa
COPY self/stdlib              /usr/local/bin/self/stdlib
ENV HEXA_STDLIB_ROOT=/usr/local/bin/self/stdlib

# Default workdir for user-mounted .hexa source.
WORKDIR /work

# ─────────────────────────────────────────────────────────────────
# Optional: bind-mount nexus (or other sister repos) at runtime so
# gate/entry.hexa, gate/prompt_scan.hexa, gate/cmd_gate.hexa can
# resolve cross-repo dispatch (HARNESS = NEXUS_HOME + "/tool",
# HEXA = NEXUS_HOME + "/bin/hexa").
#
#   docker run --rm \
#     --platform linux/arm64 \
#     --cpus=4 --memory=4g \
#     -v "$HOME/core/nexus:/nexus:ro" \
#     -e NEXUS_HOME=/nexus \
#     -v /tmp/foo.hexa:/work/foo.hexa:ro \
#     -w /work \
#     hexa-runner:latest run /work/foo.hexa
#
# When /nexus is absent, gate/entry.hexa degrades gracefully:
#   1) NEXUS_HOME env (explicit override; default /nexus when WS+HOME unset)
#   2) WS + "/nexus"
#   3) HOME + "/core/nexus"
#   4) "/nexus"
# Each dispatch site checks file_exists(NEXUS_HEXA) and silently
# skips when the binary isn't present (no `sh: hexa: not found`
# noise). The hive resolver (~/.hx/bin/hexa) should add the -v
# flag and -e NEXUS_HOME to its `docker exec` invocation when
# nexus-dependent commands (omega/drill/canon/etc) are issued.
# ─────────────────────────────────────────────────────────────────

# Use tini as PID 1 so the dispatcher's watchdog subshells are reaped
# correctly.  Without an init-aware PID 1 the `( watcher ) &` orphan
# detection in cmd_run() fires and SIGKILL's the workload before its
# output can flush — see the apt comment block above.
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/hexa"]
CMD ["help"]

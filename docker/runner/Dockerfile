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
FROM debian:12-slim

# TARGETARCH is auto-injected by buildx ("arm64" or "amd64").
# We re-export it as ARG so the COPY step can branch on it.
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

# Stage all four binaries (CLI dispatcher + interp interpreter, ×2 arches)
# into /tmp; the conditional move below picks the right pair based on
# TARGETARCH.  Buildx will only ever invoke the COPY for the platform it
# is currently building, but we keep both paths explicit for clarity /
# single-arch fallback.
#
# The interp ELF is installed at /usr/local/bin/build/hexa_interp — the
# canonical post-rename target that resolve_interp() in self/main.hexa
# searches for via argv[0]-derived install_dir + "/build/hexa_interp".
# With /usr/local/bin/hexa as argv[0], the sibling build/ dir is the
# canonical location.
COPY build/hexa_linux_arm64           /tmp/hexa_linux_arm64
COPY build/hexa_linux_x86_64          /tmp/hexa_linux_x86_64
COPY build/hexa_interp_linux_arm64    /tmp/hexa_interp_linux_arm64
COPY build/hexa_interp_linux_x86_64   /tmp/hexa_interp_linux_x86_64

RUN set -eux; \
    mkdir -p /usr/local/bin/build; \
    case "${TARGETARCH}" in \
      arm64) install -m 0755 /tmp/hexa_linux_arm64        /usr/local/bin/hexa; \
             install -m 0755 /tmp/hexa_interp_linux_arm64 /usr/local/bin/build/hexa_interp ;; \
      amd64) install -m 0755 /tmp/hexa_linux_x86_64        /usr/local/bin/hexa; \
             install -m 0755 /tmp/hexa_interp_linux_x86_64 /usr/local/bin/build/hexa_interp ;; \
      *)     echo "unsupported TARGETARCH=${TARGETARCH}" >&2; exit 1 ;; \
    esac; \
    rm -f /tmp/hexa_linux_arm64 /tmp/hexa_linux_x86_64 \
          /tmp/hexa_interp_linux_arm64 /tmp/hexa_interp_linux_x86_64; \
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

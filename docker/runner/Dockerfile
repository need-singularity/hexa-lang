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

# Minimal apt layer: ca-certificates only (TLS for fetch tools).
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Stage both binaries into /tmp; the conditional move below picks
# the right one based on TARGETARCH.  Buildx will only ever invoke
# the COPY for the platform it is currently building, but we keep
# both paths explicit for clarity / single-arch fallback.
COPY build/hexa_linux_arm64  /tmp/hexa_linux_arm64
COPY build/hexa_linux_x86_64 /tmp/hexa_linux_x86_64

RUN set -eux; \
    case "${TARGETARCH}" in \
      arm64) install -m 0755 /tmp/hexa_linux_arm64  /usr/local/bin/hexa ;; \
      amd64) install -m 0755 /tmp/hexa_linux_x86_64 /usr/local/bin/hexa ;; \
      *)     echo "unsupported TARGETARCH=${TARGETARCH}" >&2; exit 1 ;; \
    esac; \
    rm -f /tmp/hexa_linux_arm64 /tmp/hexa_linux_x86_64; \
    /usr/local/bin/hexa --version || true

# Default workdir for user-mounted .hexa source.
WORKDIR /work

ENTRYPOINT ["/usr/local/bin/hexa"]
CMD ["help"]

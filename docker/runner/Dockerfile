# syntax=docker/dockerfile:1.7
# ─────────────────────────────────────────────────────────────────
# hexa-runner DISTROLESS POC (ω-runner-4, 2026-04-26)
#
# Goal: shrink runtime image from ~166MB → ~91MB virtual by switching
# the runtime base from debian:12-slim → gcr.io/distroless/cc-debian12.
# Distroless ships libc + libssl (cc variant) but no shell / apt / tini /
# busybox. We must:
#   1) Build apt-installed binaries (tini, curl, wget) in a debian:12-slim
#      builder stage, then COPY just the binaries + their dynamic libs +
#      ca-cert bundle into the distroless runtime stage.
#   2) Mirror the production multi-arch ELF layout (per-arch scratch
#      bridge → final stage) so wrong-arch ELFs never land.
#   3) Keep the same ENTRYPOINT shape (/usr/bin/tini -- /usr/local/bin/hexa)
#      so the orphan-reaper watchdog still works (PPID==1 detection).
#
# DO NOT replace the production Dockerfile from this file. POC only.
# Cutover (if smoke passes) happens in a separate ω-runner-5 cycle.
# ─────────────────────────────────────────────────────────────────

# ─────────────────────────────────────────────────────────────────
# Stage 0: arch-token bridge (identical to production).
# ─────────────────────────────────────────────────────────────────
FROM scratch AS elf-arm64
COPY build/hexa_linux_arm64        /cli
COPY build/hexa_interp_linux_arm64 /interp

FROM scratch AS elf-amd64
COPY build/hexa_linux_x86_64        /cli
COPY build/hexa_interp_linux_x86_64 /interp

FROM elf-${TARGETARCH} AS elf-selected

# ─────────────────────────────────────────────────────────────────
# Stage 1: builder.  Has apt + shell.  We install the same packages
# the production image gets, then in the runtime stage we cherry-pick
# just the binaries + their .so dependencies.
# ─────────────────────────────────────────────────────────────────
FROM debian:12-slim AS builder
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        ca-certificates tini curl wget busybox-static \
        gcc libc6-dev gnupg \
 && rm -rf /var/lib/apt/lists/*
# ω-runner-8 (2026-04-28): node 20 added to unblock B3 P0 (F-MX-3-c).
# The hive-cli child process (`node /Users/ghost/.../hive-cli`) needs a node
# binary inside the container; without it subagent_dispatch.hexa hits
# `container-no-node` rc=76 and the entire kick infra fails. We install
# nodejs in the builder stage where apt is available, then COPY just the
# /usr/bin/node binary + transitive deps into the distroless runtime stage.
# nodejs deb installs node at /usr/bin/node and depends only on libc/libstdc++
# (which distroless/cc-debian12 already ships), so the COPY footprint is
# small (~80 MB statically-linked node + minimal deps).
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
 && apt-get install -y --no-install-recommends nodejs \
 && rm -rf /var/lib/apt/lists/* \
 && node --version \
 && npm --version \
 && which node \
 && ls -la /usr/bin/node
# ω-runner-7 (2026-04-26): gcc + libc6-dev added so the builder stage can
# compile self/native/native_gate.c → /opt/hexa/native_gate.so.  The
# distroless runtime stage then COPY's just the .so (no toolchain bloat
# in the shipped image) and ENV LD_PRELOAD activates it for every
# containerized hexa invocation.  This is the actual enforcement layer
# for raw#7 F5/F6 + raw#6 F4 + raw#8 file-naming + raw#13 git-hook-ban
# on Linux — macOS host stays advisory due to SIP per Mac→docker
# hard-landing policy 2026-04-25.

# ω-runner-5 (2026-04-26): unblock B3 (no GNU coreutils on distroless).
# busybox-static (~1 MB) ships dirname/basename/printf/test/cat/awk/grep/sed/
# date/sh/ln/mkdir/rm/ls/cp/mv/cut/tr/head/tail/wc/chmod/etc as a single
# self-contained ELF + applet symlinks.  The hexa CLI's exec()/popen("sh -c")
# paths (resolve_interp, etc.) need these.  Stage applet symlinks here
# (in the builder where /bin/sh exists) so the runtime stage just COPYs
# the resulting symlink farm.  Use a dedicated /opt/bb staging dir to
# avoid colliding with debian's own /usr/bin contents.
RUN mkdir -p /opt/bb/bin /opt/bb/usr/bin \
 && cp /bin/busybox /opt/bb/bin/busybox \
 && for applet in sh dirname basename printf test cat awk grep sed date \
                  ln mkdir rm ls cp mv cut tr head tail wc chmod \
                  env echo true false sleep find xargs sort uniq tee \
                  which mktemp readlink dd stat touch expr; do \
        ln -sf /bin/busybox /opt/bb/usr/bin/$applet; \
    done \
 && ln -sf /bin/busybox /opt/bb/bin/sh \
 && ln -sf /bin/busybox /opt/bb/bin/sleep
# ω-runner-6 (2026-04-26): /bin/sleep symlink required by ~/.hx/bin/hexa
# resolver — `_ensure_hexa_exec_container` invokes `docker run … --entrypoint
# /bin/sleep "$DOCKER_IMAGE" infinity` to keep the container long-lived.
# busybox's sleep applet otherwise only lands at /usr/bin/sleep, which makes
# the resolver-spawned container exit "Created" (no-op rather than running).

# ω-runner-6 (2026-04-26): multi-arch lib staging for the production cutover.
# The runtime stage needs curl/wget transitive .so deps under either
# /usr/lib/aarch64-linux-gnu/ (arm64) or /usr/lib/x86_64-linux-gnu/ (amd64).
# COPY --from cannot use shell vars/globs across architectures, so we stage
# the right arch's libs into a generic /opt/runtime-libs/ here in the
# builder (which IS arch-specific per buildx platform), preserving the arch
# subdir so the runtime stage can do a single fixed-path COPY for each
# arch (with on-disk overlap being fine — only one arch's COPY actually
# matches files at build time per platform).
RUN set -eux; \
    arch_dir="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null \
                || gcc -print-multiarch 2>/dev/null \
                || (uname -m | sed 's|aarch64|aarch64-linux-gnu|;s|x86_64|x86_64-linux-gnu|'))"; \
    echo "staging libs for arch_dir=$arch_dir"; \
    mkdir -p /opt/runtime-libs/aarch64-linux-gnu /opt/runtime-libs/x86_64-linux-gnu; \
    dest="/opt/runtime-libs/$arch_dir"; \
    for lib in libcurl.so.4 libnghttp2.so.14 libidn2.so.0 librtmp.so.1 \
               libssh2.so.1 libpsl.so.5 libgssapi_krb5.so.2 libkrb5.so.3 \
               libk5crypto.so.3 libcom_err.so.2 libkrb5support.so.0 \
               libldap-2.5.so.0 liblber-2.5.so.0 libsasl2.so.2 \
               libgnutls.so.30 libhogweed.so.6 libnettle.so.8 libgmp.so.10 \
               libp11-kit.so.0 libtasn1.so.6 libffi.so.8 libunistring.so.2 \
               libkeyutils.so.1 libgcrypt.so.20 libgpg-error.so.0 \
               libzstd.so.1 libbrotlidec.so.1 libbrotlicommon.so.1; do \
        for src in /usr/lib/$arch_dir/$lib*; do \
            [ -e "$src" ] && cp -a "$src" "$dest/" || true; \
        done; \
    done; \
    for lib in libz.so.1 libpcre2-8.so.0 libuuid.so.1; do \
        for src in /lib/$arch_dir/$lib* /usr/lib/$arch_dir/$lib*; do \
            [ -e "$src" ] && cp -a "$src" "$dest/" || true; \
        done; \
    done; \
    ls /opt/runtime-libs/aarch64-linux-gnu/ | wc -l; \
    ls /opt/runtime-libs/x86_64-linux-gnu/ | wc -l

# ω-runner-7 (2026-04-26): compile native_gate.so for LD_PRELOAD enforce.
# The builder stage runs per buildx platform (arm64 / amd64), so the
# resulting .so naturally matches TARGETARCH — no cross-compile gymnastics.
# Build flags mirror the macOS .dylib path documented in native_gate.c
# (cc -shared -fPIC -O2 -D_GNU_SOURCE … -ldl).  -ldl is required for
# dlsym(RTLD_NEXT) lookups of the libc syscall wrappers we shim
# (open/openat/creat/rename/renameat/renameat2).
COPY self/native/native_gate.c /opt/hexa/native_gate.c
RUN set -eux; \
    mkdir -p /opt/hexa; \
    cc -shared -fPIC -O2 -D_GNU_SOURCE \
       -o /opt/hexa/native_gate.so \
       /opt/hexa/native_gate.c \
       -ldl; \
    ls -la /opt/hexa/native_gate.so; \
    test -s /opt/hexa/native_gate.so

# ω-runner-8 (2026-04-28): build npm/npx wrapper scripts for runtime stage.
# Why a wrapper instead of a symlink: docker buildkit's `COPY --from=builder`
# DEREFERENCES symlinks across stages (turns them into regular files), which
# breaks the relative `require('../lib/cli.js')` resolution baked into the
# upstream npm-cli.js stub.  A small sh wrapper that invokes node with the
# canonical absolute path of the cli script side-steps the issue entirely:
# node receives `/usr/lib/node_modules/npm/bin/npm-cli.js` as argv[1], so
# __dirname inside that script is `/usr/lib/node_modules/npm/bin`, and
# `require('../lib/cli.js')` resolves correctly to the bundled npm sources.
# /bin/sh in runtime is busybox (staged earlier), so the wrapper runs there.
RUN mkdir -p /opt/npm-bin \
 && printf '#!/bin/sh\nexec /usr/bin/node /usr/lib/node_modules/npm/bin/npm-cli.js "$@"\n' > /opt/npm-bin/npm \
 && printf '#!/bin/sh\nexec /usr/bin/node /usr/lib/node_modules/npm/bin/npx-cli.js "$@"\n' > /opt/npm-bin/npx \
 && chmod +x /opt/npm-bin/npm /opt/npm-bin/npx \
 && ls -la /opt/npm-bin/ \
 && cat /opt/npm-bin/npm

# ─────────────────────────────────────────────────────────────────
# Stage 2: runtime.  gcr.io/distroless/cc-debian12 ships libc + libgcc +
# libssl + libcrypto (cc variant), so curl/wget should mostly Just Work
# once we COPY their binaries + a few transitive libs (libcurl4, libnghttp2,
# libpsl, libidn2, libldap, etc.).  Architecture-specific lib paths
# (/usr/lib/aarch64-linux-gnu vs /usr/lib/x86_64-linux-gnu) are handled
# by COPY-ing the whole arch dir wholesale — distroless ships an empty
# version of these dirs, so the COPY just fills them in.
# ─────────────────────────────────────────────────────────────────
FROM gcr.io/distroless/cc-debian12

ARG TARGETARCH

LABEL org.opencontainers.image.title="hexa-runner-distroless" \
      org.opencontainers.image.description="POC: hexa CLI on distroless/cc-debian12" \
      org.opencontainers.image.source="https://github.com/need-singularity/hexa-lang" \
      org.opencontainers.image.licenses="MIT"

# ω-runner-5 (2026-04-26): busybox-static unblocks B3.
# Round-3 used /bin/dash (just sh, no coreutils) — smoke (e) BLOCKED because
# `hexa run` shells out via popen("sh -c …") and needs dirname/basename/etc.
# busybox-static is a single ~1 MB ELF that provides all common applets via
# argv[0] dispatch.  Builder stage already staged /opt/bb/{bin,usr/bin} with
# the busybox binary + applet symlinks.  Two wholesale COPYs land them at
# /bin/* and /usr/bin/* in the runtime, including /bin/sh -> /bin/busybox.
COPY --from=builder /opt/bb/bin/     /bin/
COPY --from=builder /opt/bb/usr/bin/ /usr/bin/

# tini for PID-1 reaping.
COPY --from=builder /usr/bin/tini /usr/bin/tini

# curl + wget binaries (used by nexus bridges).
COPY --from=builder /usr/bin/curl /usr/bin/curl
COPY --from=builder /usr/bin/wget /usr/bin/wget

# CA cert bundle (TLS verification).
COPY --from=builder /etc/ssl/certs              /etc/ssl/certs
COPY --from=builder /usr/share/ca-certificates  /usr/share/ca-certificates
COPY --from=builder /etc/ca-certificates.conf   /etc/ca-certificates.conf

# Transitive .so dependencies of curl/wget.  Distroless/cc ships libc,
# libssl, libcrypto, libgcc, libstdc++ — we add the rest.
#
# ω-runner-6 (2026-04-26): builder stage stages the per-arch .so deps into
# /opt/runtime-libs/<arch_dir>/ (a fixed two-arch tree where only the
# matching arch's subdir is populated per buildx platform pass).  Two
# wholesale COPYs land them at /usr/lib/<arch>/ in the runtime — the
# arch that doesn't match is empty, so the COPY is a no-op for it.
COPY --from=builder /opt/runtime-libs/aarch64-linux-gnu/ /usr/lib/aarch64-linux-gnu/
COPY --from=builder /opt/runtime-libs/x86_64-linux-gnu/  /usr/lib/x86_64-linux-gnu/

# ELFs from the arch-bridge stage.  Same install layout as production.
COPY --from=elf-selected /cli    /tmp/hexa_cli
COPY --from=elf-selected /interp /tmp/hexa_interp

# Distroless has no shell and no `install` / `mkdir` / `rm`.  But the
# hexa CLI is a static musl ELF — we don't need to chmod it (COPY
# preserves +x from the source file's mode bits).  Place it directly:
COPY --from=elf-selected /cli    /usr/local/bin/hexa
COPY --from=elf-selected /interp /usr/local/bin/build/hexa_interp

# Bake module_loader + stdlib for env-less stdlib resolution.
COPY self/module_loader.hexa /usr/local/bin/self/module_loader.hexa
COPY self/stdlib              /usr/local/bin/self/stdlib
ENV HEXA_STDLIB_ROOT=/usr/local/bin/self/stdlib

# ω-runner-7 (2026-04-26): native_gate.so + LD_PRELOAD (raw#7 OS enforce).
# The .so was compiled in the builder stage from self/native/native_gate.c
# matching this platform's TARGETARCH.  ENV LD_PRELOAD makes every process
# spawned in the container — hexa CLI, tini-wrapped child, popen("sh -c …")
# subshells via busybox — inherit the shim.  EPERM is returned at the VFS
# syscall boundary for raw#7 F5/F6 + raw#6 F4 + raw#8 file-naming +
# raw#13 git-hook-ban deny rules before any byte hits disk.  Per
# Mac→docker hard-landing policy 2026-04-25 this is the *actual* enforcer
# on darwin hosts (macOS SIP blocks DYLD_INSERT_LIBRARIES on signed bins,
# so the host shim is advisory; Linux containers carry the real teeth).
# Opt-out (emergency only): docker run -e CLAUDX_NO_SANDBOX=1 ...
#
# LIMITATION: LD_PRELOAD applies to *dynamically-linked* child processes only.
# The hexa CLI itself ships as a static musl ELF (see Stage 0 above —
# build/hexa_linux_{arm64,x86_64} report `statically linked, stripped`), so
# its direct file IO (raw_loader, codegen output) bypasses this shim.  hexa's
# own write paths are covered by L0 agent-layer pre_tool_guard and the CLI's
# in-process raw_loader assertions; this LD_PRELOAD layer is L1 — the dynamic
# child enforcer (curl/wget/busybox sh).  Full ladder:
# doc/security/os-level-enforcement-limits.md §11.
COPY --from=builder /opt/hexa/native_gate.so /opt/hexa/native_gate.so
ENV LD_PRELOAD=/opt/hexa/native_gate.so

# ω-runner-8 (2026-04-28): node 20 binary + npm for hive-cli child process.
# Unblocks B3 P0 (F-MX-3-c) container-no-node rc=76 fault that's been
# repeating for 6 cycles. The nodesource deb installs:
#   /usr/bin/node              (~99 MB v8 runtime, dyn-linked against
#                               libstdc++/libgcc_s/libc — all in distroless/cc)
#   /usr/lib/node_modules/npm  (~10 MB pure-JS npm CLI)
#   /usr/bin/npm + /usr/bin/npx (symlinks → ../lib/node_modules/npm/bin/*)
# No /usr/share/nodejs in the deb (probed 2026-04-28 from builder stage).
# npm/npx are symlinks: `COPY` follows them so the runtime gets the actual
# JS file, not the dangling symlink — we recreate the symlinks at runtime.
COPY --from=builder /usr/bin/node /usr/bin/node
COPY --from=builder /usr/lib/node_modules /usr/lib/node_modules
# npm/npx in /usr/bin are originally symlinks (../lib/node_modules/npm/bin/*-cli.js).
# The cli.js scripts do `require('../lib/cli.js')`, which only resolves
# correctly when the script is invoked via a symlink whose target is the
# real bin dir (node resolves symlinks before computing __dirname for
# require()).  Builder stage already staged /opt/npm-bin/{npm,npx} as
# symlinks pointing at /usr/lib/node_modules/npm/bin/*-cli.js — COPY those
# symlinks into /usr/bin/ here.
COPY --from=builder /opt/npm-bin/npm /usr/bin/npm
COPY --from=builder /opt/npm-bin/npx /usr/bin/npx

WORKDIR /work

# Same ENTRYPOINT shape as production: tini -> hexa.
ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/hexa"]
CMD ["help"]

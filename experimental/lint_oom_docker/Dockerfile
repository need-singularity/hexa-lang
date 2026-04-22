# hexa-lang/experimental/lint_oom_docker/Dockerfile
#
# Purpose: isolated Linux container to reproduce the lint.hexa OOM bug
#          without risking the host Mac (24 GB RAM, previously frozen
#          multiple times by hexa_stage0.real zombies at 40–53 GB each).
#
# Strategy:
#   1. Build stage: compile hexa_bootstrap (C-based hexa→C transpiler)
#   2. Runtime stage: hexa_bootstrap + gcc, repo mounted as read-only volume
#   3. docker run with --memory=2g --memory-swap=2g → hard kill at 2 GB
#
# Build context expected: /Users/ghost/core/hexa-lang (repo root)
#   docker build -t hexa-lint-oom -f experimental/lint_oom_docker/Dockerfile .
#
# Known limitation: bootstrap_compiler.c is a subset of the full interpreter
# (hexa_stage0.real / hexa_full.hexa). Not every lint.hexa feature may work.
# Iterate — this is experimental scaffolding.

FROM debian:12-slim AS bootstrap

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY self/bootstrap_compiler.c self/runtime.c ./
RUN gcc -O2 -o /hexa_bootstrap bootstrap_compiler.c


FROM debian:12-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev \
        bash coreutils findutils grep gawk sed git curl ca-certificates \
        procps time \
    && rm -rf /var/lib/apt/lists/*

COPY --from=bootstrap /hexa_bootstrap /usr/local/bin/hexa_bootstrap

# Repo is mounted at /repo (read-only) at runtime — no COPY . here so the
# image stays stable across lint.hexa edits.
ENV HEXA_LANG=/repo
WORKDIR /repo

CMD ["/bin/bash"]

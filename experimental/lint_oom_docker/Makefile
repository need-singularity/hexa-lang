# experimental/lint_oom_docker/Makefile
#
# Isolated Linux container to reproduce the lint.hexa OOM bug without
# risking the host Mac.  Container memory is hard-capped at 2 GB, so OOM
# is contained.
#
# Prereqs:  Docker daemon running (Docker Desktop / Colima / OrbStack)
#
# Quick start:
#   make build          # build the image (gcc + hexa_bootstrap)
#   make smoke          # end-to-end "hello world" through the toolchain
#   make shell          # drop into an interactive 2 GB container
#   make compile-lint   # transpile lint.hexa -> lint.c via hexa_bootstrap
#   make clean          # remove the image

IMAGE       := hexa-lint-oom:latest
# Repo root (two levels up from this directory)
REPO_ROOT   := $(shell cd ../.. && pwd)

# 2 GB hard cap, no swap, read-only repo mount, writable tmpfs for artifacts.
# DOCKER_FLAGS is reused for interactive (shell) and non-interactive targets.
DOCKER_FLAGS := --rm --memory=2g --memory-swap=2g \
                -v $(REPO_ROOT):/repo:ro \
                --tmpfs /work:rw,size=512m \
                -w /work
DOCKER_RUN  := docker run $(DOCKER_FLAGS) $(IMAGE)

.PHONY: build shell smoke compile-lint clean

build:
	cd $(REPO_ROOT) && docker build -t $(IMAGE) -f experimental/lint_oom_docker/Dockerfile .

shell:
	docker run -it $(DOCKER_FLAGS) $(IMAGE) /bin/bash

# Try bootstrap transpile; will fail if bootstrap_compiler.c doesn't cover
# every feature lint.hexa uses — that's expected on first attempt, iterate.
# hexa_bootstrap generates <out>.c (+.s) then invokes gcc internally, which
# needs runtime.c in cwd — hence the cp from the read-only /repo mount.
compile-lint:
	$(DOCKER_RUN) /bin/bash -c \
	  'set -eux; \
	   cp /repo/self/runtime.c /work/runtime.c && \
	   cd /work && \
	   hexa_bootstrap /repo/gate/lint.hexa -o /work/lint 2>&1 | tail -40; \
	   echo "---artifacts---"; \
	   ls -la /work/'

# Smoke test: trivial hexa "hello world" end-to-end through the toolchain.
# If this fails, the image itself is broken; if it passes but compile-lint
# fails, the gap is specific to lint.hexa features.
smoke:
	$(DOCKER_RUN) /bin/bash -c \
	  'set -eux; \
	   cp /repo/self/runtime.c /work/runtime.c && \
	   echo "fn main() { println(\"hello from container\") }" > /work/tiny.hexa && \
	   cd /work && \
	   hexa_bootstrap /work/tiny.hexa -o /work/tiny 2>&1 | tail -20; \
	   echo "---running tiny---"; \
	   /work/tiny || echo "(run failed, exit $$?)"'

clean:
	docker rmi $(IMAGE) || true

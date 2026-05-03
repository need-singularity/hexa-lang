#!/usr/bin/env bash
# tool/_version_header_seed.sh — one-shot helper that injected `@version` +
# `@capabilities` headers onto every stdlib/*.hexa module that lacked them.
#
# Idempotent: skips files that already declare `@version`. Safe to re-run.
# Insertion point: immediately after line 1 (every stdlib file's first line
# is its `// HEXA Standard Library — X` or `// stdlib/X.hexa — desc` title).
# The injected block is exactly 4 lines:
#
#     //
#     // @version 0.1.0
#     // @capabilities [tag1, tag2]
#     //
#
# Capability vocabulary (free-form short tags, lowercase):
#   pure         — no side effects, no I/O (math, parse, encoding)
#   fs_read      — reads files / directories
#   fs_write     — writes / creates files
#   network      — opens sockets / makes HTTP / curl shell-out
#   process      — spawns / supervises OS processes (exec/popen/setsid)
#   shell        — invokes external shell utilities (sed/awk/wc/curl/ls)
#   randomness   — produces non-deterministic / RNG output
#   crypto       — hash / HMAC / cert verify (non-PKI, xxhash-style)
#   ffi          — calls native runtime builtins beyond pure stdlib core
#   reexport     — module is a thin re-export hub (mod.hexa convention)

set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Versions:
#   0.1.0 = baseline for every module
#   1.0.0 = modules that already shipped to a sister-repo as stable surface
#           (proc, json, http, bytes per the qmirror P0/P1 governance comment
#           in stdlib/proc.hexa).
declare -a SPEC=(
    "stdlib/string.hexa|0.1.0|pure"
    "stdlib/math.hexa|0.1.0|pure"
    "stdlib/parse.hexa|0.1.0|pure"
    "stdlib/collections.hexa|0.1.0|pure"
    "stdlib/json.hexa|1.0.0|pure"
    "stdlib/json_object.hexa|0.1.0|pure"
    "stdlib/yaml.hexa|0.1.0|pure"
    "stdlib/bytes.hexa|1.0.0|pure"
    "stdlib/http.hexa|1.0.0|network, shell"
    "stdlib/proc.hexa|1.0.0|process, shell, fs_write"
    "stdlib/portable_fs.hexa|0.1.0|fs_read, shell"
    "stdlib/qrng_anu.hexa|0.1.0|network, randomness, shell"
    "stdlib/registry_autodiscover.hexa|0.1.0|fs_read, shell"
    "stdlib/optim.hexa|0.1.0|pure, ffi"
    "stdlib/nn.hexa|0.1.0|pure, ffi"
    "stdlib/consciousness.hexa|0.1.0|pure, ffi"
    "stdlib/anima_audio_worker.hexa|0.1.0|process, fs_write, shell"
    "stdlib/net/mod.hexa|0.1.0|reexport, network"
    "stdlib/net/socket.hexa|0.1.0|network, ffi"
    "stdlib/net/http_request.hexa|0.1.0|pure"
    "stdlib/net/http_response.hexa|0.1.0|pure"
    "stdlib/net/http_server.hexa|0.1.0|network, ffi"
    "stdlib/net/http_client.hexa|0.1.0|network, shell"
    "stdlib/net/concurrent_serve.hexa|0.1.0|network, process"
    "stdlib/cert/mod.hexa|0.1.0|reexport, crypto"
    "stdlib/cert/dag.hexa|0.1.0|pure"
    "stdlib/cert/factor.hexa|0.1.0|pure"
    "stdlib/cert/json_scan.hexa|0.1.0|pure"
    "stdlib/cert/loader.hexa|0.1.0|fs_read"
    "stdlib/cert/meta2_schema.hexa|0.1.0|pure"
    "stdlib/cert/meta2_validator.hexa|0.1.0|pure"
    "stdlib/cert/reward.hexa|0.1.0|pure"
    "stdlib/cert/selftest.hexa|0.1.0|pure"
    "stdlib/cert/verdict.hexa|0.1.0|pure"
    "stdlib/cert/writer.hexa|0.1.0|fs_write"
    "stdlib/ckpt/mod.hexa|0.1.0|reexport, crypto"
    "stdlib/ckpt/chunk_store.hexa|0.1.0|fs_read, fs_write"
    "stdlib/ckpt/format.hexa|0.1.0|pure"
    "stdlib/ckpt/merkle.hexa|0.1.0|pure, crypto"
    "stdlib/ckpt/verifier.hexa|0.1.0|fs_read, crypto"
    "stdlib/ckpt/writer.hexa|0.1.0|fs_write, crypto"
    "stdlib/hash/xxhash.hexa|0.1.0|pure, crypto"
    "stdlib/linalg/mod.hexa|0.1.0|reexport"
    "stdlib/linalg/dispatch.hexa|0.1.0|pure, ffi"
    "stdlib/linalg/ffi.hexa|0.1.0|ffi"
    "stdlib/linalg/reference.hexa|0.1.0|pure"
    "stdlib/math/eigen.hexa|0.1.0|pure"
    "stdlib/math/permille.hexa|0.1.0|pure"
    "stdlib/math/rng.hexa|0.1.0|pure, randomness"
    "stdlib/math/rng_ctx.hexa|0.1.0|pure, randomness"
    "stdlib/matrix/mod.hexa|0.1.0|reexport, pure"
    "stdlib/matrix/construct.hexa|0.1.0|pure"
    "stdlib/matrix/stack.hexa|0.1.0|pure"
    "stdlib/optim/cpgd.hexa|0.1.0|pure"
    "stdlib/optim/projector.hexa|0.1.0|pure"
    "stdlib/policy/closure_roadmap.hexa|0.1.0|fs_read, pure"
    "stdlib/policy/dual_track.hexa|0.1.0|pure"
    "stdlib/tokenize/tokenizer_spec.hexa|0.1.0|pure, fs_read"
)

inject() {
    local f="$1"; local v="$2"; local caps="$3"
    if [[ ! -f "$f" ]]; then
        echo "  skip (missing): $f" >&2
        return 0
    fi
    if grep -qE '^[[:space:]]*//[[:space:]]*@version[[:space:]]' "$f"; then
        echo "  skip (already versioned): $f"
        return 0
    fi
    # Insert 4 header lines after line 1 (the file's `// title` line).
    # Use printf + cat to assemble; macOS awk can't take multiline -v values.
    {
        head -n 1 "$f"
        printf '%s\n' "//"
        printf '%s\n' "// @version $v"
        printf '%s\n' "// @capabilities [$caps]"
        printf '%s\n' "//"
        tail -n +2 "$f"
    } > "$f.tmp" && mv "$f.tmp" "$f"
    echo "  injected: $f (v=$v)"
}

count=0
for entry in "${SPEC[@]}"; do
    IFS='|' read -r path v caps <<< "$entry"
    inject "$path" "$v" "$caps"
    count=$((count + 1))
done

echo "[_version_header_seed] processed $count modules."

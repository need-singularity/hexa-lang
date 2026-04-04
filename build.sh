#!/bin/bash
set -e
export PATH="$HOME/.cargo/bin:/opt/homebrew/bin:$PATH"

if [ "$1" = "test" ]; then
    cargo test
elif [ "$1" = "release" ]; then
    cargo build --release
    cp target/release/hexa .
elif [ "$1" = "bench" ]; then
    cargo build --release
    cp target/release/hexa .
    bash scripts/bench_hexa_ir.sh
else
    cargo build
    cp target/debug/hexa .
fi

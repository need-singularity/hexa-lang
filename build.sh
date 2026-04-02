#!/bin/bash
set -e
export PATH="$HOME/.cargo/bin:/opt/homebrew/bin:$PATH"

if [ "$1" = "test" ]; then
    cargo test
elif [ "$1" = "release" ]; then
    cargo build --release
    cp target/release/hexa .
else
    cargo build
    cp target/debug/hexa .
fi

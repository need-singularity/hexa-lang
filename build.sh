#!/bin/bash
set -e
RUSTC="${HOME}/.cargo/bin/rustc"
SRC="src/main.rs"
OUT="hexa"

if [ "$1" = "test" ]; then
    $RUSTC --edition 2021 --test $SRC -o hexa_test && ./hexa_test
else
    $RUSTC --edition 2021 $SRC -o $OUT
    echo "Built: ./$OUT"
fi

#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g19"
mkdir -p "$BUILD"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G19_gfx_test.c" "$ROOT/kernel/gfx.c" -o "$BUILD/gfx-test-$mode"
    "$BUILD/gfx-test-$mode"
done
echo "PASS G19-GFX-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g08"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G08_graphics_test.c" \
    "$ROOT/kernel/graphics.c" \
    -o "$BUILD/graphics-test"
"$BUILD/graphics-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G08_graphics_test.c" \
    "$ROOT/kernel/graphics.c" \
    -o "$BUILD/graphics-test-sanitized"
"$BUILD/graphics-test-sanitized"
echo "PASS G08-GRAPHICS-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

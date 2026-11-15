#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g10"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror \
    -DEXPERIMENTALOS_DESKTOP_TEST_DIAGNOSTICS \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G10_desktop_test.c" \
    "$ROOT/kernel/desktop.c" "$ROOT/kernel/graphics.c" \
    "$ROOT/kernel/process.c" "$ROOT/kernel/memory.c" \
    -o "$BUILD/desktop-test"
"$BUILD/desktop-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -DEXPERIMENTALOS_DESKTOP_TEST_DIAGNOSTICS \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G10_desktop_test.c" \
    "$ROOT/kernel/desktop.c" "$ROOT/kernel/graphics.c" \
    "$ROOT/kernel/process.c" "$ROOT/kernel/memory.c" \
    -o "$BUILD/desktop-test-sanitized"
"$BUILD/desktop-test-sanitized"
echo "PASS G10-DESKTOP-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

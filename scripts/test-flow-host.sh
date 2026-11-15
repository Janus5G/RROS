#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g11"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror \
    -DEXPERIMENTALOS_DESKTOP_TEST_DIAGNOSTICS \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G11_flow_test.c" \
    "$ROOT/kernel/desktop.c" "$ROOT/kernel/graphics.c" \
    "$ROOT/kernel/process.c" "$ROOT/kernel/memory.c" \
    -o "$BUILD/flow-test"
"$BUILD/flow-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -DEXPERIMENTALOS_DESKTOP_TEST_DIAGNOSTICS \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G11_flow_test.c" \
    "$ROOT/kernel/desktop.c" "$ROOT/kernel/graphics.c" \
    "$ROOT/kernel/process.c" "$ROOT/kernel/memory.c" \
    -o "$BUILD/flow-test-sanitized"
"$BUILD/flow-test-sanitized"
echo "PASS G11-FLOW-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

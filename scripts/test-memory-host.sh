#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g03"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G03_memory_test.c" \
    "$ROOT/kernel/memory.c" \
    -o "$BUILD/memory-test"
"$BUILD/memory-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G03_memory_test.c" \
    "$ROOT/kernel/memory.c" \
    -o "$BUILD/memory-test-sanitized"
"$BUILD/memory-test-sanitized"
echo "PASS G03-MEMORY-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g05"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror -pthread \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G05_process_test.c" \
    "$ROOT/kernel/process.c" "$ROOT/kernel/memory.c" \
    -o "$BUILD/process-test"
"$BUILD/process-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror -pthread \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G05_process_test.c" \
    "$ROOT/kernel/process.c" "$ROOT/kernel/memory.c" \
    -o "$BUILD/process-test-sanitized"
"$BUILD/process-test-sanitized"
echo "PASS G05-PROCESS-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

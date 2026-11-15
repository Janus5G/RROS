#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g06"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G06_storage_test.c" \
    "$ROOT/kernel/storage.c" \
    -o "$BUILD/storage-test"
"$BUILD/storage-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G06_storage_test.c" \
    "$ROOT/kernel/storage.c" \
    -o "$BUILD/storage-test-sanitized"
"$BUILD/storage-test-sanitized"
echo "PASS G06-STORAGE-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

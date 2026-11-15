#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g07"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G07_network_test.c" \
    "$ROOT/kernel/network.c" \
    -o "$BUILD/network-test"
"$BUILD/network-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G07_network_test.c" \
    "$ROOT/kernel/network.c" \
    -o "$BUILD/network-test-sanitized"
"$BUILD/network-test-sanitized"
echo "PASS G07-NETWORK-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

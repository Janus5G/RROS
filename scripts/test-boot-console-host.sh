#!/bin/sh
# Host tests for firmware-driven boot console selection (plain and ASan/UBSan).
# Optional first argument: a Raspberry Pi 5 DTB to resolve for real.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/pi5-probe-host"
mkdir -p "$BUILD"
# Same parser limits as the probe: the real BCM2712 firmware DT has 323 nodes.
LIMITS="-DOS_DEVICE_TREE_MAX_NODES=512u -DOS_DEVICE_TREE_MAX_INTERRUPT_CELLS=32u"
SOURCES="$ROOT/kernel/boot_console_test.c $ROOT/kernel/boot_console.c $ROOT/kernel/device_tree.c"

cc -std=c17 -O2 -Wall -Wextra -Werror $LIMITS -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    $SOURCES -o "$BUILD/boot-console-test"
"$BUILD/boot-console-test" "$@"

cc -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-sanitize-recover=undefined $LIMITS -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    $SOURCES -o "$BUILD/boot-console-test-sanitized"
"$BUILD/boot-console-test-sanitized" "$@"
echo "PASS PI5-BOOT-CONSOLE-HOST: plain and AddressSanitizer/UndefinedBehaviorSanitizer"

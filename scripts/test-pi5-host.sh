#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g12"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G12_device_tree_test.c" \
    "$ROOT/kernel/device_tree.c" "$ROOT/kernel/device_tree_test.c" \
    "$ROOT/kernel/memory.c" \
    "$ROOT/kernel/pi5_platform.c" "$ROOT/kernel/pi5_driver.c" \
    "$ROOT/kernel/network.c" \
    "$ROOT/kernel/pi5_driver_test.c" \
    "$ROOT/kernel/pi5_dwc3_core.c" "$ROOT/kernel/pi5_usb_host.c" "$ROOT/kernel/pi5_gem_host.c" \
    -o "$BUILD/device-tree-test"
"$BUILD/device-tree-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G12_device_tree_test.c" \
    "$ROOT/kernel/device_tree.c" "$ROOT/kernel/device_tree_test.c" \
    "$ROOT/kernel/memory.c" \
    "$ROOT/kernel/pi5_platform.c" "$ROOT/kernel/pi5_driver.c" \
    "$ROOT/kernel/network.c" \
    "$ROOT/kernel/pi5_driver_test.c" \
    "$ROOT/kernel/pi5_dwc3_core.c" "$ROOT/kernel/pi5_usb_host.c" "$ROOT/kernel/pi5_gem_host.c" \
    -o "$BUILD/device-tree-test-sanitized"
"$BUILD/device-tree-test-sanitized"
echo "PASS G12-DEVICE-TREE-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G12_aarch64_mmu_test.c" \
    "$ROOT/kernel/aarch64_mmu.c" \
    -o "$BUILD/aarch64-mmu-test"
"$BUILD/aarch64-mmu-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G12_aarch64_mmu_test.c" \
    "$ROOT/kernel/aarch64_mmu.c" \
    -o "$BUILD/aarch64-mmu-test-sanitized"
"$BUILD/aarch64-mmu-test-sanitized"
echo "PASS G12-AARCH64-MMU-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G12_aarch64_mmu_runtime_test.c" \
    "$ROOT/kernel/aarch64_mmu_runtime.c" \
    "$ROOT/kernel/aarch64_mmu.c" \
    -o "$BUILD/aarch64-mmu-runtime-test"
"$BUILD/aarch64-mmu-runtime-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G12_aarch64_mmu_runtime_test.c" \
    "$ROOT/kernel/aarch64_mmu_runtime.c" \
    "$ROOT/kernel/aarch64_mmu.c" \
    -o "$BUILD/aarch64-mmu-runtime-test-sanitized"
"$BUILD/aarch64-mmu-runtime-test-sanitized"
echo "PASS G12-AARCH64-MMU-RUNTIME-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G12_dwc3_core_test.c" \
    "$ROOT/kernel/pi5_dwc3_core.c" \
    -o "$BUILD/dwc3-core-test"
"$BUILD/dwc3-core-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G12_dwc3_core_test.c" \
    "$ROOT/kernel/pi5_dwc3_core.c" \
    -o "$BUILD/dwc3-core-test-sanitized"
"$BUILD/dwc3-core-test-sanitized"
echo "PASS G12-DWC3-CORE-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"
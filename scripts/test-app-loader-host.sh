#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLCHAIN="$ROOT/.toolchain/llvm"
CLANG="$TOOLCHAIN/usr/lib/llvm-21/bin/clang"
BUILD="$ROOT/build/g09"
mkdir -p "$BUILD"

cc -std=c17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G09_app_loader_test.c" \
    "$ROOT/kernel/app_loader.c" \
    -o "$BUILD/app-loader-test"
"$BUILD/app-loader-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G09_app_loader_test.c" \
    "$ROOT/kernel/app_loader.c" \
    -o "$BUILD/app-loader-test-sanitized"
"$BUILD/app-loader-test-sanitized"
echo "PASS G09-LOADER-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

"$CLANG" --target=x86_64-none-elf -std=c17 -ffreestanding -Wall -Wextra -Werror \
    -I "$ROOT/sdk/include" -I "$ROOT/boot/uefi" \
    -c "$ROOT/sdk/examples/hello.c" \
    -o "$BUILD/hello-x86_64.o"
"$CLANG" --target=aarch64-none-elf -std=c17 -ffreestanding -Wall -Wextra -Werror \
    -I "$ROOT/sdk/include" -I "$ROOT/boot/uefi" \
    -c "$ROOT/sdk/examples/hello.c" \
    -o "$BUILD/hello-aarch64.o"
echo "PASS G09-SDK-CROSS-COMPILE: x86-64 and AArch64 ABI wrappers compile"

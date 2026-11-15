#!/bin/sh
# Builds the isolated Raspberry Pi 5 hardware-probe UEFI application.
# Output: build/pi5-probe/{obj,esp}. Never touches build/<arch> or build/dev.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLCHAIN="$ROOT/.toolchain/llvm"
BIN="$TOOLCHAIN/usr/lib/llvm-21/bin"
LIBS="$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/usr/lib/llvm-21/lib"
OUT="${EXPERIMENTALOS_PROBE_DIR:-$ROOT/build/pi5-probe}"
OBJ="$OUT/obj"
ESP="$OUT/esp"
REVISION="${PI5_PROBE_REVISION:-$(git -C "$ROOT" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)}"

export PATH="$BIN:$PATH"
export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

rm -rf "$OUT"
mkdir -p "$OBJ" "$ESP/EFI/BOOT"

# One parser layout for every probe object: the real BCM2712 firmware DT has 323 nodes.
COMMON="--target=aarch64-pc-windows-msvc -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -DOS_DEVICE_TREE_MAX_NODES=512u -DOS_DEVICE_TREE_MAX_INTERRUPT_CELLS=32u \
    -I $ROOT/boot/uefi -I $ROOT/kernel"

printf 'TOOLCHAIN: %s\n' "$(clang --version | head -n 1)"
# shellcheck disable=SC2086
clang $COMMON -DPI5_PROBE_REVISION="\"$REVISION\"" -c "$ROOT/boot/aarch64/pi5_probe_main.c" -o "$OBJ/pi5_probe_main.obj"
clang $COMMON -c "$ROOT/boot/aarch64/pi5_probe_support.c" -o "$OBJ/pi5_probe_support.obj"
for unit in boot_console device_tree aarch64_mmu aarch64_mmu_runtime; do
    # shellcheck disable=SC2086
    clang $COMMON -c "$ROOT/kernel/$unit.c" -o "$OBJ/$unit.obj"
done
clang --target=aarch64-pc-windows-msvc -ffreestanding -c "$ROOT/boot/aarch64/vectors.S" -o "$OBJ/vectors.obj"

ld.lld -flavor link /brepro \
    /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm64 \
    "/out:$ESP/EFI/BOOT/BOOTAA64.EFI" \
    "$OBJ/pi5_probe_main.obj" "$OBJ/pi5_probe_support.obj" "$OBJ/boot_console.obj" "$OBJ/device_tree.obj" \
    "$OBJ/aarch64_mmu.obj" "$OBJ/aarch64_mmu_runtime.obj" "$OBJ/vectors.obj"

printf 'Built %s (probe revision %s)\n' "$ESP/EFI/BOOT/BOOTAA64.EFI" "$REVISION"

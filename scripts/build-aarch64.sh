#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLCHAIN="$ROOT/.toolchain/llvm"
BIN="$TOOLCHAIN/usr/lib/llvm-21/bin"
LIBS="$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/usr/lib/llvm-21/lib"
# Development and gate builds never write the release tree (build/<arch>).
if [ -n "${EXPERIMENTALOS_BUILD_DIR:-}" ]; then
    BUILD="$EXPERIMENTALOS_BUILD_DIR"
elif [ "${EXPERIMENTALOS_RELEASE_BUILD:-0}" = "1" ]; then
    BUILD="$ROOT/build/aarch64"
else
    BUILD="$ROOT/build/dev/aarch64"
fi
ESP="$BUILD/esp"
G04_DEFINE=
if [ "${EXPERIMENTALOS_G04_TEST:-0}" = "1" ]; then
    G04_DEFINE=-DEXPERIMENTALOS_G04_TEST
fi
G05_DEFINE=
if [ "${EXPERIMENTALOS_G05_TEST:-0}" = "1" ]; then
    G05_DEFINE=-DEXPERIMENTALOS_G05_TEST
fi
G06_DEFINE=
if [ "${EXPERIMENTALOS_G06_TEST:-0}" = "1" ]; then
    G06_DEFINE=-DEXPERIMENTALOS_G06_TEST
fi
G07_DEFINE=
if [ "${EXPERIMENTALOS_G07_TEST:-0}" = "1" ]; then
    G07_DEFINE=-DEXPERIMENTALOS_G07_TEST
fi
G08_DEFINE=
if [ "${EXPERIMENTALOS_G08_TEST:-0}" = "1" ]; then
    G08_DEFINE=-DEXPERIMENTALOS_G08_TEST
fi
G09_DEFINE=
if [ "${EXPERIMENTALOS_G09_TEST:-0}" = "1" ]; then
    G09_DEFINE=-DEXPERIMENTALOS_G09_TEST
fi
G10_DEFINE=
if [ "${EXPERIMENTALOS_G10_TEST:-0}" = "1" ]; then
    G10_DEFINE=-DEXPERIMENTALOS_G10_TEST
fi
G11_DEFINE=
if [ "${EXPERIMENTALOS_G11_TEST:-0}" = "1" ]; then
    G11_DEFINE=-DEXPERIMENTALOS_G11_TEST
fi
G12_DEFINE=
if [ "${EXPERIMENTALOS_G12_TEST:-0}" = "1" ]; then
    G12_DEFINE=-DEXPERIMENTALOS_G12_TEST
fi
QEMU_VIRT_DEFINE=
if [ "${EXPERIMENTALOS_QEMU_VIRT:-0}" = "1" ]; then
    QEMU_VIRT_DEFINE=-DEXPERIMENTALOS_QEMU_VIRT
fi
G19_DEFINE=
if [ "${EXPERIMENTALOS_G19_TEST:-0}" = "1" ]; then
    G19_DEFINE=-DEXPERIMENTALOS_G19_TEST
fi
G13_DEFINE=
if [ "${EXPERIMENTALOS_G13_TEST:-0}" = "1" ]; then
    G13_DEFINE=-DEXPERIMENTALOS_G13_TEST
fi

export PATH="$BIN:$PATH"
export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

printf 'TOOLCHAIN: %s\n' "$(clang --version | head -n 1)"
printf 'TOOLCHAIN: %s\n' "$(ld.lld --version | head -n 1)"
mkdir -p "$ESP/EFI/BOOT"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    $G04_DEFINE $G05_DEFINE $G06_DEFINE $G07_DEFINE $G08_DEFINE $G09_DEFINE \
    $G10_DEFINE $G11_DEFINE $G12_DEFINE $G13_DEFINE $G19_DEFINE $QEMU_VIRT_DEFINE \
    -c "$ROOT/boot/aarch64/efi_main.c" \
    -o "$BUILD/efi_main.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/memory.c" \
    -o "$BUILD/memory.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/aarch64_mmu.c" \
    -o "$BUILD/aarch64_mmu.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/aarch64_mmu_runtime.c" \
    -o "$BUILD/aarch64_mmu_runtime.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    $QEMU_VIRT_DEFINE \
    -c "$ROOT/kernel/aarch64_mmu_uefi.c" \
    -o "$BUILD/aarch64_mmu_uefi.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/g04.c" \
    -o "$BUILD/g04.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/g05.c" \
    -o "$BUILD/g05.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/arch/aarch64/g05.c" \
    -o "$BUILD/g05_arch.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/process.c" \
    -o "$BUILD/process.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/storage.c" \
    -o "$BUILD/storage.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/network.c" \
    -o "$BUILD/network.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/graphics.c" \
    -o "$BUILD/graphics.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/desktop.c" \
    -o "$BUILD/desktop.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/boot_console.c" \
    -o "$BUILD/boot_console.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/device_tree.c" \
    -o "$BUILD/device_tree.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/pi5_platform.c" \
    -o "$BUILD/pi5_platform.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/pi5_driver.c" \
    -o "$BUILD/pi5_driver.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/pi5_dwc3_core.c" \
    -o "$BUILD/pi5_dwc3_core.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/pi5_usb_host.c" \
    -o "$BUILD/pi5_usb_host.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/pi5_gem_host.c" \
    -o "$BUILD/pi5_gem_host.obj"
G12_TEST_OBJECT=
G12_DRIVER_TEST_OBJECT=
if [ "${EXPERIMENTALOS_G12_TEST:-0}" = "1" ]; then
    clang --target=aarch64-pc-windows-msvc \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
        -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
        -c "$ROOT/kernel/device_tree_test.c" \
        -o "$BUILD/device_tree_test.obj"
    G12_TEST_OBJECT="$BUILD/device_tree_test.obj"
    clang --target=aarch64-pc-windows-msvc \
        -std=c17 -O2 -Wall -Wextra -Werror \
        -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
        -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
        -c "$ROOT/kernel/pi5_driver_test.c" \
        -o "$BUILD/pi5_driver_test.obj"
    G12_DRIVER_TEST_OBJECT="$BUILD/pi5_driver_test.obj"
fi
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
    -c "$ROOT/kernel/app_loader.c" \
    -o "$BUILD/app_loader.obj"
clang --target=aarch64-pc-windows-msvc \
    -std=c17 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -I "$ROOT/boot/uefi" \
    -c "$ROOT/arch/aarch64/timer.c" \
    -o "$BUILD/timer.obj"
clang --target=aarch64-pc-windows-msvc \
    -ffreestanding -c "$ROOT/boot/aarch64/vectors.S" \
    -o "$BUILD/vectors.obj"
clang --target=aarch64-pc-windows-msvc \
    -ffreestanding -c "$ROOT/boot/aarch64/g05_context.S" \
    -o "$BUILD/g05_context.obj"
G13_OBJECTS=
if [ "${EXPERIMENTALOS_G13_TEST:-0}" = "1" ]; then
    for unit in ai_core ai_worker ai_selftest; do
        clang --target=aarch64-pc-windows-msvc \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
            -c "$ROOT/kernel/$unit.c" \
            -o "$BUILD/$unit.obj"
        G13_OBJECTS="$G13_OBJECTS $BUILD/$unit.obj"
    done
fi
if [ "${EXPERIMENTALOS_G19_TEST:-0}" = "1" ]; then
    for unit in gfx gfx_selftest; do
        clang --target=aarch64-pc-windows-msvc \
            -std=c17 -O2 -Wall -Wextra -Werror \
            -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
            -c "$ROOT/kernel/$unit.c" \
            -o "$BUILD/$unit.obj"
        G13_OBJECTS="$G13_OBJECTS $BUILD/$unit.obj"
    done
fi
ld.lld -flavor link /brepro \
    /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm64 \
    "/out:$ESP/EFI/BOOT/BOOTAA64.EFI" \
    "$BUILD/efi_main.obj" "$BUILD/memory.obj" "$BUILD/aarch64_mmu.obj" \
    "$BUILD/aarch64_mmu_runtime.obj" "$BUILD/aarch64_mmu_uefi.obj" \
    "$BUILD/g04.obj" \
    "$BUILD/g05.obj" "$BUILD/g05_arch.obj" "$BUILD/process.obj" \
    "$BUILD/storage.obj" "$BUILD/network.obj" "$BUILD/graphics.obj" \
    "$BUILD/desktop.obj" \
    "$BUILD/boot_console.obj" "$BUILD/device_tree.obj" "$BUILD/pi5_platform.obj" "$BUILD/pi5_driver.obj" "$BUILD/pi5_dwc3_core.obj" "$BUILD/pi5_usb_host.obj" "$BUILD/pi5_gem_host.obj" \
    $G12_TEST_OBJECT $G12_DRIVER_TEST_OBJECT $G13_OBJECTS \
    "$BUILD/app_loader.obj" \
    "$BUILD/timer.obj" "$BUILD/vectors.obj" \
    "$BUILD/g05_context.obj"

printf 'Built %s\n' "$ESP/EFI/BOOT/BOOTAA64.EFI"

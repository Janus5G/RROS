#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOL="$ROOT/.toolchain/qemu-arm"
QEMU="$TOOL/root/usr/bin/qemu-system-aarch64"
FIRMWARE="$TOOL/root/usr/share/AAVMF"
BUILD="$ROOT/build/aarch64"
ESP="${EXPERIMENTALOS_BUILD_DIR:-$ROOT/build/dev/aarch64}/esp"
VARS="$BUILD/AAVMF_VARS.fd"
SERIAL_LOG="$BUILD/qemu-serial.log"

sh "$ROOT/scripts/bootstrap-aarch64-emu.sh"
sh "$ROOT/scripts/build-aarch64.sh"
export QEMU_MODULE_DIR="$TOOL/root/usr/lib/x86_64-linux-gnu/qemu"
export QEMU_DATADIR="$TOOL/root/usr/share/qemu"
mkdir -p "$BUILD"
cp "$FIRMWARE/AAVMF_VARS.fd" "$VARS"
: > "$SERIAL_LOG"

set +e
timeout --signal=TERM --kill-after=2s "${EXPERIMENTALOS_QEMU_TIMEOUT:-90}s" \
    "$QEMU" \
    -machine virt -cpu cortex-a72 -m 1024M -smp 1 \
    -nodefaults -no-reboot -no-shutdown -display none -monitor none \
    -serial "file:$SERIAL_LOG" \
    -drive "if=pflash,format=raw,readonly=on,file=$FIRMWARE/AAVMF_CODE.no-secboot.fd" \
    -drive "if=pflash,format=raw,file=$VARS" \
    -drive "format=raw,file=fat:rw:$ESP"
qemu_status=$?
set -e

if [ "$qemu_status" -ne 124 ] && [ "$qemu_status" -ne 137 ]; then
    echo "AArch64 QEMU exited unexpectedly with status $qemu_status" >&2
    cat "$SERIAL_LOG" >&2
    exit 1
fi

for marker in \
    "G02 AARCH64 UEFI ENTRY" \
    "G02 EXCEPTION LEVEL VALIDATED" \
    "G02 MMU STATE VALIDATED" \
    "G02 EXCEPTION VECTOR PASS" \
    "G02 MEMORY MAP VALIDATED" \
    "G02 UEFI HANDOFF PASS" \
    "G02 IDLE"
do
    if ! grep -Fq "$marker" "$SERIAL_LOG"; then
        echo "Missing AArch64 guest marker: $marker" >&2
        cat "$SERIAL_LOG" >&2
        exit 1
    fi
done

echo "PASS: AArch64 UEFI guest validated exception level, MMU state, vectors, memory map and idle."

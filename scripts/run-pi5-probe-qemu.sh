#!/bin/sh
# Runs the isolated Pi 5 hardware-probe under generic AArch64 QEMU/AAVMF (virtual regression only).
# This is NOT Raspberry Pi 5 hardware evidence. Log: logs/pi5_probe/<UTC timestamp>_qemu_probe_serial.log
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOL="$ROOT/.toolchain/qemu-arm"
QEMU="$TOOL/root/usr/bin/qemu-system-aarch64"
FIRMWARE="$TOOL/root/usr/share/AAVMF"
ESP="${EXPERIMENTALOS_PROBE_DIR:-$ROOT/build/pi5-probe}/esp"
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
LOGDIR="$ROOT/logs/pi5_probe"
SERIAL_LOG="$LOGDIR/${STAMP}_qemu_probe_serial.log"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$LOGDIR"

export QEMU_MODULE_DIR="$TOOL/root/usr/lib/x86_64-linux-gnu/qemu"
export QEMU_DATADIR="$TOOL/root/usr/share/qemu"
# vvfat needs a writable directory; use a throwaway copy so the probe ESP stays untouched.
cp -R "$ESP" "$WORK/esp"
cp "$FIRMWARE/AAVMF_VARS.fd" "$WORK/VARS.fd"
: > "$SERIAL_LOG"

set +e
timeout --signal=TERM --kill-after=2s "${EXPERIMENTALOS_QEMU_TIMEOUT:-60}s" \
    "$QEMU" \
    -machine virt,acpi=off -cpu cortex-a72 -m 1024M -smp 1 \
    -nodefaults -no-reboot -no-shutdown -display none -monitor none \
    -serial "file:$SERIAL_LOG" \
    -drive "if=pflash,format=raw,readonly=on,file=$FIRMWARE/AAVMF_CODE.no-secboot.fd" \
    -drive "if=pflash,format=raw,file=$WORK/VARS.fd" \
    -drive "format=raw,file=fat:rw:$WORK/esp"
status=$?
set -e
echo "qemu_exit=$status serial_log=$SERIAL_LOG"
python3 "$ROOT/tests/pi5_hw_validation/evaluate_probe_log.py" --mode qemu "$SERIAL_LOG"

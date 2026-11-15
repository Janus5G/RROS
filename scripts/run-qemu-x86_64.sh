#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/x86_64"
ESP="${EXPERIMENTALOS_BUILD_DIR:-$ROOT/build/dev/x86_64}/esp"
OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS_TEMPLATE=/usr/share/OVMF/OVMF_VARS_4M.fd
OVMF_VARS="$BUILD/OVMF_VARS.fd"
SERIAL_LOG="$BUILD/qemu-serial.log"

sh "$ROOT/scripts/build-x86_64.sh"
test -r "$OVMF_CODE"
test -r "$OVMF_VARS_TEMPLATE"
mkdir -p "$BUILD"
cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"
: > "$SERIAL_LOG"

set +e
printf 'TOOLCHAIN: %s\n' "$(qemu-system-x86_64 --version | head -n 1)"
timeout --signal=TERM --kill-after=2s "${EXPERIMENTALOS_QEMU_TIMEOUT:-30}s" \
    qemu-system-x86_64 \
    -machine q35 -m 256M -smp 1 \
    -no-reboot -no-shutdown -display none -monitor none \
    -serial "file:$SERIAL_LOG" \
    -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE" \
    -drive "if=pflash,format=raw,file=$OVMF_VARS" \
    -drive "format=raw,file=fat:rw:$ESP"
qemu_status=$?
set -e

if [ "$qemu_status" -ne 124 ] && [ "$qemu_status" -ne 137 ]; then
    echo "QEMU exited unexpectedly with status $qemu_status" >&2
    cat "$SERIAL_LOG" >&2
    exit 1
fi

for marker in \
    "G01 UEFI ENTRY" \
    "G01 DISPLAY CONOUT_SUCCESS" \
    "UEFI BOOT SERVICES EXITED" \
    "MEMORY MAP VALIDATED" \
    "G01 UEFI HANDOFF PASS" \
    "G01 IDLE"
do
    if ! grep -Fq "$marker" "$SERIAL_LOG"; then
        echo "Missing guest marker: $marker" >&2
        cat "$SERIAL_LOG" >&2
        exit 1
    fi
done

echo "PASS: QEMU guest completed UEFI handoff, serial/display checks and safe idle."

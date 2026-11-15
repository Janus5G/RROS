#!/bin/sh
# Boots the release ISO (x86-64, OVMF) and the Pi 5 image (generic AArch64 UEFI guest, NOT Pi hardware) in QEMU.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/build/release-boot"
rm -rf "$OUT"
mkdir -p "$OUT"

OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$OUT/OVMF_VARS.fd"
: > "$OUT/x86.log"
set +e
timeout --signal=TERM --kill-after=2s 10s \
    qemu-system-x86_64 -machine q35 -m 256M -smp 1 -no-reboot -no-shutdown -display none -monitor none \
    -serial "file:$OUT/x86.log" \
    -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE" \
    -drive "if=pflash,format=raw,file=$OUT/OVMF_VARS.fd" \
    -drive "format=raw,media=cdrom,readonly=on,file=$ROOT/build/x86_64/ExperimentalOS.iso"
status=$?
set -e
[ "$status" -eq 124 ] || [ "$status" -eq 137 ] || { echo "x86 ISO QEMU exited with $status" >&2; cat "$OUT/x86.log" >&2; exit 1; }
for m in "G01 UEFI ENTRY" "UEFI BOOT SERVICES EXITED" "G01 UEFI HANDOFF PASS" "G01 IDLE"; do
    grep -Fq "$m" "$OUT/x86.log" || { echo "Missing x86 ISO marker: $m" >&2; cat "$OUT/x86.log" >&2; exit 1; }
done
echo "PASS RELEASE-ISO-BOOTS-IN-OVMF-GUEST"

TOOL="$ROOT/.toolchain/qemu-arm"
FW="$TOOL/root/usr/share/AAVMF"
export QEMU_MODULE_DIR="$TOOL/root/usr/lib/x86_64-linux-gnu/qemu"
export QEMU_DATADIR="$TOOL/root/usr/share/qemu"
cp "$FW/AAVMF_VARS.fd" "$OUT/AAVMF_VARS.fd"
: > "$OUT/aarch64.log"
set +e
timeout --signal=TERM --kill-after=2s 40s \
    "$TOOL/root/usr/bin/qemu-system-aarch64" -machine virt -cpu cortex-a72 -m 1024M -smp 1 \
    -nodefaults -no-reboot -no-shutdown -display none -monitor none \
    -serial "file:$OUT/aarch64.log" \
    -drive "if=pflash,format=raw,readonly=on,file=$FW/AAVMF_CODE.no-secboot.fd" \
    -drive "if=pflash,format=raw,file=$OUT/AAVMF_VARS.fd" \
    -drive "format=raw,snapshot=on,file=$ROOT/build/aarch64/ExperimentalOS-Pi5.img"
status=$?
set -e
[ "$status" -eq 124 ] || [ "$status" -eq 137 ] || { echo "AArch64 image QEMU exited with $status" >&2; cat "$OUT/aarch64.log" >&2; exit 1; }
for m in "G02 AARCH64 UEFI ENTRY" "G02 UEFI HANDOFF PASS" "G02 IDLE"; do
    grep -Fq "$m" "$OUT/aarch64.log" || { echo "Missing AArch64 image marker: $m" >&2; cat "$OUT/aarch64.log" >&2; exit 1; }
done
echo "PASS RELEASE-PI5-IMAGE-BOOTS-AS-GENERIC-AARCH64-UEFI-GUEST (not Raspberry Pi hardware)"

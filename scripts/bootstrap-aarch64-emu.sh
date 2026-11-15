#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOL="$ROOT/.toolchain/qemu-arm"
BIN="$TOOL/root/usr/bin/qemu-system-aarch64"
CODE="$TOOL/root/usr/share/AAVMF/AAVMF_CODE.no-secboot.fd"
VARS="$TOOL/root/usr/share/AAVMF/AAVMF_VARS.fd"

if [ -x "$BIN" ] && [ -r "$CODE" ] && [ -r "$VARS" ]; then
    "$BIN" --version | grep -q '10\.2\.1'
    "$BIN" --version | head -n 1
    printf 'AAVMF code: %s\n' "$CODE"
    exit 0
fi

command -v apt-get >/dev/null 2>&1 || {
    echo "apt-get is required to fetch the AArch64 emulator packages." >&2
    exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
    echo "dpkg-deb is required to extract the emulator packages." >&2
    exit 1
}

mkdir -p "$TOOL/packages" "$TOOL/root"
cd "$TOOL/packages"
apt-get download \
    qemu-system-arm=1:10.2.1+ds-1ubuntu3 \
    qemu-system-common=1:10.2.1+ds-1ubuntu3 \
    qemu-system-data=1:10.2.1+ds-1ubuntu3 \
    qemu-efi-aarch64=2025.11-3ubuntu7.3
for package in ./*.deb; do
    dpkg-deb -x "$package" "$TOOL/root"
done

"$BIN" --version | grep '10\.2\.1'
printf 'AAVMF code: %s\n' "$CODE"
test -r "$CODE"
test -r "$VARS"

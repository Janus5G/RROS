#!/bin/sh
# Builds the local ExperimentalOS release candidate. Never pushes, publishes or flashes anything.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"
export SOURCE_DATE_EPOCH=1767225600
export TZ=UTC
export LC_ALL=C

OUT_X="$ROOT/build/x86_64"
OUT_A="$ROOT/build/aarch64"
SDK="$ROOT/build/sdk"
MAN="$ROOT/build/manifests"
STAGE="$ROOT/build/release-stage"

unset EXPERIMENTALOS_G04_TEST EXPERIMENTALOS_G05_TEST EXPERIMENTALOS_G06_TEST EXPERIMENTALOS_G07_TEST \
    EXPERIMENTALOS_G08_TEST EXPERIMENTALOS_G09_TEST EXPERIMENTALOS_G10_TEST EXPERIMENTALOS_G11_TEST \
    EXPERIMENTALOS_G12_TEST EXPERIMENTALOS_G13_TEST EXPERIMENTALOS_G19_TEST || true

# The release tree is immutable evidence: refuse to rebuild over an existing release image.
if [ -e "$OUT_A/ExperimentalOS-Pi5.img" ] && [ "${EXPERIMENTALOS_ALLOW_RELEASE_REBUILD:-0}" != "1" ]; then
    echo "refusing to overwrite existing release artifacts; set EXPERIMENTALOS_ALLOW_RELEASE_REBUILD=1 with owner approval" >&2
    exit 2
fi
export EXPERIMENTALOS_RELEASE_BUILD=1
rm -rf "$OUT_X" "$OUT_A" "$SDK" "$MAN" "$STAGE" "$ROOT/build/test-results"
sh scripts/build-x86_64.sh
sh scripts/build-aarch64.sh
mkdir -p "$SDK" "$MAN" "$STAGE" "$ROOT/build/test-results"

# x86-64 UEFI bootable ISO: El Torito EFI image plus a GPT-visible ESP.
mkdir -p "$STAGE/x86/EFI/BOOT" "$STAGE/iso"
cp "$OUT_X/esp/EFI/BOOT/BOOTX64.EFI" "$STAGE/x86/EFI/BOOT/BOOTX64.EFI"
python3 tools/mkimage.py fat "$STAGE/x86" "$STAGE/iso/efiboot.img" --size-mib 8 --bits 16
mkdir -p "$STAGE/iso/EFI/BOOT"
cp "$OUT_X/esp/EFI/BOOT/BOOTX64.EFI" "$STAGE/iso/EFI/BOOT/BOOTX64.EFI"
find "$STAGE/iso" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
rm -f "$OUT_X/ExperimentalOS.iso"
xorriso -as mkisofs -quiet -iso-level 3 -V EXPERIMENTALOS -R \
    -e efiboot.img -no-emul-boot \
    -appended_part_as_gpt -append_partition 2 C12A7328-F81F-11D2-BA4B-00A0C93EC93B "$STAGE/iso/efiboot.img" \
    -o "$OUT_X/ExperimentalOS.iso" "$STAGE/iso" 2>&1

# AArch64 Raspberry Pi 5 image: GPT disk with a FAT32 EFI System Partition. Structural packaging only.
mkdir -p "$STAGE/pi/EFI/BOOT"
cp "$OUT_A/esp/EFI/BOOT/BOOTAA64.EFI" "$STAGE/pi/EFI/BOOT/BOOTAA64.EFI"
python3 tools/mkimage.py gpt-disk "$STAGE/pi" "$OUT_A/ExperimentalOS-Pi5.img" --partition-mib 64 --disk-mib 80 --bits 32

# SDK package (deterministic archive).
mkdir -p "$STAGE/sdkpkg/ExperimentalOS-SDK/kernel" "$STAGE/sdkpkg/ExperimentalOS-SDK/tools" "$STAGE/sdkpkg/ExperimentalOS-SDK/docs"
cp -r sdk/include sdk/examples "$STAGE/sdkpkg/ExperimentalOS-SDK/"
cp kernel/abi.h "$STAGE/sdkpkg/ExperimentalOS-SDK/kernel/"
cp tools/eospkg.py tools/mkimage.py tools/checkimage.py "$STAGE/sdkpkg/ExperimentalOS-SDK/tools/"
for d in KNOWN_LIMITATIONS.md SECURITY.md HARDWARE_VALIDATION_GUIDE.md REPRODUCIBLE_BUILD.md RELEASE_NOTES.md; do
    cp "docs/$d" "$STAGE/sdkpkg/ExperimentalOS-SDK/docs/"
done

pack() {
    # pack <dir> <member> <out.tar.gz>
    (cd "$1" && tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner \
        --mode='u=rwX,go=rX' -cf - "$2" | gzip -n -9 > "$3")
}
pack "$STAGE/sdkpkg" ExperimentalOS-SDK "$SDK/ExperimentalOS-SDK-0.1.0-rc1.tar.gz"

# Development & Deployment Manager resources: contracts and simulation sources. No executable exists.
mkdir -p "$STAGE/ddm/RPi5-DevDeploy-Manager-Resources/kernel" "$STAGE/ddm/RPi5-DevDeploy-Manager-Resources/docs"
for f in mcp_deploy.c mcp_deploy.h devpkg.c devpkg.h secdeploy.c secdeploy.h; do
    cp "kernel/$f" "$STAGE/ddm/RPi5-DevDeploy-Manager-Resources/kernel/"
done
cp docs/ARCHITECTURE.md docs/G14_PROGRESS.md docs/G20_PROGRESS.md docs/SECURITY.md \
    "$STAGE/ddm/RPi5-DevDeploy-Manager-Resources/docs/"
printf '%s\n' 'Executable: NOT PRESENT (Not implemented). These are contracts, sources and simulation code only.' \
    'JSON manifests are data, never executable code. No private keys or credentials are included.' \
    > "$STAGE/ddm/RPi5-DevDeploy-Manager-Resources/README.txt"
pack "$STAGE/ddm" RPi5-DevDeploy-Manager-Resources "$SDK/RPi5-DevDeploy-Manager-Resources-0.1.0-rc1.tar.gz"

# Test reports and manifests.
cp logs/G*_RESULT.md "$ROOT/build/test-results/" 2>/dev/null || true
python3 tools/release_manifest.py
echo "RELEASE CANDIDATE BUILT (local only; hardware not verified)"

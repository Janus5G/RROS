#!/bin/sh
# Run only in the disposable Ubuntu CI container, never on a shared development host.
set -eu
[ "${RROS_DISPOSABLE_CI:-}" = "1" ] || {
    echo "Refusing host provisioning: RROS_DISPOSABLE_CI=1 is required." >&2
    exit 2
}
export DEBIAN_FRONTEND=noninteractive
# The digest-pinned base image may not yet have the CA bundle.
apt-get update
apt-get install -y --no-install-recommends ca-certificates=20260816ubuntu0.26.04.2
rm -f /etc/apt/sources.list.d/ubuntu.sources
printf '%s\n' \
    'deb https://snapshot.ubuntu.com/ubuntu/20261113T000000Z resolute main universe' \
    'deb https://snapshot.ubuntu.com/ubuntu/20261113T000000Z resolute-updates main universe' \
    'deb https://snapshot.ubuntu.com/ubuntu/20261113T000000Z resolute-security main universe' \
    > /etc/apt/sources.list
apt-get -o Acquire::Check-Valid-Until=false update
apt-get -o Acquire::Check-Valid-Until=false install -y --no-install-recommends \
    ca-certificates git curl gcc make python3 python3-serial xorriso gh=2.46.0-4 \
    clang-21=1:21.1.8-6ubuntu1 lld-21=1:21.1.8-6ubuntu1 \
    qemu-system-x86=1:10.2.1+ds-1ubuntu3.3 \
    qemu-system-arm=1:10.2.1+ds-1ubuntu3.3 \
    qemu-system-common=1:10.2.1+ds-1ubuntu3.3 \
    qemu-system-data=1:10.2.1+ds-1ubuntu3.3 \
    qemu-utils=1:10.2.1+ds-1ubuntu3.3 \
    ovmf=2025.11-3ubuntu7.3 qemu-efi-aarch64=2025.11-3ubuntu7.3 \
    nodejs=22.22.1+dfsg+~cs22.19.15-1ubuntu1
mkdir -p .toolchain/llvm .toolchain/qemu-arm/root
ln -s /usr .toolchain/llvm/usr
ln -s /usr .toolchain/qemu-arm/root/usr
dpkg-query -W > .toolchain/ci-packages.txt
sh scripts/release/bootstrap-actionlint.sh

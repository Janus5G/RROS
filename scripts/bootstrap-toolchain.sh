#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLCHAIN="$ROOT/.toolchain/llvm"
BIN="$TOOLCHAIN/usr/lib/llvm-21/bin"
LIBS="$TOOLCHAIN/usr/lib/x86_64-linux-gnu:$TOOLCHAIN/usr/lib/llvm-21/lib"

if [ -x "$BIN/clang" ] && [ -x "$BIN/ld.lld" ]; then
    PATH="$BIN:$PATH" LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$BIN/clang" --version | grep -q '21\.1\.8'
    PATH="$BIN:$PATH" LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        "$BIN/ld.lld" --version | grep -q '21\.1\.8'
    exit 0
fi

command -v apt-get >/dev/null 2>&1 || {
    echo "apt-get is required to fetch the pinned Ubuntu LLVM packages." >&2
    exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
    echo "dpkg-deb is required to extract LLVM packages." >&2
    exit 1
}

mkdir -p "$ROOT/.toolchain/packages" "$TOOLCHAIN"
cd "$ROOT/.toolchain/packages"
apt-get download \
    clang-21=1:21.1.8-6ubuntu1 \
    lld-21=1:21.1.8-6ubuntu1 \
    llvm-21-linker-tools=1:21.1.8-6ubuntu1 \
    libllvm21=1:21.1.8-6ubuntu1 \
    libclang-cpp21=1:21.1.8-6ubuntu1 \
    libclang1-21=1:21.1.8-6ubuntu1 \
    libclang-common-21-dev=1:21.1.8-6ubuntu1

for package in ./*.deb; do
    dpkg-deb -x "$package" "$TOOLCHAIN"
done

export PATH="$BIN:$PATH"
export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$BIN/clang" --version | grep '21\.1\.8'
"$BIN/ld.lld" --version | grep '21\.1\.8'

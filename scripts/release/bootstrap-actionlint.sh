#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/.toolchain/actionlint"
mkdir -p "$OUT"
if [ -x "$OUT/actionlint" ]; then
    "$OUT/actionlint" -version | grep -q '^1.7.7$'
    exit 0
fi
curl --fail --location --retry 3 \
    --output "$OUT/actionlint_1.7.7_linux_amd64.tar.gz" \
    https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_linux_amd64.tar.gz
(
    cd "$OUT"
    echo '023070a287cd8cccd71515fedc843f1985bf96c436b7effaecce67290e7e0757  actionlint_1.7.7_linux_amd64.tar.gz' | sha256sum -c -
    tar -xzf actionlint_1.7.7_linux_amd64.tar.gz actionlint LICENSE.txt
)
"$OUT/actionlint" -version | grep '^1.7.7$'

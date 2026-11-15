#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g21"
mkdir -p "$BUILD"
SRC="$ROOT/kernel/secsvc.c $ROOT/kernel/secdeploy.c $ROOT/kernel/chacha20.c $ROOT/kernel/devpkg.c $ROOT/kernel/eosvm.c $ROOT/kernel/storage.c $ROOT/kernel/sha256.c $ROOT/kernel/hmac_sha256.c $ROOT/kernel/jsonrpc.c"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G21_integration_test.c" $SRC -o "$BUILD/integration-test-$mode"
    "$BUILD/integration-test-$mode"
done
echo "PASS G21-INTEGRATION-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

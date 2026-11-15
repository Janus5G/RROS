#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g16"
mkdir -p "$BUILD"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G16_compat_test.c" "$ROOT/kernel/sha256.c" "$ROOT/kernel/hmac_sha256.c" \
        "$ROOT/kernel/jsonrpc.c" "$ROOT/kernel/chroma_adapter.c" "$ROOT/kernel/compat_registry.c" \
        -o "$BUILD/compat-test-$mode"
    "$BUILD/compat-test-$mode" "$ROOT/tests/gates/fixtures/chromaneural"
done
echo "PASS G16-COMPAT-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

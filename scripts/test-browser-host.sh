#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g18"
mkdir -p "$BUILD"
SRC="$ROOT/kernel/browser_net.c $ROOT/kernel/browser_html.c $ROOT/kernel/browser_js.c $ROOT/kernel/browser_image.c $ROOT/kernel/browser.c $ROOT/kernel/docsvc.c $ROOT/kernel/storage.c $ROOT/kernel/sha256.c"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G18_browser_test.c" $SRC -o "$BUILD/browser-test-$mode"
    "$BUILD/browser-test-$mode"
done
echo "PASS G18-BROWSER-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

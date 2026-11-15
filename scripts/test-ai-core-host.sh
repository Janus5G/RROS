#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g13"
mkdir -p "$BUILD"
cc -std=c17 -O2 -Wall -Wextra -Werror \
    "$ROOT/tests/gates/G13_ai_core_test.c" "$ROOT/kernel/ai_core.c" \
    -o "$BUILD/ai-core-test"
"$BUILD/ai-core-test"

cc -std=c17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$ROOT/tests/gates/G13_ai_core_test.c" "$ROOT/kernel/ai_core.c" \
    -o "$BUILD/ai-core-test-sanitized"
"$BUILD/ai-core-test-sanitized"
echo "PASS G13-AI-CORE-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror \
        -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G13_worker_test.c" "$ROOT/kernel/ai_selftest.c" \
        "$ROOT/kernel/ai_worker.c" "$ROOT/kernel/ai_core.c" \
        "$ROOT/kernel/process.c" "$ROOT/kernel/memory.c" \
        -o "$BUILD/ai-worker-test-$mode"
    "$BUILD/ai-worker-test-$mode"
done
echo "PASS G13-WORKER-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

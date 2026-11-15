#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g15"
mkdir -p "$BUILD"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G15_roboforge_test.c" "$ROOT/kernel/sha256.c" \
        "$ROOT/kernel/jsonrpc.c" "$ROOT/kernel/mcp_client.c" "$ROOT/kernel/mcp_server.c" \
        "$ROOT/kernel/mcp_registry.c" "$ROOT/kernel/roboforge_adapter.c" \
        -o "$BUILD/roboforge-test-$mode"
    "$BUILD/roboforge-test-$mode" "$ROOT/tests/gates/fixtures/roboforge"
done
echo "PASS G15-ROBOFORGE-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"
cc -std=c17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -I "$ROOT/kernel" \
    "$ROOT/tests/gates/G15_roboforge_session.c" "$ROOT/kernel/sha256.c" \
    "$ROOT/kernel/jsonrpc.c" "$ROOT/kernel/mcp_client.c" "$ROOT/kernel/mcp_server.c" \
    "$ROOT/kernel/mcp_registry.c" "$ROOT/kernel/roboforge_adapter.c" \
    -o "$BUILD/roboforge-session"
echo "PASS G15-SESSION-BUILT: live session harness compiled with sanitizers"

#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g14"
mkdir -p "$BUILD"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G14_mcp_test.c" "$ROOT/kernel/sha256.c" \
        "$ROOT/kernel/jsonrpc.c" "$ROOT/kernel/mcp_registry.c" \
        -o "$BUILD/mcp-test-$mode"
    "$BUILD/mcp-test-$mode"
done
echo "PASS G14-MCP-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G14_mcp_integration_test.c" "$ROOT/kernel/sha256.c" \
        "$ROOT/kernel/jsonrpc.c" "$ROOT/kernel/mcp_registry.c" "$ROOT/kernel/mcp_server.c" \
        "$ROOT/kernel/mcp_client.c" "$ROOT/kernel/mcp_deploy.c" \
        -o "$BUILD/mcp-integration-$mode"
    "$BUILD/mcp-integration-$mode"
done
echo "PASS G14-MCP-INTEGRATION-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"

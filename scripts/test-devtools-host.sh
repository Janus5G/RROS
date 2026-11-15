#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build/g17"
mkdir -p "$BUILD"
SRC="$ROOT/kernel/eosvm.c $ROOT/kernel/editor.c $ROOT/kernel/devpkg.c $ROOT/kernel/runtime_iface.c $ROOT/kernel/storage.c $ROOT/kernel/sha256.c $ROOT/kernel/hmac_sha256.c $ROOT/kernel/jsonrpc.c"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G17_dev_test.c" $SRC -o "$BUILD/dev-test-$mode"
    "$BUILD/dev-test-$mode" "$ROOT"
done
echo "PASS G17-DEV-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G17_integration_test.c" $SRC \
        "$ROOT/kernel/mcp_registry.c" "$ROOT/kernel/mcp_server.c" "$ROOT/kernel/mcp_client.c" "$ROOT/kernel/mcp_deploy.c" \
        -o "$BUILD/integration-$mode"
    "$BUILD/integration-$mode" "$ROOT"
done
echo "PASS G17-INTEGRATION-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"
mkdir -p "$BUILD/py"
python3 "$ROOT/tools/eospkg.py" assemble "$ROOT/sdk/examples/fib.eos" "$BUILD/py/fib.bin"
python3 "$ROOT/tools/eospkg.py" package "$ROOT/sdk/examples/fib.eos" "$BUILD/py/fib.epk" --id fib --version 1 --arch aarch64 --key-hex 09080706050403020100010203040506 --slot 1
python3 "$ROOT/tools/eospkg.py" inspect "$BUILD/py/fib.epk" > /dev/null
echo "PASS G17-PYTHON-TOOL-BUILDS-ARTIFACTS"
for mode in plain sanitized; do
    if [ "$mode" = plain ]; then
        FLAGS="-O2"
    else
        FLAGS="-O1 -g -fsanitize=address,undefined"
    fi
    cc -std=c17 $FLAGS -Wall -Wextra -Werror -I "$ROOT/boot/uefi" -I "$ROOT/kernel" \
        "$ROOT/tests/gates/G17_crosscheck_test.c" $SRC -o "$BUILD/xcheck-$mode"
    "$BUILD/xcheck-$mode" "$ROOT" "$BUILD/py"
done
echo "PASS G17-XCHECK-SANITIZERS: AddressSanitizer and UndefinedBehaviorSanitizer"
CLANG="$ROOT/.toolchain/llvm/usr/lib/llvm-21/bin/clang"
if [ -x "$CLANG" ]; then
    "$CLANG" --target=aarch64-none-elf -std=c17 -ffreestanding -Wall -Wextra -Werror \
        -I "$ROOT/sdk/include" -I "$ROOT/boot/uefi" -c "$ROOT/sdk/examples/hello.c" -o "$BUILD/hello-aarch64.o"
    "$CLANG" --target=x86_64-none-elf -std=c17 -ffreestanding -Wall -Wextra -Werror \
        -I "$ROOT/sdk/include" -I "$ROOT/boot/uefi" -c "$ROOT/sdk/examples/hello.c" -o "$BUILD/hello-x86_64.o"
    python3 - "$ROOT" "$BUILD" <<'EOF'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1] + "/tools")
import eospkg
b = Path(sys.argv[2])
assert eospkg.elf_machine(b / "hello-aarch64.o") == "aarch64"
assert eospkg.elf_machine(b / "hello-x86_64.o") == "x86_64"
EOF
    echo "PASS G17-NATIVE-AARCH64-BUILD-AND-COMPAT-INSPECTION: clang AArch64 object inspected as e_machine 183"
else
    echo "SKIP G17-NATIVE-AARCH64-BUILD: toolchain unavailable (NOT RUN)"
fi
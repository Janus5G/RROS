#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/build/community-checks"
mkdir -p "$OUT"
for variant in plain sanitized thread; do
    flags="-O2"
    if [ "$variant" = sanitized ]; then
        flags="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"
    elif [ "$variant" = thread ]; then
        flags="-O1 -g -fsanitize=thread"
    fi
    # shellcheck disable=SC2086
    cc -std=c17 -Wall -Wextra -Werror -pthread -fno-builtin \
        -ffunction-sections -fdata-sections -Wl,--gc-sections $flags \
        -I "$ROOT/boot/uefi" -I "$ROOT/boot/aarch64" -I "$ROOT/kernel" \
        "$ROOT/tests/unit/safety_blockers_test.c" \
        "$ROOT/boot/aarch64/pi5_probe_support.c" "$ROOT/kernel/memory.c" \
        "$ROOT/kernel/aarch64_mmu_runtime.c" "$ROOT/kernel/aarch64_mmu.c" \
        -o "$OUT/safety-$variant"
    for finding in uart map ownership bounds; do
        log="$OUT/safety-$variant-$finding.log"
        if ! "$OUT/safety-$variant" "$finding" > "$log" 2>&1; then
            cat "$log"
            exit 1
        fi
        cat "$log"
    done
done

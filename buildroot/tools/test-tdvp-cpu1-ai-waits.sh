#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
tests="$project/buildroot/tools/tests"
bsp="${1:?usage: test-tdvp-cpu1-ai-waits.sh /path/to/pristine/pinned/maix3}"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
mkdir -p "$scratch/bsp/drivers/interdrv/gnne" "$scratch/bsp/drivers/interdrv/hardlock"
cp "$bsp/drivers/interdrv/gnne/gnne_dev.c" "$bsp/drivers/interdrv/gnne/ai2d_dev.c" "$scratch/bsp/drivers/interdrv/gnne/"
cp "$bsp/drivers/interdrv/hardlock/drv_hardlock.c" "$scratch/bsp/drivers/interdrv/hardlock/"
sed -i 's/\r$//' "$scratch/bsp/drivers/interdrv/gnne/"*.c "$scratch/bsp/drivers/interdrv/hardlock/"*.c
for patch_file in 0003-rtsmart-ai-initialization-errors.patch 0004-rtsmart-ai-honor-poll-and-lock-bounds.patch; do
    patch --batch --fuzz=0 -d "$scratch/bsp" -p1 < "$vision/$patch_file"
done
# Execute bodies from the actual patched driver, not rewritten fixture logic.
for symbol in gnne_device_ioctl gnne_device_poll; do
    awk -v symbol="$symbol" '$0 ~ "^(static )?int " symbol "\\(" {copy=1} copy {print} copy && /^}/ {exit}' \
        "$scratch/bsp/drivers/interdrv/gnne/gnne_dev.c" > "$scratch/$symbol.inc"
    test -s "$scratch/$symbol.inc"
done
awk '/^int ai_2d_device_poll\(/ {copy=1} copy {print} copy && /^}/ {exit}' \
    "$scratch/bsp/drivers/interdrv/gnne/ai2d_dev.c" > "$scratch/ai_2d_device_poll.inc"
for driver in gnne ai2d; do
    awk '/^static void irq_callback\(/ {copy=1} copy {print} copy && /^}/ {exit}' \
        "$scratch/bsp/drivers/interdrv/gnne/${driver}_dev.c" > "$scratch/$driver-irq.inc"
    test -s "$scratch/$driver-irq.inc"
done
printf '/* RT typedefs are declared by the test harness. */\n' > "$scratch/rtdef.h"
"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-variable \
    -UNDEBUG -DRT_USING_TDVP_CPU1_VISION -I"$scratch" -I"$bsp/drivers/interdrv/hardlock" \
    "$tests/tdvp-cpu1-ai-waits-test.c" -o "$scratch/waits"
"$scratch/waits"

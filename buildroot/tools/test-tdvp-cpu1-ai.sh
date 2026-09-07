#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
tests="$project/buildroot/tools/tests"
bsp="${1:?usage: test-tdvp-cpu1-ai.sh /path/to/pinned/maix3 /path/to/pinned/mpp}"
mpp="${2:?pinned MPP source required}"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
for header in rtthread rtdef rthw dfs_posix lwp_user_mm ioremap riscv_io; do
    printf '#include "tdvp-cpu1-ai-mock.h"\n' > "$test_dir/$header.h"
done
# Use the actual pinned BSP address constants and MPP ioctl types. Do not
# silently substitute a synthetic ABI if the SDK headers move or disappear.
grep -E '^#define[[:space:]]+(KPU|FFT|AI2D|MAILBOX)_(BASE_ADDR|IO_SIZE)[[:space:]]' \
    "$bsp/board/board.h" > "$test_dir/board.h"
test "$(wc -l < "$test_dir/board.h")" -eq 8
cc=("${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -O2 -UNDEBUG
    -I"$test_dir" -I"$tests" -I"$vision" -I"$mpp/include" -I"$mpp/include/ioctl"
    -I"$bsp/drivers/interdrv/sysctl/sysctl_reset"
    -I"$bsp/drivers/interdrv/sysctl/sysctl_power"
    -I"$bsp/drivers/interdrv/hardlock"
    -I"$mpp/kernel/mediafreq/src/sysctl/sysctl_media_clock")
"${cc[@]}" "$vision/tdvp_cpu1_ai_clock.c" "$tests/tdvp-cpu1-ai-clock-test.c" -o "$test_dir/clock"
"${cc[@]}" "$vision/tdvp_cpu1_ai_init.c" "$tests/tdvp-cpu1-ai-init-test.c" -o "$test_dir/init"
"${cc[@]}" "$tests/tdvp-cpu1-fft-test.c" -o "$test_dir/fft"
mkdir -p "$test_dir/bsp/drivers/interdrv/gnne" "$test_dir/bsp/drivers/interdrv/hardlock"
cp "$bsp/drivers/interdrv/gnne/gnne_dev.c" "$bsp/drivers/interdrv/gnne/ai2d_dev.c" "$test_dir/bsp/drivers/interdrv/gnne/"
cp "$bsp/drivers/interdrv/hardlock/drv_hardlock.c" "$test_dir/bsp/drivers/interdrv/hardlock/"
# Windows read-only checkouts may contain CRLF; normalize only test copies.
sed -i 's/\r$//' "$test_dir/bsp/drivers/interdrv/gnne/gnne_dev.c" \
    "$test_dir/bsp/drivers/interdrv/gnne/ai2d_dev.c" "$test_dir/bsp/drivers/interdrv/hardlock/drv_hardlock.c"
patch --batch --fuzz=0 -d "$test_dir/bsp" -p1 < "$vision/0003-rtsmart-ai-initialization-errors.patch"
awk '/^int gnne_device_init\(void\)/ {copy=1} copy {print} copy && /^}/ {exit}' \
    "$test_dir/bsp/drivers/interdrv/gnne/gnne_dev.c" > "$test_dir/gnne-init.inc"
awk '/^int ai_2d_device_init\(void\)/ {copy=1} copy {print} copy && /^}/ {exit}' \
    "$test_dir/bsp/drivers/interdrv/gnne/ai2d_dev.c" > "$test_dir/ai2d-init.inc"
awk '/^int tdvp_cpu1_hardlock_ready\(void\)/ {copy=1} /^INIT_BOARD_EXPORT/ {exit} copy {print}' \
    "$test_dir/bsp/drivers/interdrv/hardlock/drv_hardlock.c" > "$test_dir/hardlock-init.inc"
"${cc[@]}" -DRT_USING_TDVP_CPU1_VISION "$tests/tdvp-cpu1-ai-drivers-test.c" -o "$test_dir/drivers"
for scenario in {0..21}; do "$test_dir/clock" "$scenario"; done
for scenario in {0..4}; do "$test_dir/init" "$scenario"; done
for scenario in {0..17}; do "$test_dir/fft" "$scenario"; done
for scenario in {0..8}; do "$test_dir/drivers" "$scenario"; done
echo 'CPU1 AI: PASS bounded startup/FFT PIO fault tests with pinned headers; not hardware or model acceptance'

#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
bsp="${1:?usage: test-tdvp-cpu1-camera-clock.sh /path/to/pinned/maix3 /path/to/pinned/mpp}"
mpp="${2:?pinned MPP source required}"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
printf '#define RT_ENOMEM 5\n#define RT_EIO 8\n#define RT_EBUSY 7\n#define RT_EINVAL 10\n#define RT_NULL ((void *)0)\nint rt_kprintf(const char *, ...);\n' > "$test_dir/rtthread.h"
printf 'void *rt_ioremap(void *, unsigned long);\nvoid rt_iounmap(void *);\n' > "$test_dir/ioremap.h"
printf '#include <stdint.h>\nuint32_t readl(const volatile void *);\nvoid writel(uint32_t, volatile void *);\n' > "$test_dir/riscv_io.h"
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -O2 -UNDEBUG \
    -I"$test_dir" -I"$bsp/drivers/interdrv/sysctl/sysctl_boot" \
    -I"$bsp/drivers/interdrv/sysctl/sysctl_power" \
    -I"$mpp/kernel/mediafreq/src/sysctl/sysctl_media_clock" \
    "$vision/tdvp_cpu1_camera_clock.c" \
    "$project/buildroot/tools/tests/tdvp-cpu1-camera-clock-test.c" -o "$test_dir/check"
for scenario in {0..21}; do "$test_dir/check" "$scenario"; done
echo 'CPU1 camera clock: PASS 22 cases, pinned register layouts, no shared PLL/power/DDR/display writes'

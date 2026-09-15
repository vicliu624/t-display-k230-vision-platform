#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
printf '#define RT_EOK 0\n#define RT_ENOMEM 5\n#define RT_EIO 8\n#define RT_EBUSY 7\nint rt_kprintf(const char *, ...);\n' > "$test_dir/rtthread.h"
printf 'void *rt_ioremap(void *, unsigned long);\nvoid rt_iounmap(void *);\n' > "$test_dir/ioremap.h"
printf '#include <stdint.h>\nuint32_t readl(const volatile void *);\nvoid writel(uint32_t, volatile void *);\n' > "$test_dir/riscv_io.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -UNDEBUG -I"$test_dir" \
    "$source_dir/tdvp_cpu1_vision_pins.c" "$project/buildroot/tools/tests/tdvp-cpu1-vision-pins-test.c" \
    -o "$test_dir/check"
"$test_dir/check"

#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
bsp="${1:?usage: test-tdvp-cpu1-i2c4-early.sh /path/to/pinned/maix3}"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/drivers/interdrv/i2c" "$test_dir/include"
cp "$bsp/Kconfig" "$test_dir/"
cp "$bsp/drivers/interdrv/i2c/drv_i2c.c" "$test_dir/drivers/interdrv/i2c/"
patch --batch --fuzz=0 -d "$test_dir" -p1 < "$vision/0001-rtsmart-i2c4-early-clock.patch"
python3 - "$bsp" "$test_dir" <<'PY'
import re
import sys
from pathlib import Path
bsp, root = map(Path, sys.argv[1:])
relative = 'drivers/interdrv/i2c/drv_i2c.c'
original = (bsp / relative).read_text()
patched = (root / relative).read_text()
pattern = r'^int rt_hw_i2c_init\(void\).*?^}'
entry = re.search(pattern, patched, re.M | re.S).group()
old_entry = re.search(pattern, original, re.M | re.S).group()
legacy = re.sub(r'#ifdef RT_USING_TDVP_CPU1_VISION.*?#else\n', '', entry, flags=re.S).replace('#endif\n', '')
assert legacy == old_entry, 'non-vision BOARD entry changed'
assert patched.index('#include "tdvp_cpu1_i2c4_board.h"') < patched.index('int rt_hw_i2c_init(void)')
assert 'INIT_BOARD_EXPORT(rt_hw_i2c_init);' in patched
(root / 'include/production-i2c-entry.h').write_text(entry + '\n')
PY
printf '#define RT_EOK 0\n#define RT_ENOMEM 5\n#define RT_EIO 8\n#define RT_EBUSY 7\n#define RT_EINVAL 10\n#define RT_ETIMEOUT 2\n#define RT_NULL ((void *)0)\ntypedef long rt_base_t;\nint rt_kprintf(const char *, ...);\n' > "$test_dir/include/rtthread.h"
printf 'long rt_hw_interrupt_disable(void);\nvoid rt_hw_interrupt_enable(long);\n' > "$test_dir/include/rthw.h"
printf 'void *rt_ioremap(void *, unsigned long);\nvoid rt_iounmap(void *);\n' > "$test_dir/include/ioremap.h"
printf '#include <stdint.h>\nuint32_t readl(const volatile void *);\nvoid writel(uint32_t, volatile void *);\n' > "$test_dir/include/riscv_io.h"
printf '#include <stdint.h>\nuint64_t tdvp_test_rdtime(void);\n#define rdtime() tdvp_test_rdtime()\n' > "$test_dir/include/encoding.h"
cp "$bsp/c908/tick.h" "$test_dir/include/tick.h"
flags=(-std=gnu11 -Wall -Wextra -Werror -O2 -UNDEBUG -DRT_USING_TDVP_CPU1_VISION -DRT_USING_I2C4
       -I"$test_dir/include" -I"$vision")
"${CC:-cc}" "${flags[@]}" "$vision/tdvp_cpu1_i2c4_clock.c" \
    "$project/buildroot/tools/tests/tdvp-cpu1-i2c4-early-test.c" -o "$test_dir/check"
for scenario in {0..26}; do "$test_dir/check" "$scenario"; done
for illegal in RT_USING_I2C0 RT_USING_I2C1 RT_USING_I2C2 RT_USING_I2C3 RT_USING_I2C4_SLAVE; do
    if "${CC:-cc}" "${flags[@]}" -D"$illegal" -c \
        "$project/buildroot/tools/tests/tdvp-cpu1-i2c4-early-test.c" -o "$test_dir/wrong.o" \
        > "$test_dir/rejected.log" 2>&1; then
        echo "FAIL invalid camera ownership compiled: $illegal" >&2; exit 1
    fi
    grep -Fq 'CPU1 vision must own only I2C4' "$test_dir/rejected.log"
done
echo 'CPU1 I2C4 early init: PASS actual BOARD hook, 27 lifecycle/clock cases, five ownership refusals and unchanged non-vision entry'

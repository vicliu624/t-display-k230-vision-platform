#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
bsp="${1:?usage: test-tdvp-cpu1-ownership.sh /path/to/pinned/maix3}"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/board" "$test_dir/drivers/interdrv/gnne" "$test_dir/include"
cp "$bsp/board/sdk_kernel_init.c" "$test_dir/board/"
cp "$bsp/drivers/interdrv/gnne/ai_module.c" "$test_dir/drivers/interdrv/gnne/"
patch --batch --fuzz=0 -d "$test_dir" -p1 < "$vision/0002-rtsmart-defer-vision-components.patch"
printf '#ifndef TEST_RTTHREAD_H\n#define TEST_RTTHREAD_H\n#define RT_EOK 0\n#define RT_ENOMEM 5\n#define RT_EIO 8\n#define RT_EBUSY 7\n#define RT_NULL ((void *)0)\ntypedef int rt_int32_t;\nint rt_kprintf(const char *, ...);\nvoid rt_thread_mdelay(int);\n#define INIT_COMPONENT_EXPORT(fn) int (*tdvp_test_init_##fn)(void) = fn\n#endif\n' > "$test_dir/include/rtthread.h"
printf '#include <rtthread.h>\n' > "$test_dir/include/rtdevice.h"
printf '#include <rtthread.h>\n' > "$test_dir/include/rthw.h"
printf 'void *rt_ioremap(void *, unsigned long);\nvoid rt_iounmap(void *);\n' > "$test_dir/include/ioremap.h"
printf '#include <stdint.h>\nuint64_t tdvp_test_rdtime(void);\n#define rdtime() tdvp_test_rdtime()\n' > "$test_dir/include/encoding.h"
cp "$bsp/c908/tick.h" "$test_dir/include/"
flags=(-std=gnu11 -O2 -Wall -Wextra -Werror -UNDEBUG -I"$test_dir/include" -I"$vision")
"${CC:-cc}" "${flags[@]}" "$vision/tdvp_vision_owner.c" \
    "$project/buildroot/tools/tests/tdvp-cpu1-owner-test.c" -o "$test_dir/policy"
"$test_dir/policy"
"${CC:-cc}" "${flags[@]}" -DRT_USING_TDVP_CPU1_VISION \
    "$vision/tdvp_vision_owner.c" "$vision/tdvp_cpu1_vision_startup.c" \
    "$project/buildroot/tools/tests/tdvp-cpu1-startup-test.c" -o "$test_dir/startup"
for scenario in 0 1 2 3 4 5 6 7 8 9 10 12; do "$test_dir/startup" "$scenario"; done
"${CC:-cc}" "${flags[@]}" -DRT_USING_MPP -DRT_USING_TDVP_CPU1_VISION \
    -c "$test_dir/board/sdk_kernel_init.c" -o "$test_dir/sdk.o"
"${CC:-cc}" "${flags[@]}" -DRT_USING_TDVP_CPU1_VISION \
    -c "$test_dir/drivers/interdrv/gnne/ai_module.c" -o "$test_dir/ai.o"
if nm "$test_dir/sdk.o" "$test_dir/ai.o" | grep -Eq ' (mpp_init|gnne_device_init|ai_2d_device_init|ai_module_init)$'; then
    echo 'FAIL automatic media/AI initialization survived' >&2; exit 1
fi
python3 - "$bsp" "$test_dir" <<'PY'
import sys
from pathlib import Path
base, patched = map(Path, sys.argv[1:])
p = 'board/sdk_kernel_init.c'
assert (patched/p).read_text().replace('#if defined(RT_USING_MPP) && !defined(RT_USING_TDVP_CPU1_VISION)', '#ifdef RT_USING_MPP') == (base/p).read_text()
p = 'drivers/interdrv/gnne/ai_module.c'
assert (patched/p).read_text().replace('#ifndef RT_USING_TDVP_CPU1_VISION\n', '').removesuffix('\n#endif\n') == (base/p).read_text()
print('CPU1 component deferral: PASS actual pinned sources, unchanged unselected paths')
PY
echo 'CPU1 ownership: PASS protocol, production startup and real component deferral; not Linux resource preparation or hardware acceptance'

#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/linux"
for header in types clk device err ktime of_address of_reserved_mem pm_domain pm_runtime random io; do
    printf '#include "tdvp-cpu1-linux-owner-mock.h"\n' > "$test_dir/linux/$header.h"
done
"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -UNDEBUG -D__KERNEL__ \
    -I"$test_dir" -I"$project/buildroot/tools/tests" -I"$vision" \
    "$project/buildroot/tools/tests/tdvp-cpu1-linux-owner-test.c" \
    "$vision/tdvp_vision_owner.c" -o "$test_dir/owner"
"$test_dir/owner"
python3 - "$vision" <<'PY'
from pathlib import Path
import sys
v = Path(sys.argv[1])
s = (v / 'linux/tdvp_cpu1_vision_main.c').read_text()
release = s.split('static int vision_release(', 1)[1].split('static const struct file_operations', 1)[0]
assert 'cancel_delayed_work' not in release
assert 'tdvp_linux_owner_abort' not in release
tick = s.split('static void vision_tick(', 1)[1].split('static int vision_open(', 1)[0]
assert tick.index('tdvp_linux_owner_poll') < tick.index('if (vision->opened)')
assert '.suppress_bind_attrs = true' in s and '.remove =' not in s
probe = s.split('static int vision_probe(', 1)[1].split('static int vision_pm_prepare(', 1)[0]
assert probe.index('tdvp_linux_owner_prepare') < probe.index('misc_register') < probe.index('__module_get') < probe.index('tdvp_linux_owner_start')
assert 'producer->epoch) != vision->owner.session.observed_cookie' in s
assert 'epoch = owner_cookie;' in (v / 'tdvp_cpu1_vision_worker.c').read_text()
print('Linux bridge lifecycle: PASS source guards; runtime FD/PM behavior still needs paired hardware tests')
PY

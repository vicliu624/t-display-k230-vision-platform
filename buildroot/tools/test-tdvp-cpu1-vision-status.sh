#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
hardware="$project/user-space/vicliu-pocket-linux-hardware/src/hardware"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -UNDEBUG -I"$vision" -I"$vision/linux" \
    "$vision/linux/tdvp_vision_observer.c" "$project/buildroot/tools/tests/tdvp-vision-observer-test.c" \
    -o "$temporary/observer"
"$temporary/observer"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -UNDEBUG -I"$hardware" \
    "$hardware/cpu1_vision_status.cpp" "$project/buildroot/tests/tdvp-cpu1-vision-status-test.cpp" \
    -o "$temporary/status"
"$temporary/status"
python3 - "$vision/linux/tdvp_cpu1_vision_main.c" "$vision/tdvp_cpu1_vision_startup.c" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
status = source.split('static ssize_t status_show(', 1)[1].split('static DEVICE_ATTR_RO(status);', 1)[0]
assert 'mutex_trylock' in status and 'sysfs_emit' in status
for forbidden in ('tdvp_linux_owner_poll', 'tdvp_linux_owner_start', 'writeq(', 'writel(', 'vision_open(', 'vision_refresh('):
    assert forbidden not in status, forbidden
assert 'vision->misc.groups = vision_groups;' in source
for field in ('startup_trace_version=', 'startup_stage=', 'startup_result='):
    assert field in status, field
startup = Path(sys.argv[2]).read_text()
trace = startup.split('void tdvp_cpu1_startup_trace(', 1)[1].split('static tdvp_v_u64 owner_now(', 1)[0]
for forbidden in ('owner_poll(', 'owner_now(', 'rdtime(', 'rt_kprintf(', 'rt_ioremap(', 'rt_thread_', 'last_publication_ms ='):
    assert forbidden not in trace, forbidden
assert 'tdvp_owner_publish(&wire->cpu1_side, &owner.own)' in trace
assert 'TDVP_OWNER_STARTING' in trace and 'TDVP_OWNER_READY' in trace
print('CPU1 status source contract: PASS read-only sysfs callback; not runtime/hardware acceptance')
PY

# Keep the early behavioral test tied to the production post-image assertions.
# Inspect production code alone so assertion strings in the test cannot mask
# a missing runtime marker or an obsolete image requirement.
bash "$project/buildroot/tools/test-tdvp-cpu1-hwctl-image-contract.sh"

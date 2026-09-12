#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
kernel="${1:?usage: test-tdvp-cpu1-clock-amp.sh /path/to/pinned/linux [--patched]}"
mode="${2:-pristine}"
[[ $# -le 2 && ( "$mode" == pristine || "$mode" == --patched ) ]] || exit 2
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/drivers/clk"
cp "$kernel/drivers/clk/clk-k230.c" "$test_dir/drivers/clk/"
clock_patch="$project/buildroot/k230-sdk-overlay/linux/0069-tdvp-clock-cpu1-i2c4-arbitration.patch"
awk '/^diff --git / { keep = ($0 == "diff --git a/drivers/clk/clk-k230.c b/drivers/clk/clk-k230.c") } keep' \
    "$project/buildroot/k230-sdk-overlay/linux/0070-tdvp-cpu1-runtime-supplier-readiness.patch" > "$test_dir/readiness.patch"
if [[ "$mode" == --patched ]]; then
    cp "$kernel/drivers/clk/clk-k230-tdvp-amp.h" "$test_dir/drivers/clk/"
    mkdir -p "$test_dir/baseline/drivers/clk"
    cp "$test_dir/drivers/clk/"* "$test_dir/baseline/drivers/clk/"
    patch --batch --fuzz=0 --reverse -d "$test_dir/baseline" -p1 < "$test_dir/readiness.patch"
    patch --batch --fuzz=0 --reverse --dry-run -d "$test_dir/baseline" -p1 < "$clock_patch"
else
    patch --batch --fuzz=0 -d "$test_dir" -p1 < "$clock_patch"
    patch --batch --fuzz=0 -d "$test_dir" -p1 < "$test_dir/readiness.patch"
fi
# Compile verbatim production struct, helper and affected CCF operations.
# Only the Linux service layer/MMIO is mocked, not a copy of the algorithm.
python3 - "$test_dir" <<'PY'
import re
import sys
from pathlib import Path
root = Path(sys.argv[1])
source = (root / 'drivers/clk/clk-k230.c').read_text()
parts = re.findall(r'^#define K230_CLK_\w+_OFFSET .*$', source, re.M)
parts.append(re.search(r'^struct k230_clk_composite \{.*?^\};', source, re.M | re.S).group())
parts.append('#define to_k230_clk_composite(hwptr) container_of(hwptr, struct k230_clk_composite, hw)')
parts.append('#include "drivers/clk/clk-k230-tdvp-amp.h"')
parts.append(re.search(r'^bool k230_tdvp_clock_ready\(struct device_node \*parent\)\n\{.*?^\}', source, re.M | re.S).group())
for name in ('k230_clk_find_approximate', 'k230_clk_composite_set_rate',
             'k230_clk_composite_enable', 'k230_clk_composite_disable'):
    match = re.search(r'^static [^\n]*\b' + name + r'\(.*?^\}', source, re.M | re.S)
    assert match, name
    parts.append(match.group())
setup = source.index('if (k230_clk_amp_setup(k230_clk_composite, parent_sys_clk))')
assert setup < source.index('if (clk_hw_register(NULL, hw))')
cleanup = source[source.index('free_k230_clk_composite:'):source.index('static int canaan_k230_clk_composite_probe')]
assert 'iounmap(k230_clk_composite->amp_lock_reg)' in cleanup
(root / 'production-clock-ops.h').write_text('\n\n'.join(parts) + '\n')
PY
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wno-sign-compare -O2 -UNDEBUG \
    -pthread -I"$test_dir" "$project/buildroot/tools/tests/tdvp-cpu1-clock-amp-test.c" -o "$test_dir/check"
"$test_dir/check"

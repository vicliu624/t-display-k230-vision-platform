#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
kernel="${1:?usage: test-tdvp-cpu1-power-amp.sh /path/to/pinned/linux [--patched]}"
mode="${2:-pristine}"
[[ $# -le 2 && ( "$mode" == pristine || "$mode" == --patched ) ]] || exit 2
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
mkdir -p "$test_dir/drivers/soc/canaan" "$test_dir/linux" "$test_dir/dt-bindings/soc"
cp "$kernel/drivers/soc/canaan/k230-power-domains.c" "$test_dir/drivers/soc/canaan/"
cp "$kernel/include/dt-bindings/soc/canaan,k230_pm_domains.h" "$test_dir/dt-bindings/soc/"
power_patch="$project/buildroot/k230-sdk-overlay/linux/0068-tdvp-power-retain-cpu1-vision-domains.patch"
if [[ "$mode" == --patched ]]; then
    # CI supplies linux-patch output. Verify that it really contains the patch,
    # but execute it as built; do not revert the source under test.
    patch --batch --fuzz=0 --reverse --dry-run -d "$test_dir" -p1 < "$power_patch"
else
    patch --batch --fuzz=0 -d "$test_dir" -p1 < "$power_patch"
fi
# Compile the complete patched production source; only the Linux/MMIO services
# are mocked. No separate copy of the policy or probe/error logic is tested.
for header in delay err io of pm_domain platform_device; do
    : > "$test_dir/linux/$header.h"
done
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -O2 -UNDEBUG -I"$test_dir" \
    "$project/buildroot/tools/tests/tdvp-cpu1-power-amp-test.c" -o "$test_dir/check"
"$test_dir/check"

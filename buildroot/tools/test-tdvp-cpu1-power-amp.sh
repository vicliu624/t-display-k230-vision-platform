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
awk '/^diff --git / { keep = ($0 == "diff --git a/drivers/soc/canaan/k230-power-domains.c b/drivers/soc/canaan/k230-power-domains.c") } keep' \
    "$project/buildroot/k230-sdk-overlay/linux/0070-tdvp-cpu1-runtime-supplier-readiness.patch" > "$test_dir/readiness.patch"
if [[ "$mode" == --patched ]]; then
    # CI supplies linux-patch output. Verify that it really contains the patch,
    # but execute it as built; do not revert the source under test.
    mkdir -p "$test_dir/baseline/drivers/soc/canaan"
    cp "$test_dir/drivers/soc/canaan/k230-power-domains.c" "$test_dir/baseline/drivers/soc/canaan/"
    patch --batch --fuzz=0 --reverse -d "$test_dir/baseline" -p1 < "$test_dir/readiness.patch"
    patch --batch --fuzz=0 --reverse --dry-run -d "$test_dir/baseline" -p1 < "$power_patch"
else
    patch --batch --fuzz=0 -d "$test_dir" -p1 < "$power_patch"
    patch --batch --fuzz=0 -d "$test_dir" -p1 < "$test_dir/readiness.patch"
fi
# Compile the complete patched production source; only the Linux/MMIO services
# are mocked. No separate copy of the policy or probe/error logic is tested.
for header in delay err io of pm_domain platform_device module; do
    : > "$test_dir/linux/$header.h"
done
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -O2 -UNDEBUG -I"$test_dir" \
    "$project/buildroot/tools/tests/tdvp-cpu1-power-amp-test.c" -o "$test_dir/check"
"$test_dir/check"

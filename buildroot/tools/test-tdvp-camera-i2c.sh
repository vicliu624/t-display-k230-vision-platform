#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source_dir="${PROJECT_DIR}/user-space/tdvp-camera-isp"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/tdvp-camera-i2c.XXXXXX")"
trap 'rm -rf -- "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -UNDEBUG \
    -I"$source_dir/src" "$source_dir/src/tdvp-gc2093-i2c.c" \
    "$source_dir/tests/gc2093-i2c-test.c" \
    -Wl,--wrap=realpath,--wrap=__realpath_chk,--wrap=open,--wrap=close,--wrap=ioctl \
    -o "$test_dir/gc2093-i2c-test"
"$test_dir/gc2093-i2c-test"

# Load the actual patched driver and adapter, not just a mock registration
# table. This catches missing dynamic exports such as vvcam_api_version.
vendor_dir="${PROJECT_DIR}/vendor/k230_linux_sdk/buildroot-overlay/package/vvcam"
mkdir -p "$test_dir/src" "$test_dir/mipi" "$test_dir/v4l2/isp"
cp "$vendor_dir/src/gc2093.c" "$vendor_dir/src/common.h" \
    "$vendor_dir/src/version.c" "$test_dir/src/"
cp "$vendor_dir/mipi/vvcam_mipi_driver.c" "$test_dir/mipi/"
cp "$vendor_dir/v4l2/isp/vvcam_isp_driver.c" \
    "$vendor_dir/v4l2/isp/vvcam_isp_driver.h" \
    "$vendor_dir/v4l2/isp/vvcam_isp_platform.c" "$test_dir/v4l2/isp/"
# Exercise the entire actual candidate queue against pinned source, including
# the kernel clock and completion patches, without building a kernel in CI.
bash "$SCRIPT_DIR/validate-k230-sdk-linux-patches.sh" "$source_dir/patches"
for patch_file in "$source_dir"/patches/*.patch; do
    patch --batch --fuzz=0 -d "$test_dir" -p1 < "$patch_file"
done
"${CC:-cc}" -std=gnu11 -Wall -Wextra -O2 -fPIC -shared \
    -I"$source_dir/src" -I"$vendor_dir/include" \
    "$source_dir/src/tdvp-gc2093-i2c.c" \
    "$source_dir/src/tdvp-vvcam-legacy-adapter.c" \
    "$test_dir/src/gc2093.c" "$test_dir/src/version.c" \
    -Wl,-soname,libvvcam.so -o "$test_dir/libvvcam.so"
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -O2 -UNDEBUG \
    -I"$source_dir/src" -I"$vendor_dir/include" \
    "$source_dir/tests/plugin-load-test.c" -Wl,--export-dynamic -ldl \
    -o "$test_dir/plugin-load-test"
"$test_dir/plugin-load-test" "$test_dir/libvvcam.so"

# Compile the real capture checker, but never open a host camera in CI.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 \
    "$source_dir/tests/v4l2-capture-check.c" -o "$test_dir/v4l2-capture-check"
capture_status=0
"$test_dir/v4l2-capture-check" /dev/null || capture_status=$?
if [ "$capture_status" -ne 1 ]; then
    echo "capture checker returned $capture_status for /dev/null, expected 1" >&2
    exit 1
fi
echo 'VVCAM capture checker: PASS host build and non-camera rejection (no capture claim)'

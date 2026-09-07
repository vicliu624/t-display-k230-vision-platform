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
mkdir -p "$test_dir/src"
cp "$vendor_dir/src/gc2093.c" "$vendor_dir/src/common.h" \
    "$vendor_dir/src/version.c" "$test_dir/src/"
patch --batch --fuzz=0 -d "$test_dir" -p1 \
    < "$source_dir/patches/0001-gc2093-use-verified-tdvp-i2c4.patch"
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

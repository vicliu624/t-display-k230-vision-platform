#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
source_dir="${PROJECT_DIR}/user-space/tdvp-camera-isp"
include_dir="${TDVP_VVCAM_SENSOR_INCLUDE:-${PROJECT_DIR}/vendor/k230_linux_sdk/buildroot-overlay/package/vvcam/include}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/tdvp-camera-legacy-abi.XXXXXX")"
trap 'rm -rf -- "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -UNDEBUG \
    -I"$include_dir" -I"$source_dir/src" \
    "$source_dir/src/tdvp-vvcam-legacy-adapter.c" \
    "$source_dir/tests/legacy-adapter-test.c" -o "$test_dir/legacy-adapter-test"
"$test_dir/legacy-adapter-test"

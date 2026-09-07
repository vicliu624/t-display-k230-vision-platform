#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <pinned-cpu1-mpp-source>" >&2
    exit 2
fi
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
mpp="$1"
test -s "$mpp/userapps/api/mpi_vicap_api.h"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
# Supply the sensor enum selection for a pristine clone. This is a mocked
# host test, not the generated/validated board firmware configuration.
printf '#define CONFIG_MPP_ENABLE_SENSOR_GC2093 1\n#define CONFIG_MPP_ENABLE_CSI_DEV_2 1\n' > "$test_dir/k_autoconf_comm.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 \
    -I"$source_dir" -I"$mpp/include" -I"$mpp/include/comm" -I"$mpp/userapps/api" -I"$test_dir" \
    "$source_dir/tdvp_cpu1_capture.c" "$project/buildroot/tools/tests/tdvp-cpu1-capture-test.c" \
    -o "$test_dir/check"
"$test_dir/check"

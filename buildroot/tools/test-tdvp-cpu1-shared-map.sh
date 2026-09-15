#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -I"$source_dir" \
    "$project/buildroot/tools/tests/tdvp-cpu1-shared-map-test.c" -o "$test_dir/check"
"$test_dir/check"
if grep -q 'kd_mpi_sys_mmap' "$source_dir/tdvp_cpu1_vision_worker.c"; then
    echo 'FAIL worker must not map non-MMZ reservations through MPI' >&2
    exit 1
fi
[ "$(grep -c '= tdvp_cpu1_shared_map(' "$source_dir/tdvp_cpu1_vision_worker.c")" -eq 3 ]
echo 'PASS worker uses the bounded shared mapper for all three ABI regions'

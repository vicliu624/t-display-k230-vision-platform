#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 -I"$source_dir" \
    "$source_dir/tdvp_cpu1_transport.c" "$project/buildroot/tools/tests/tdvp-cpu1-transport-test.c" \
    -o "$test_dir/check"
"$test_dir/check"

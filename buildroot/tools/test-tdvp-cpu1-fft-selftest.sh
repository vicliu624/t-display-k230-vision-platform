#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ] || { echo "usage: $0 <pinned-mpp-source>" >&2; exit 2; }
project="$(cd "$(dirname "$0")/../.." && pwd)"
mpp="$1"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$mpp/include" -I"$mpp/include/comm" -I"$mpp/include/ioctl" \
    -I"$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision" \
    "$project/buildroot/tools/tests/tdvp-cpu1-fft-selftest-test.c" -lm -o "$scratch/test"
"$scratch/test"

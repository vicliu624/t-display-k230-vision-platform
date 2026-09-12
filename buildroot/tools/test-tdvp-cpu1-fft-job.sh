#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ] || { echo "usage: $0 <pinned-mpp-source>" >&2; exit 2; }
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
mpp="$1"
scratch="$(mktemp -d)"
trap 'rm -rf -- "$scratch"' EXIT
# Compile the complete production function body, not a rewritten executor.
awk '/^void execute_fft\(\)/ {copy=1} copy {print} copy && /^}/ {exit}' \
    "$vision/tdvp_cpu1_ai_service.cpp" > "$scratch/execute_fft.inc"
test -s "$scratch/execute_fft.inc"
"${CXX:-c++}" -std=c++17 -O2 -UNDEBUG -Wall -Wextra -Werror \
    -I"$mpp/include" -I"$mpp/include/ioctl" -I"$vision" -I"$scratch" \
    "$project/buildroot/tools/tests/tdvp-cpu1-fft-job-executor-test.cpp" -o "$scratch/test"
"$scratch/test"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -I"$vision" \
    "$project/buildroot/tools/tdvp-cpu1-fft-job-probe.c" -o "$scratch/probe"
result=0
"$scratch/probe" unexpected > "$scratch/rejected.log" 2>&1 || result=$?
[ "$result" -eq 2 ]
echo 'FFT job probe: PASS host compile and CLI refusal (hardware numerical tests require /dev/tdvp-ai)'

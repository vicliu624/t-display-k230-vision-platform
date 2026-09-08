#!/usr/bin/env bash
set -euo pipefail
[ "$#" -le 1 ] || { echo "usage: $0 [pinned-nncase-riscv64-directory]" >&2; exit 2; }
root="$(cd "$(dirname "$0")/../.." && pwd)"
source="$root/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
work="$(mktemp -d)"
trap 'rm -f "$work/test" "$work/test-sanitized" "$work/layout"; rmdir "$work"' EXIT
args=(-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -I"$source"
    "$source/tdvp_cpu1_ai_guard.c" "$source/tdvp_cpu1_kpu_guard.c"
    "$root/buildroot/tools/tests/tdvp-cpu1-kpu-guard-test.c")
"${CC:-cc}" "${args[@]}" -O2 -o "$work/test"
"$work/test"
"${CC:-cc}" "${args[@]}" -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o "$work/test-sanitized"
ASAN_OPTIONS=detect_leaks=1 "$work/test-sanitized"
if [ "$#" -eq 1 ]; then
    grep -Fxq '#define NNCASE_VERSION "2.9.0"' "$1/nncase/include/nncase/version.h"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I"$source" -I"$1/nncase/include" \
        "$root/buildroot/tools/tests/tdvp-cpu1-kpu-register-layout.cpp" -o "$work/layout"
    "$work/layout"
fi
# Model loading and execution must use the same guarded service, not a demo
# that bypasses its deadline/owner checks. Actual call-site audit follows in CI.
grep -Fq 'request.operation != TDVP_AI_KPU' "$source/tdvp_cpu1_ai_service.cpp"
# Embedded ELF bytes must not use nncase's default virtual-address pinning.
# This policy assertion complements the hardware PC-range check and real model
# regression; it does not claim that a string match proves DMA correctness.
grep -Fq 'tdvp_cpu1_kws_model_start), model_bytes}, true)' "$source/tdvp_cpu1_ai_service.cpp"
grep -Fq -- '--wrap=gnne_init' "$source/build-capture-probe.sh"
grep -Fq -- '--wrap=gnne_enable' "$source/build-capture-probe.sh"
echo 'PASS KPU guard regression; hardware inference is a separate required test'

#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
printf '#define RT_EBUSY 7\nint rt_kprintf(const char *, ...);\n' > "$test_dir/rtthread.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 \
    -DCONFIG_MEM_MMZ_BASE=0x14000000UL -DCONFIG_MEM_MMZ_SIZE=0x08000000UL \
    -I"$test_dir" -I"$source_dir" "$source_dir/tdvp_cpu1_mpp_init.c" \
    "$project/buildroot/tools/tests/tdvp-cpu1-vision-init-test.c" -o "$test_dir/check"
for stage in 0 1 2 3 4 5 6 7 8 9 10; do
    "$test_dir/check" "$stage"
done
for wrong in base size; do
    base=0x14000000UL
    size=0x08000000UL
    case "$wrong" in
        base) base=0x10000000UL ;;
        size) size=0x10000000UL ;;
    esac
    if "${CC:-cc}" -std=c11 -DCONFIG_MEM_MMZ_BASE="$base" -DCONFIG_MEM_MMZ_SIZE="$size" \
        -I"$test_dir" -c "$source_dir/tdvp_cpu1_mpp_init.c" -o "$test_dir/wrong.o" \
        > "$test_dir/rejected.log" 2>&1; then
        echo "FAIL: unsafe MMZ $wrong compiled" >&2
        exit 1
    fi
    grep -Fq 'static assertion failed' "$test_dir/rejected.log"
done
# Undefined symbols, including initialization callbacks, must be exactly the
# expected camera subset. A newly added display/audio init cannot pass by
# accidentally acquiring a test stub.
echo 'CPU1 vision init: PASS success, early I2C4/camera-clock refusal, eight latched MPP failure paths and MMZ guards'

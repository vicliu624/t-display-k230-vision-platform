#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
vision="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
tests="$project/buildroot/tools/tests"
bsp="${1:?usage: test-tdvp-cpu1-mmio-unmap.sh /path/to/pinned/maix3}"
ioremap="$bsp/../../rt-thread/components/lwp/ioremap.c"
mmu="$bsp/c908/mmu.c"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
ulimit -c 0
# Extract the pinned implementation, not a handwritten equivalent that
# repeats the desired behavior. Missing/moved functions must fail the test.
awk '/^static void _iounmap_range\(/ {copy=1} copy {print} copy && /^}/ {exit}' "$ioremap" > "$temporary/sdk-iounmap-range.inc"
awk '/^void _rt_hw_mmu_unmap\(/ {copy=1} copy {print} copy && /^}/ {exit}' "$mmu" > "$temporary/sdk-mmu-unmap-range.inc"
awk '/^void rt_hw_mmu_unmap\(/ {copy=1} copy {print} copy && /^}/ {exit}' "$mmu" > "$temporary/sdk-mmu-unmap-wrapper.inc"
for excerpt in sdk-iounmap-range sdk-mmu-unmap-range sdk-mmu-unmap-wrapper; do
    test -s "$temporary/$excerpt.inc"
    grep -qx '}' "$temporary/$excerpt.inc"
done
grep -E '^#define[[:space:]]+PAGE_OFFSET_BIT[[:space:]]' "$bsp/c908/riscv_mmu.h" > "$temporary/sdk-page-size.inc"
for header in rtthread rthw ioremap riscv_io encoding; do
    printf '#include "tdvp-cpu1-mmio-mock.h"\n' > "$temporary/$header.h"
done
cp "$bsp/c908/tick.h" "$temporary/tick.h"
cc=("${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -O2 -UNDEBUG
    -I"$temporary" -I"$tests" -I"$vision")
"${cc[@]}" "$vision/tdvp_cpu1_i2c4_clock.c" "$vision/tdvp_cpu1_ai_clock.c" \
    "$tests/tdvp-cpu1-mmio-unmap-test.c" -o "$temporary/check"
for scenario in 0 1 2 3 5 6; do "$temporary/check" 1 "$scenario"; done
for scenario in {0..7}; do "$temporary/check" 2 "$scenario"; done
for mode in 3 4; do
    for neighbor in 0 1; do "$temporary/check" "$mode" "$neighbor"; done
done
# Reintroduce each original call shape in a throwaway source copy and prove
# the production-helper test fails; a standalone demonstration is not enough.
python3 - "$vision" "$temporary" <<'PY'
from pathlib import Path
import sys
source, temporary = map(Path, sys.argv[1:])
mutations = {
    'tdvp_cpu1_i2c4_clock.c': [
        ('rt_ioremap((void *)LOCK_PAGE_PHYS, 0x1000)', 'rt_ioremap((void *)(LOCK_PAGE_PHYS + 0xa0), 4)'),
        ('lock = lock_page + LOCK_OFFSET;', 'lock = lock_page;')],
    'tdvp_cpu1_ai_clock.c': [
        ('rt_ioremap((void *)0x91101000UL, 0x1000)', 'rt_ioremap((void *)0x91101014UL, 4)'),
        ('reset = reset_page + 0x14 / sizeof(uint32_t);', 'reset = reset_page;')]
}
for name, edits in mutations.items():
    text = (source / name).read_text()
    for before, after in edits:
        assert text.count(before) == 1, (name, before)
        text = text.replace(before, after)
    (temporary / name).write_text(text)
PY
for mode in 1 2; do
    i2c="$vision/tdvp_cpu1_i2c4_clock.c"; ai="$vision/tdvp_cpu1_ai_clock.c"
    if [ "$mode" = 1 ]; then i2c="$temporary/tdvp_cpu1_i2c4_clock.c"; else ai="$temporary/tdvp_cpu1_ai_clock.c"; fi
    "${cc[@]}" "$i2c" "$ai" "$tests/tdvp-cpu1-mmio-unmap-test.c" -o "$temporary/mutant"
    if "$temporary/mutant" "$mode" 0 > "$temporary/rejected-$mode.log" 2>&1; then
        echo "FAIL old unaligned helper $mode was accepted" >&2; exit 1
    fi
    grep -Fq 'MMU unmap reached an unowned adjacent page' "$temporary/rejected-$mode.log"
done
echo 'CPU1 MMIO unmap: PASS actual helpers with pinned SDK range logic, 14 lifecycle cases, four legacy demonstrations and two rejected production mutations; not hardware acceptance'

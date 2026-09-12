#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
kernel="${1:?usage: test-tdvp-cpu1-vision-dtb.sh /path/to/patched/linux}"
source_dir="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
dts_dir="$kernel/arch/riscv/boot/dts/canaan"
production=0
if grep -Fxq '#include "tdvp-cpu1-vision.dtsi"' "$dts_dir/k230-canmv-rm69a10.dts"; then
    production=1
    [ "$(grep -Fxc '#include "tdvp-cpu1-vision.dtsi"' "$dts_dir/k230-canmv-rm69a10.dts")" -eq 1 ]
    cmp "$source_dir/tdvp-cpu1-vision.dtsi" "$dts_dir/tdvp-cpu1-vision.dtsi"
    # Undo only the ownership include in a TEMPORARY baseline. The candidate
    # is the actual production DTS, not a substitute constructed by the test.
    sed '/^#include "tdvp-cpu1-vision.dtsi"$/d' "$dts_dir/k230-canmv-rm69a10.dts" > "$test_dir/baseline-source.dts"
fi
for variant in baseline candidate; do
    source="$dts_dir/k230-canmv-rm69a10.dts"
    if [[ "$production:$variant" == 1:baseline ]]; then
        source="$test_dir/baseline-source.dts"
    elif [[ "$production:$variant" == 0:candidate ]]; then
        source="$source_dir/k230-canmv-rm69a10-cpu1-vision.dts"
    fi
    "${CPP:-cpp}" -nostdinc -undef -D__DTS__ -x assembler-with-cpp -P \
        -I"$kernel/include" -I"$dts_dir" -I"$source_dir" "$source" > "$test_dir/$variant.dts"
    "${DTC:-dtc}" -q -I dts -O dtb -o "$test_dir/$variant.dtb" "$test_dir/$variant.dts"
done
python3 "$project/buildroot/tools/tests/tdvp-cpu1-vision-dtb-test.py" \
    "$test_dir/baseline.dtb" "$test_dir/candidate.dtb"
if [ -n "${2:-}" ]; then
    bash "$project/buildroot/tools/test-tdvp-cpu1-pair.sh" "$test_dir/baseline.dtb" "$test_dir/candidate.dtb" "$2"
else
    bash "$project/buildroot/tools/test-tdvp-cpu1-pair.sh" "$test_dir/baseline.dtb" "$test_dir/candidate.dtb"
fi

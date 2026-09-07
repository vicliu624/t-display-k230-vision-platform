#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
kernel="${1:?usage: test-tdvp-cpu1-vision-dtb.sh /path/to/patched/linux}"
source_dir="$project/buildroot/k230-sdk-overlay/board/tdvp/cpu1/vision"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
dts_dir="$kernel/arch/riscv/boot/dts/canaan"
for variant in baseline candidate; do
    source="$dts_dir/k230-canmv-rm69a10.dts"
    if [[ "$variant" == candidate ]]; then
        source="$source_dir/k230-canmv-rm69a10-cpu1-vision.dts"
    fi
    "${CPP:-cpp}" -nostdinc -undef -D__DTS__ -x assembler-with-cpp -P \
        -I"$kernel/include" -I"$dts_dir" -I"$source_dir" "$source" > "$test_dir/$variant.dts"
    "${DTC:-dtc}" -q -I dts -O dtb -o "$test_dir/$variant.dtb" "$test_dir/$variant.dts"
done
python3 "$project/buildroot/tools/tests/tdvp-cpu1-vision-dtb-test.py" \
    "$test_dir/baseline.dtb" "$test_dir/candidate.dtb"

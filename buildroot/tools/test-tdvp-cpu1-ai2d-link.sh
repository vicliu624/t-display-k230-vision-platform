#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 3 ] || { echo "usage: $0 <pinned-sdk> <musl-cross-prefix> <output-parent>" >&2; exit 2; }
script_dir="$(cd "$(dirname "$0")" && pwd)"
# Reuse the exact archive/revision/ABI pins, not a second relaxed check.
bash "$script_dir/test-tdvp-cpu1-nncase-link.sh" "$@"
sdk="$(cd "$1" && pwd)"; cross="$2"
runtime="$sdk/src/rtsmart/libs/nncase/riscv64"
mpp="$sdk/src/rtsmart/mpp"
source_dir="$script_dir/../k230-sdk-overlay/board/tdvp/cpu1/vision"
output="$(mktemp -d "$(cd "$3" && pwd)/ai2d-link.XXXXXX")"
echo "CPU1_AI2D_LINK_OUTPUT=$output"
flags=(-O2 -mcmodel=medany -march=rv64imafdcv -mabi=lp64d -I"$source_dir")
"${cross}gcc" -std=c11 -Wall -Wextra -Werror "${flags[@]}" \
    -c "$source_dir/tdvp_cpu1_ai_guard.c" -o "$output/guard.o"
"${cross}g++" -std=c++17 -DBUILDING_RUNTIME -Wall -Wextra "${flags[@]}" \
    -I"$runtime" -I"$runtime/nncase/include" \
    -c "$script_dir/tdvp-cpu1-ai2d-selftest.cpp" -o "$output/selftest.o"
"${cross}g++" -std=c++17 "${flags[@]}" \
    "$script_dir/tests/tdvp-cpu1-ai2d-link-probe.cpp" "$output/selftest.o" "$output/guard.o" \
    -T "$script_dir/tdvp-cpu1-nncase-tls.lds" \
    -T "$mpp/userapps/sample/linker_scripts/riscv64/link.lds" -n --static \
    -Wl,--wrap=open,--wrap=close,--wrap=poll -Wl,-Map,"$output/probe.map" -Wl,--start-group \
    -L"$runtime/nncase/lib" -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 \
    -L"$mpp/userapps/lib" -lsys -lpthread -lm -Wl,--end-group -o "$output/probe.elf"
"${cross}nm" -C "$output/probe.elf" > "$output/probe.symbols"
for symbol in __wrap_open __wrap_close __wrap_poll tdvp_cpu1_ai_guard_wait tdvp_cpu1_ai2d_selftest; do
    grep -Eq " T $symbol$" "$output/probe.symbols"
done
"${cross}readelf" -SW "$output/probe.elf" > "$output/probe.sections"
# There must be one collected TLS-BSS section, never overlapping orphans.
test "$(grep -Ec '\] \.tbss[[:space:]]' "$output/probe.sections")" -eq 1
if grep -Eq '\] \.tbss\.' "$output/probe.sections"; then
    echo 'FAIL orphan nncase TLS section' >&2; exit 1
fi
# Negative control: the original vendor script can link successfully while
# leaving overlapping TLS-BSS orphans. Never install or execute this mutant.
"${cross}g++" -std=c++17 "${flags[@]}" \
    "$script_dir/tests/tdvp-cpu1-ai2d-link-probe.cpp" "$output/selftest.o" "$output/guard.o" \
    -T "$mpp/userapps/sample/linker_scripts/riscv64/link.lds" -n --static \
    -Wl,--wrap=open,--wrap=close,--wrap=poll -Wl,--start-group \
    -L"$runtime/nncase/lib" -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 \
    -L"$mpp/userapps/lib" -lsys -lpthread -lm -Wl,--end-group -o "$output/old-tls-layout.elf" \
    > "$output/old-tls-layout.log" 2>&1
"${cross}readelf" -SW "$output/old-tls-layout.elf" > "$output/old-tls-layout.sections"
grep -Eq '\] \.tbss\.' "$output/old-tls-layout.sections"
"${cross}size" "$output/probe.elf"
sha256sum "$output/probe.elf"
echo 'CPU1 AI2D selftest cross-link: PASS; not executed or installed'

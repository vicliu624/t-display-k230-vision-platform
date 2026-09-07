#!/usr/bin/env bash
set -euo pipefail
# Standalone cross-link audit: no fetching, SDK writes, installation or execution.
[ "$#" -eq 3 ] || { echo "usage: $0 <pinned-canmv_k230-sdk> <musl-cross-prefix> <output-parent>" >&2; exit 2; }
sdk="$(cd "$1" && pwd)"
cross="$2"
script_dir="$(cd "$(dirname "$0")" && pwd)"
runtime="$sdk/src/rtsmart/libs/nncase/riscv64"
mpp="$sdk/src/rtsmart/mpp"
test "$(git -C "$sdk" rev-parse HEAD)" = abb07090ad8a666ed7a5e097b3c714b918731645
git -C "$sdk" ls-files --error-unmatch "$runtime/nncase/include/nncase/version.h" >/dev/null
git -C "$sdk" diff --quiet HEAD -- "$runtime"
grep -Fxq '#define NNCASE_VERSION "2.9.0"' "$runtime/nncase/include/nncase/version.h"
# Pin the real RT-Smart libraries, not the retired Linux 2.11 runtime or models.
for entry in \
    'f6674a664be8133e368ab0f08df3e42d351e1f50811fdbddb6cf195cab6c0264:libNncase.Runtime.Native.a' \
    '5f6baf7c785916beb7e18bda2535585cabaa62315dd8446f256d651900c06564:libnncase.rt_modules.k230.a' \
    '1ac694e7197944e7217e21b50acfa2a8b14956355cf28e2f887d16d2a608fb01:libfunctional_k230.a'; do
    actual="$(sha256sum "$runtime/nncase/lib/${entry#*:}" | awk '{print $1}')"
    [ "$actual" = "${entry%%:*}" ] || { echo "wrong CPU1 nncase archive: ${entry#*:}" >&2; exit 1; }
done
test "$("${cross}g++" -dumpmachine)" = riscv64-unknown-linux-musl
mkdir -p "$3"
output="$(mktemp -d "$(cd "$3" && pwd)/nncase-link.XXXXXX")"
echo "CPU1_NNCASE_LINK_OUTPUT=$output"
"${cross}g++" -std=c++17 -O2 -mcmodel=medany -march=rv64imafdcv -mabi=lp64d \
    -I"$runtime" -I"$runtime/nncase/include" \
    "$script_dir/tests/tdvp-cpu1-nncase-link-probe.cpp" \
    -T "$mpp/userapps/sample/linker_scripts/riscv64/link.lds" -n --static \
    -Wl,-Map,"$output/probe.map" -Wl,--start-group \
    -L"$runtime/nncase/lib" -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 \
    -L"$mpp/userapps/lib" -lsys -lpthread -lm -Wl,--end-group -o "$output/probe.elf"
"${cross}nm" -C "$output/probe.elf" > "$output/probe.symbols"
"${cross}readelf" -h "$output/probe.elf" > "$output/probe.elf-header"
grep -Eq 'Machine:.*RISC-V' "$output/probe.elf-header"
for symbol in ' T gnne_init' ' T kd_mpi_sys_mmz_alloc' ' T kd_mpi_sys_mmz_flush_cache' \
    ' T nncase::runtime::k230::k230_runtime_function::invoke_core(' \
    ' T nncase::F::k230::ai2d_builder::invoke('; do
    grep -Fq "$symbol" "$output/probe.symbols"
done
"${cross}size" "$output/probe.elf"
sha256sum "$output/probe.elf" > "$output/probe.sha256"
cat "$output/probe.sha256"
echo 'CPU1 nncase link: PASS pinned 2.9.0 model/tensor/AI2D dependencies; not executed, installed, or inference acceptance'

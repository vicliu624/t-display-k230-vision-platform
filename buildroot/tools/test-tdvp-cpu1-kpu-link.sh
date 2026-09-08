#!/usr/bin/env bash
set -euo pipefail
# Reuse actual production objects; force the pinned model call graph into a
# separate audit ELF. Never execute it or install it in the production ROMFS.
[ "$#" -eq 3 ] || { echo "usage: $0 <pinned-mpp-dir> <musl-cross-prefix> <production-worker-build-dir>" >&2; exit 2; }
mpp="$1"; cross="$2"; worker="$3"
script_dir="$(cd "$(dirname "$0")" && pwd)"
runtime="$(dirname "$mpp")/libs/nncase/riscv64"
test -s "$worker/tdvp-vision-worker.elf"
objects=()
for unit in tdvp_cpu1_ai_service tdvp_cpu1_ai_guard tdvp_ai_job tdvp_cpu1_ai_owner tdvp_cpu1_kpu_guard; do
    test -s "$worker/$unit.o"
    objects+=("$worker/$unit.o")
done
out="$(mktemp -d "$worker/kpu-link.XXXXXX")"
"${cross}g++" -std=c++17 -DBUILDING_RUNTIME -O2 -mcmodel=medany -march=rv64imafdcv -mabi=lp64d \
    -I"$runtime" -I"$runtime/nncase/include" "$script_dir/tests/tdvp-cpu1-nncase-link-probe.cpp" "${objects[@]}" \
    -T "$script_dir/tdvp-cpu1-nncase-tls.lds" -T "$mpp/userapps/sample/linker_scripts/riscv64/link.lds" -n --static \
    -Wl,--wrap=open,--wrap=close,--wrap=poll,--wrap=gnne_init -Wl,-Map,"$out/audit.map" \
    -Wl,--start-group -L"$runtime/nncase/lib" -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 \
    -L"$mpp/userapps/lib" -lsys -lpthread -lm -Wl,--end-group -o "$out/audit.elf"
"${cross}nm" -C "$out/audit.elf" > "$out/audit.symbols"
grep -Fq ' T nncase::runtime::k230::k230_runtime_function::invoke_core(' "$out/audit.symbols"
grep -Eq ' T __wrap_gnne_init$' "$out/audit.symbols"
grep -Eq ' T tdvp_cpu1_kpu_prepare$' "$out/audit.symbols"
"${cross}objdump" -d "$out/audit.elf" | awk '
    /[[:space:]](jal|jalr|j|jr)[[:space:]]/ && /<gnne_init>/ { bypass = 1 }
    /[[:space:]](jal|jalr|j|jr)[[:space:]]/ && /<__wrap_gnne_init>/ { guarded++; print }
    END { if (bypass || !guarded) {
        print "FAIL model call bypasses the production GNNE guard" > "/dev/stderr"; exit 1
    } }
' > "$out/guarded-calls.txt"
cat "$out/guarded-calls.txt"
sha256sum "$out/audit.elf"
echo 'PASS actual nncase KPU calls link to production bounded initializer; build-only, not executed'

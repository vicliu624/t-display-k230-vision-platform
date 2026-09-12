#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <pinned-mpp-source> <musl-cross-prefix> <output-directory>" >&2
    exit 2
fi
mpp="$1"
cross="$2"
output="$3"
source_dir="$(cd "$(dirname "$0")" && pwd)"
linker="$mpp/userapps/sample/linker_scripts/riscv64/link.lds"
test -s "$linker"
mkdir -p "$output"
includes=(-I"$mpp/include" -I"$mpp/include/comm" -I"$mpp/include/ioctl" -I"$mpp/userapps/api")
# This pinned SDK builds libsensor from source; it is not a shipped archive.
# Keep upstream warning policy separate from the -Werror TDVP implementation.
for source in mpi_sensor mpi_sensor_type_to_mirror; do
    "${cross}gcc" -std=gnu11 -O2 -mcmodel=medany -march=rv64imafdcv -mabi=lp64d \
        "${includes[@]}" -include "$mpp/include/comm/k_autoconf_comm.h" \
        -c "$mpp/userapps/src/sensor/$source.c" -o "$output/$source.o"
done
# Regular archive extraction, never --whole-archive. The ISP implementation
# has cyclic dependencies, so use a group, but omit display/audio/GPU libs.
libraries=(vicap vb sys 3a auto_ctrol binder buffer_management cam_caldb cam_device
    cam_engine cameric_drv cameric_reg_drv cmd_buffer common ebase fpga hal isi
    isp_drv oslayer start_engine switch t_common_c t_database_c t_json_c t_mxml_c
    video_in virtual_hal)
archives=()
for library in "${libraries[@]}"; do
    archive="$mpp/userapps/lib/lib${library}.a"
    test -s "$archive"
    archives+=("$archive")
done
"${cross}gcc" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O2 \
    -mcmodel=medany -march=rv64imafdcv -mabi=lp64d \
    "${includes[@]}" \
    "$source_dir/tdvp_cpu1_capture.c" "$source_dir/tdvp_cpu1_capture_probe.c" \
    "$output/mpi_sensor.o" "$output/mpi_sensor_type_to_mirror.o" \
    -T "$linker" -n --static -Wl,-Map,"$output/tdvp-capture-probe.map" \
    -Wl,--start-group "${archives[@]}" -lpthread -lm -Wl,--end-group \
    -o "$output/tdvp-capture-probe.elf"
"${cross}nm" "$output/tdvp-capture-probe.elf" > "$output/tdvp-capture-probe.symbols"
for symbol in tdvp_cpu1_capture_start tdvp_cpu1_capture_next tdvp_cpu1_capture_stop; do
    grep -Eq " [tT] $symbol$" "$output/tdvp-capture-probe.symbols"
done
if grep -Eq ' [tT] (kd_mpi_vo_.*|kd_mpi_connector_.*|vg_lite_.*|sample_vicap_vo.*)$' "$output/tdvp-capture-probe.symbols"; then
    echo 'FAIL CPU1 capture executable linked a display owner' >&2
    exit 1
fi
"${cross}readelf" -h "$output/tdvp-capture-probe.elf"
"${cross}size" "$output/tdvp-capture-probe.elf"
sha256sum "$output/tdvp-capture-probe.elf"
echo 'PASS RT-Smart capture probe cross-linked (not booted, not production integration)'
runtime="$(dirname "$mpp")/libs/nncase/riscv64"
grep -Fxq '#define NNCASE_VERSION "2.9.0"' "$runtime/nncase/include/nncase/version.h"
for entry in \
    'f6674a664be8133e368ab0f08df3e42d351e1f50811fdbddb6cf195cab6c0264:libNncase.Runtime.Native.a' \
    '5f6baf7c785916beb7e18bda2535585cabaa62315dd8446f256d651900c06564:libnncase.rt_modules.k230.a' \
    '1ac694e7197944e7217e21b50acfa2a8b14956355cf28e2f887d16d2a608fb01:libfunctional_k230.a'; do
    [ "$(sha256sum "$runtime/nncase/lib/${entry#*:}" | awk '{print $1}')" = "${entry%%:*}" ] || {
        echo "FAIL CPU1 nncase archive pin: ${entry#*:}" >&2; exit 1;
    }
done
ai_flags=(-O2 -mcmodel=medany -march=rv64imafdcv -mabi=lp64d -I"$source_dir")
model="$(dirname "$mpp")/libs/kmodel/ai_poc/kmodel/kws.kmodel"
[ "$(sha256sum "$model" | awk '{print $1}')" = b51a31c3310a052488cbce9fbbc52a1d9957f574bc31f1969a1757c7917ae8b4 ] || {
    echo 'FAIL CPU1 official KWS model pin' >&2; exit 1;
}
cp "$model" "$output/tdvp-kws.kmodel"
# Build the immutable model into this worker, not a mutable Linux pathname.
# Assemble with the same RV64 ABI; a raw binary objcopy object lacks its ABI flags.
(
    cd "$output"
    printf '%s\n' '.section .rodata.tdvp_kws,"a",@progbits' '.balign 64' \
        '.global tdvp_cpu1_kws_model_start' 'tdvp_cpu1_kws_model_start:' \
        '.incbin "tdvp-kws.kmodel"' '.global tdvp_cpu1_kws_model_end' 'tdvp_cpu1_kws_model_end:' \
        '.section .note.GNU-stack,"",@progbits' |
        "${cross}gcc" -mcmodel=medany -march=rv64imafdcv -mabi=lp64d -x assembler -c -o tdvp-kws-model.o -
)
worker_objects=()
for unit in tdvp_cpu1_capture tdvp_cpu1_transport tdvp_cpu1_vision_worker tdvp_ai_job tdvp_cpu1_ai_owner tdvp_cpu1_ai_guard tdvp_cpu1_kpu_guard; do
    "${cross}gcc" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror "${ai_flags[@]}" "${includes[@]}" \
        -c "$source_dir/$unit.c" -o "$output/$unit.o"
    worker_objects+=("$output/$unit.o")
done
"${cross}g++" -std=c++17 -DBUILDING_RUNTIME -Wall -Wextra "${ai_flags[@]}" \
    "${includes[@]}" -I"$runtime" -I"$runtime/nncase/include" -c "$source_dir/tdvp_cpu1_ai_service.cpp" -o "$output/tdvp_cpu1_ai_service.o"
"${cross}g++" "${ai_flags[@]}" "${worker_objects[@]}" "$output/tdvp_cpu1_ai_service.o" \
    "$output/tdvp-kws-model.o" \
    "$output/mpi_sensor.o" "$output/mpi_sensor_type_to_mirror.o" \
    -T "$source_dir/tdvp_nncase_tls.lds" \
    -T "$linker" -n --static -Wl,-Map,"$output/tdvp-vision-worker.map" \
    -Wl,--wrap=open,--wrap=close,--wrap=poll,--wrap=gnne_init,--wrap=gnne_enable \
    -Wl,--start-group "${archives[@]}" -L"$runtime/nncase/lib" \
    -lNncase.Runtime.Native -lnncase.rt_modules.k230 -lfunctional_k230 -lpthread -lm -Wl,--end-group \
    -o "$output/tdvp-vision-worker.elf"
"${cross}nm" "$output/tdvp-vision-worker.elf" > "$output/tdvp-vision-worker.symbols"
"${cross}readelf" -SW "$output/tdvp-vision-worker.elf" > "$output/tdvp-vision-worker.sections"
test "$(grep -Ec '\] \.tbss[[:space:]]' "$output/tdvp-vision-worker.sections")" -eq 1
if grep -Eq '\] \.tbss\.' "$output/tdvp-vision-worker.sections"; then
    echo 'FAIL orphan nncase TLS sections in CPU1 worker' >&2; exit 1
fi
grep -Eq ' T tdvp_cpu1_ai_service_start$' "$output/tdvp-vision-worker.symbols"
grep -Eq ' T __wrap_gnne_init$' "$output/tdvp-vision-worker.symbols"
grep -Eq ' T tdvp_cpu1_kpu_prepare$' "$output/tdvp-vision-worker.symbols"
grep -Eq ' T tdvp_cpu1_kpu_complete$' "$output/tdvp-vision-worker.symbols"
grep -Eq ' T __wrap_gnne_enable$' "$output/tdvp-vision-worker.symbols"
# Verify real linked calls, not just the presence of the wrapper object. The
# old gnne_init definition may remain because it shares a vendor object with
# other used helpers, but no direct call may bypass the bounded adapter.
"${cross}objdump" -d "$output/tdvp-vision-worker.elf" | awk -f "$source_dir/audit-kpu-calls.awk"
if grep -Eq ' [tT] (kd_mpi_vo_.*|kd_mpi_connector_.*|vg_lite_.*|sample_vicap_vo.*)$' "$output/tdvp-vision-worker.symbols"; then
    echo 'FAIL CPU1 worker linked a display owner' >&2
    exit 1
fi
"${cross}size" "$output/tdvp-vision-worker.elf"
sha256sum "$output/tdvp-vision-worker.elf"
echo 'PASS RT-Smart asynchronous worker cross-linked with pinned KWS model (not booted, no inference executed)'

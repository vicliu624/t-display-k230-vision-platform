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
"${cross}gcc" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O2 \
    -mcmodel=medany -march=rv64imafdcv -mabi=lp64d "${includes[@]}" \
    "$source_dir/tdvp_cpu1_capture.c" "$source_dir/tdvp_cpu1_transport.c" \
    "$source_dir/tdvp_cpu1_vision_worker.c" "$output/mpi_sensor.o" "$output/mpi_sensor_type_to_mirror.o" \
    -T "$linker" -n --static -Wl,-Map,"$output/tdvp-vision-worker.map" \
    -Wl,--start-group "${archives[@]}" -lpthread -lm -Wl,--end-group \
    -o "$output/tdvp-vision-worker.elf"
"${cross}nm" "$output/tdvp-vision-worker.elf" > "$output/tdvp-vision-worker.symbols"
if grep -Eq ' [tT] (kd_mpi_vo_.*|kd_mpi_connector_.*|vg_lite_.*|sample_vicap_vo.*)$' "$output/tdvp-vision-worker.symbols"; then
    echo 'FAIL CPU1 worker linked a display owner' >&2
    exit 1
fi
"${cross}size" "$output/tdvp-vision-worker.elf"
sha256sum "$output/tdvp-vision-worker.elf"
echo 'PASS RT-Smart asynchronous worker cross-linked (not booted, no model loaded)'

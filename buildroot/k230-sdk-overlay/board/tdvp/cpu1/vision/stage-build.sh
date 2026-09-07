#!/usr/bin/env bash
set -euo pipefail
# Called by the production firmware builder, never by board startup. The SDK
# has already been checked out at the pinned revision in its dedicated cache.
[ "$#" -eq 2 ] || { echo "usage: $0 <prepare|build> <sdk-root>" >&2; exit 2; }
phase="$1"
sdk="$(cd "$2" && pwd)"
source_dir="$(cd "$(dirname "$0")" && pwd)"
bsp="$sdk/src/rtsmart/rtsmart/kernel/bsp/maix3"
mpp="$sdk/src/rtsmart/mpp"
test -s "$bsp/SConstruct" && test -s "$sdk/.config" && test -s "$mpp/kernel/mpp.mk"
case "$phase" in
prepare)
    # Fresh tracked BSP sources come from build-rtsmart.sh's pinned checkout.
    # A dirty/mixed revision must fail patching rather than skip this step.
    for patch_file in "$source_dir"/000*.patch; do
        patch --batch --fuzz=0 -d "$bsp" -p1 < "$patch_file"
    done
    install -m 0644 "$source_dir/tdvp_cpu1_mpp_init.c" "$bsp/board/mpp/mpp_init.c"
    install -m 0644 "$source_dir/SConscript" "$bsp/board/mpp/SConscript"
    for file in tdvp_cpu1_vision_layout.h tdvp_cpu1_vision_pins.c tdvp_cpu1_i2c4_clock.c \
        tdvp_cpu1_camera_clock.c tdvp_vision_owner.c tdvp_vision_owner.h tdvp_vision_owner_io.h \
        tdvp_vision_abi.h tdvp_cpu1_vision_startup.c tdvp_cpu1_ai_clock.c tdvp_cpu1_ai_init.c tdvp_cpu1_fft.c; do
        install -m 0644 "$source_dir/$file" "$bsp/board/mpp/$file"
    done
    install -m 0644 "$source_dir/tdvp_cpu1_i2c4_board.h" "$bsp/drivers/interdrv/i2c/"
    install -m 0644 "$source_dir/tdvp_cpu1_vision_launch.c" "$bsp/applications/"
    install -m 0644 "$source_dir/tdvp_cpu1_vision_mount.c" "$bsp/applications/mnt.c"
    if ! grep -Fq "src += Glob('tdvp_cpu1_vision_launch.c')" "$bsp/applications/SConscript"; then
        sed -i "/src[[:space:]]*+=[[:space:]]*Glob('mnt.c')/a src += Glob('tdvp_cpu1_vision_launch.c')" "$bsp/applications/SConscript"
    fi
    grep -Fq "src += Glob('tdvp_cpu1_vision_launch.c')" "$bsp/applications/SConscript"
    config="$bsp/configs/k230_canmv_v3p0_defconfig"
    for symbol in I2C0 I2C1 I2C2 I2C3 I2C4_SLAVE; do
        sed -i "/^CONFIG_RT_USING_${symbol}=/d; /^# CONFIG_RT_USING_${symbol} is not set/d" "$config"
        printf '# CONFIG_RT_USING_%s is not set\n' "$symbol" >> "$config"
    done
    for symbol in MPP GPIO PIN I2C I2C4 GNNE TDVP_CPU1_VISION; do
        sed -i "/^CONFIG_RT_USING_${symbol}=/d; /^# CONFIG_RT_USING_${symbol} is not set/d" "$config"
        printf 'CONFIG_RT_USING_%s=y\n' "$symbol" >> "$config"
    done
    # Let the real SDK syncconfig generate all derived values. Only GC2093
    # CSI2 is configured; no vendor display/other sensor defaults may leak in.
    sed -i '/^CONFIG_MPP_/d; /^# CONFIG_MPP_/d; /^CONFIG_MEM_MMZ_/d' "$sdk/.config"
    printf '%s\n' \
        'CONFIG_MEM_MMZ_BASE=0x14000000' 'CONFIG_MEM_MMZ_SIZE=0x08000000' \
        'CONFIG_MPP_ENABLE_CSI_DEV_2=y' 'CONFIG_MPP_CSI_DEV2_POWER=-1' \
        'CONFIG_MPP_CSI_DEV2_RESET=21' 'CONFIG_MPP_CSI_DEV2_I2C_DEV="i2c4"' \
        'CONFIG_MPP_CSI_DEV2_MCLK_1=y' 'CONFIG_MPP_ENABLE_SENSOR_GC2093=y' \
        'CONFIG_MPP_SENSOR_GC2093_ON_CSI2_USE_CHIP_CLK=y' >> "$sdk/.config"
    ;;
build)
    : "${SDK_RTSMART_BUILD_DIR:?production RT-Smart output required}"
    : "${TDVP_VISION_CROSS_COMPILE:?production musl cross compiler required}"
    export SDK_SRC_ROOT_DIR="$sdk" MPP_SRC_DIR="$mpp" RTSMART_SRC_DIR="$sdk/src/rtsmart/rtsmart"
    export PATH="$(dirname "$TDVP_VISION_CROSS_COMPILE"):$PATH"
    for required in 'CONFIG_MEM_MMZ_BASE 0x14000000' 'CONFIG_MEM_MMZ_SIZE 0x08000000' \
        'CONFIG_MPP_CSI_DEV2_RESET 21' 'CONFIG_MPP_CSI_DEV2_MCLK_NUM 1' \
        'CONFIG_MPP_CSI_DEV2_I2C_DEV "i2c4"'; do
        grep -Fxq "#define $required" "$bsp/k_autoconf_comm.h"
    done
    # ar -rc does not remove obsolete members. Recreate these two owned
    # source-built archives so an old OV5647 object cannot survive a rebuild.
    rm -f "$mpp/kernel/lib/libsensor.a" "$mpp/kernel/lib/libmediafreq.a"
    make -B -C "$mpp/kernel/sensor" all
    make -B -C "$mpp/kernel/mediafreq" all
    output="$SDK_RTSMART_BUILD_DIR/tdvp-vision"
    mkdir -p "$output"
    bash "$source_dir/build-capture-probe.sh" "$mpp" "$TDVP_VISION_CROSS_COMPILE" "$output"
    romfs="$(mktemp -d "$output/romfs.XXXXXX")"
    trap 'rm -rf -- "$romfs"' EXIT
    mkdir -p "$romfs/bin" "$romfs/tmp" "$romfs/data"
    install -m 0755 "$output/tdvp-vision-worker.elf" "$romfs/bin/tdvp-vision-worker.elf"
    "${TDVP_CPU1_PYTHON:-/usr/bin/python3}" "$sdk/src/rtsmart/rtsmart/tools/mkromfs.py" \
        "$romfs" "$bsp/applications/tdvp_vision_romfs.inc"
    test -s "$bsp/applications/tdvp_vision_romfs.inc"
    ;;
*) echo "unknown CPU1 vision build phase: $phase" >&2; exit 2 ;;
esac

#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 2 ] || { echo "usage: $0 <bsp> <musl-cross-prefix>" >&2; exit 2; }
bsp="$1"
cross="$2"
symbols="$(mktemp)"
trap 'rm -f -- "$symbols"' EXIT
"${cross}nm" "$bsp/rtthread.elf" > "$symbols"
for symbol in tdvp_cpu1_service mpp_init tdvp_cpu1_vision_ownership_status vicap_init \
    sensor_gc2093_probe tdvp_cpu1_vision_launch tdvp_cpu1_vision_mount \
    tdvp_cpu1_i2c4_clock_prepare tdvp_cpu1_i2c4_init_status tdvp_cpu1_camera_clock_prepare \
    tdvp_cpu1_vision_startup tdvp_owner_cpu1_step tdvp_cpu1_ai_init tdvp_cpu1_ai_clock_prepare \
    gnne_device_init ai_2d_device_init tdvp_cpu1_fft_init; do
    grep -Eq " [tT] ${symbol}$" "$symbols" || { echo "CPU1 kernel missing $symbol" >&2; exit 1; }
done
if grep -Eq '(__rt_init_ai_module_init| ai_module_init)$| [tT] (kd_vo_init|connector_device_init|ai_init|ao_init|venc_init|vdec_init|sdma_memcpy_only_for_fft|usbd_mtp.*)$' "$symbols"; then
    echo 'CPU1 kernel contains automatic AI init or an unowned display/audio/SDMA driver' >&2; exit 1
fi
for symbol in MPP GPIO PIN I2C I2C4 GNNE TDVP_CPU1_VISION; do
    grep -Eq "^#define RT_USING_${symbol}([[:space:]]|$)" "$bsp/rtconfig.h"
done
if grep -Eq '^#define (RT_USING_(SDIO[01]?|I2C[0-3]|I2C4_SLAVE|SPI[012]?|WIFI|CANAAN_UART|RTC[^[:space:]]*)|ENABLE_CHERRY_USB[^[:space:]]*|ENABLE_CANMV_USB[^[:space:]]*)([[:space:]]|$)' "$bsp/rtconfig.h"; then
    echo 'CPU1 kernel enables a Linux-owned peripheral' >&2; exit 1
fi
echo 'CPU1 kernel contract: PASS AI/vision, gated startup, no Linux-owned display/audio/SDMA; not hardware acceptance'

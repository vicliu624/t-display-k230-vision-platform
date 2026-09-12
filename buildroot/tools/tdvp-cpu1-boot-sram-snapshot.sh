#!/bin/sh
# SPDX-License-Identifier: MIT
# Manual, read-only K230 diagnostic. Never a KPU admission or boot acceptance gate.
# TRM V0.3.1: SDMA section 2.5.2.7 and decompressor section 15.5.
set -eu
if [ "$#" -ne 1 ] || [ "$1" != --read-mmio ]; then
    echo "usage: sh $0 --read-mmio (root on K230; reads status, never grants ownership)" >&2
    exit 2
fi
[ "$(id -u)" = 0 ] || { echo 'root required' >&2; exit 2; }
[ "$(uname -m)" = riscv64 ] || { echo 'not a RISC-V 64-bit board' >&2; exit 2; }
compatible=$(cat /sys/firmware/devicetree/base/compatible | tr '\000' '\n')
if ! printf '%s\n' "$compatible" | grep -Fxq canaan,kendryte-k230; then
    echo 'not a confirmed K230 device tree' >&2
    exit 2
fi
command -v devmem >/dev/null || { echo 'devmem is unavailable' >&2; exit 2; }
printf 'diagnostic=read-only-snapshot\nboot_id='
cat /proc/sys/kernel/random/boot_id
idle=1
for sample in 1 2 3; do
    printf 'sample=%s\nuptime=' "$sample"
    cat /proc/uptime
    # No control/start/stop/reset or interrupt-clear register access. Each
    # devmem invocation has exactly ADDRESS WIDTH, never a third write value.
    for address in 0x80800054 0x80800084 0x808000b4 0x808000e4 0x80800058 0x80808004 0x8080800c; do
        value=$(devmem "$address" 32)
        case "$value" in
            0x????????) ;;
            *) echo "invalid register response: $address" >&2; exit 2 ;;
        esac
        case "${value#0x}" in
            *[!0123456789abcdefABCDEF]*) echo "non-hex register response: $address" >&2; exit 2 ;;
        esac
        printf '%s=%s\n' "$address" "$value"
        case "$address" in
            0x80800054|0x80800084|0x808000b4|0x808000e4)
                [ "$((value & 3))" -eq 0 ] || idle=0 ;; # RO busy/pause
            0x80800058)
                [ "$((value & 1024))" -eq 0 ] || idle=0 ;; # RW, read only: decompression mode
            0x80808004)
                [ "$((value & 2147483648))" -eq 0 ] || idle=0 ;; # RW, read only: controller enable
            0x8080800c)
                # CRC and state are RO. Do not assign undocumented state values
                # or reserved bits a completion/quiescence meaning.
                : ;;
        esac
    done
done
printf 'snapshot_no_activity=%s\n' "$idle"
printf '%s\n' 'startup_order=not_proven' 'exclusive_ownership=not_proven' 'kpu_model_completion=not_tested'
[ "$idle" -eq 1 ]

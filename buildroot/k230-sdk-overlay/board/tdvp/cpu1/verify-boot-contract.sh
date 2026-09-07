#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
	printf 'Usage: %s <uboot-build-dir> <raw-firmware> <firmware-manifest>\n' "$0" >&2
	exit 2
fi

UBOOT_HEADER="$1/board/canaan/common/sdk_autoconf.h"
FIRMWARE="$2"
MANIFEST="$3"

fail() {
	printf 'TDVP CPU boot contract: %s\n' "$*" >&2
	exit 1
}

[ -s "${UBOOT_HEADER}" ] || fail "missing built U-Boot core-selection header"
core="$(awk '$1 == "#define" && $2 == "CONFIG_LINUX_RUN_CORE_ID" { print $3 }' "${UBOOT_HEADER}" | tr -d '\r')"
[ "${core}" = 0 ] || fail "U-Boot must execute on CPU0 before resetting CPU1 (got ${core})"
[ -s "${FIRMWARE}" ] && [ -s "${MANIFEST}" ] || fail "missing firmware or manifest"
for field in \
	'firmware_format=opensbi-fw-payload-raw' \
	'entry_point=0x10000000' \
	'runtime_base=0x10000000' \
	'runtime_size=0x04000000' \
	'allocatable_size=0x03ff0000' \
	'mailbox_physical=0x13ff0000'; do
	grep -Fqx "${field}" "${MANIFEST}" || fail "missing firmware contract: ${field}"
done
size="$(stat -c '%s' "${FIRMWARE}")"
[ "${size}" -gt $((0x20000)) ] && [ "${size}" -le $((20 * 1024 * 1024)) ] ||
	fail "raw payload must include RT-Smart at +0x20000 and fit the 20 MiB slot"
grep -Fqx "firmware_size=${size}" "${MANIFEST}" || fail "firmware size differs from manifest"
hash="$(sha256sum "${FIRMWARE}" | awk '{print $1}')"
grep -Fqx "firmware_sha256=${hash}" "${MANIFEST}" || fail "firmware hash differs from manifest"
magic="$(od -An -N4 -tx1 "${FIRMWARE}" | tr -d ' \n')"
case "${magic}" in
	4b323330|27051956|7f454c46|1f8b*)
		fail "boot_baremetal cannot execute a K230/uImage/ELF/gzip container"
		;;
esac
# Check the actual build, not only the overlay waiting to be copied. The BSP
# sync is additive and a reused output must not package an old unguarded SPL.
DECOMPRESSOR="$(dirname "$0")/../../../boot/uboot/u-boot-2022.10-overlay/arch/riscv/cpu/k230/unzip.c"
cmp -s "${DECOMPRESSOR}" "$1/arch/riscv/cpu/k230/unzip.c" ||
	fail 'built U-Boot decompressor differs from the paired overlay'
for binary in u-boot.bin spl/u-boot-spl.bin; do
	[ -s "$1/${binary}" ] && grep -aFq 'TDVP boot decompressor: DMA/SRAM handoff refused' "$1/${binary}" ||
		fail "built ${binary} lacks the DMA/SRAM handoff guard"
done
printf '%s\n' 'TDVP CPU boot contract: PASS CPU0 U-Boot + raw CPU1 payload + boot DMA/SRAM guard'

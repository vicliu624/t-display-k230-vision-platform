#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERIFY="${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/cpu1/verify-boot-contract.sh"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT
UBOOT="${TEMP_DIR}/uboot"
FIRMWARE="${TEMP_DIR}/fw_payload.bin"
MANIFEST="${TEMP_DIR}/manifest"
mkdir -p "${UBOOT}/board/canaan/common"
HEADER="${UBOOT}/board/canaan/common/sdk_autoconf.h"
UNZIP="${PROJECT_DIR}/buildroot/k230-sdk-overlay/boot/uboot/u-boot-2022.10-overlay/arch/riscv/cpu/k230/unzip.c"
mkdir -p "${UBOOT}/arch/riscv/cpu/k230" "${UBOOT}/spl"
cp "${UNZIP}" "${UBOOT}/arch/riscv/cpu/k230/unzip.c"
# Synthetic marker files only exercise packaging refusals. The separate real
# SPL/U-Boot cross-build must establish that these are linked into boot code.
printf '%s\n' 'TDVP boot decompressor: DMA/SRAM handoff refused' > "${UBOOT}/u-boot.bin"
cp "${UBOOT}/u-boot.bin" "${UBOOT}/spl/u-boot-spl.bin"

fixture() {
	printf '#define CONFIG_LINUX_RUN_CORE_ID 0\n' > "${HEADER}"
	# Synthetic bytes only test the packaging guard, not executable firmware.
	dd if=/dev/zero of="${FIRMWARE}" bs=1 count=1 seek=$((0x20000)) status=none
	printf '\x6f\x00\x00\x00' | dd of="${FIRMWARE}" conv=notrunc status=none
	{
		printf '%s\n' 'firmware_format=opensbi-fw-payload-raw' \
			'entry_point=0x10000000' 'runtime_base=0x10000000' \
			'runtime_size=0x04000000' 'allocatable_size=0x03ff0000' \
			'mailbox_physical=0x13ff0000'
		printf 'firmware_size=%s\n' "$(stat -c '%s' "${FIRMWARE}")"
		printf 'firmware_sha256=%s\n' "$(sha256sum "${FIRMWARE}" | awk '{print $1}')"
	} > "${MANIFEST}"
}

reject() {
	local expected="$1"
	if bash "${VERIFY}" "${UBOOT}" "${FIRMWARE}" "${MANIFEST}" > "${TEMP_DIR}/log" 2>&1; then
		printf 'test-tdvp-cpu1-boot-contract: FAIL accepted %s\n' "${expected}" >&2
		exit 1
	fi
	grep -Fq "${expected}" "${TEMP_DIR}/log" || {
		cat "${TEMP_DIR}/log" >&2
		exit 1
	}
}

fixture
bash "${VERIFY}" "${UBOOT}" "${FIRMWARE}" "${MANIFEST}"
printf '#define CONFIG_LINUX_RUN_CORE_ID 1\n' > "${HEADER}"
reject 'U-Boot must execute on CPU0'
fixture
sed -i 's/entry_point=0x10000000/entry_point=0x0/' "${MANIFEST}"
reject 'missing firmware contract: entry_point'
fixture
sed -i 's/allocatable_size=0x03ff0000/allocatable_size=0x04000000/' "${MANIFEST}"
reject 'missing firmware contract: allocatable_size'
fixture
printf '\xff' | dd of="${FIRMWARE}" bs=1 seek=32 conv=notrunc status=none
reject 'firmware hash differs'
for magic in '\x4b\x32\x33\x30' '\x27\x05\x19\x56' '\x7f\x45\x4c\x46' '\x1f\x8b\x08\x00'; do
	fixture
	printf '%b' "${magic}" | dd of="${FIRMWARE}" conv=notrunc status=none
	# Keep the hash valid to prove container rejection is independent of it.
	sed -i "s/^firmware_sha256=.*/firmware_sha256=$(sha256sum "${FIRMWARE}" | awk '{print $1}')/" "${MANIFEST}"
	reject 'boot_baremetal cannot execute'
done
fixture
truncate -s $((20 * 1024 * 1024 + 1)) "${FIRMWARE}"
reject 'fit the 20 MiB slot'
fixture
truncate -s $((0x20000)) "${FIRMWARE}"
reject 'include RT-Smart at +0x20000'
fixture
printf '\n/* stale build */\n' >> "${UBOOT}/arch/riscv/cpu/k230/unzip.c"
reject 'built U-Boot decompressor differs'
cp "${UNZIP}" "${UBOOT}/arch/riscv/cpu/k230/unzip.c"
printf 'old unguarded U-Boot\n' > "${UBOOT}/u-boot.bin"
reject 'built u-boot.bin lacks the DMA/SRAM handoff guard'
cp "${UBOOT}/spl/u-boot-spl.bin" "${UBOOT}/u-boot.bin"
printf 'old unguarded SPL\n' > "${UBOOT}/spl/u-boot-spl.bin"
reject 'built spl/u-boot-spl.bin lacks the DMA/SRAM handoff guard'
cp "${UBOOT}/u-boot.bin" "${UBOOT}/spl/u-boot-spl.bin"
bash "${VERIFY}" "${UBOOT}" "${FIRMWARE}" "${MANIFEST}"
printf '%s\n' 'test-tdvp-cpu1-boot-contract: PASS valid payload and 13 rejected regressions'

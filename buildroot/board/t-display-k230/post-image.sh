#!/usr/bin/env bash
set -euo pipefail

BINARIES_DIR="${1:?missing Buildroot images directory argument}"
BOARD_DIR="${BR2_EXTERNAL_TDVP_PATH:?BR2_EXTERNAL_TDVP_PATH is not set}/board/t-display-k230"
ARTIFACT_DIR="${TDVP_BOOT_ARTIFACT_DIR:-${BOARD_DIR}/boot-artifacts}"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
SYSIMAGE="${BINARIES_DIR}/sysimage-sdcard.img"
SDK_DTB="${TDVP_DTB:-tdisplay-k230.dtb}"
BOOT_DIR="${BINARIES_DIR}/boot"

missing=0

require_file() {
	local path="$1"
	local label="$2"

	if [ ! -f "${path}" ]; then
		printf 'TDVP: missing %-32s %s\n' "${label}" "${path}" >&2
		missing=1
	fi
}

require_file "${BINARIES_DIR}/Image" "kernel Image"
require_file "${BINARIES_DIR}/${SDK_DTB}" "selected TDVP DTB"
require_file "${BINARIES_DIR}/rootfs.ext2" "rootfs"
require_file "${BINARIES_DIR}/uboot-env.bin" "generated U-Boot env"
require_file "${ARTIFACT_DIR}/spl.bin" "K230 SPL"
require_file "${ARTIFACT_DIR}/u-boot.bin" "K230 U-Boot"
require_file "${ARTIFACT_DIR}/fw_jump_add_uboot_head.bin" "OpenSBI fw_jump image"

if [ "${missing}" -ne 0 ]; then
	rm -f "${SYSIMAGE}"
	cat >&2 <<EOF
TDVP: sysimage-sdcard.img was not generated.
TDVP: this is expected until all required K230 Linux SDK boot inputs exist.
TDVP: local K230 boot artifacts are read from:
TDVP:   ${ARTIFACT_DIR}
TDVP: or set TDVP_BOOT_ARTIFACT_DIR to another directory.
TDVP: expected canonical artifact names:
TDVP:   spl.bin
TDVP:   u-boot.bin
TDVP:   fw_jump_add_uboot_head.bin
TDVP: U-Boot env is generated from ${BOARD_DIR}/uboot-linux.env.
TDVP: set TDVP_REQUIRE_SYSIMAGE=1 to make missing inputs a build error.
EOF
	if [ "${TDVP_REQUIRE_SYSIMAGE:-0}" = "1" ]; then
		exit 1
	fi
	exit 0
fi

rm -rf "${BINARIES_DIR}/boot-artifacts" "${BOOT_DIR}" "${BINARIES_DIR}/boot.ext4"
mkdir -p "${BINARIES_DIR}/boot-artifacts" "${BOOT_DIR}"

install -m 0644 "${ARTIFACT_DIR}/spl.bin" "${BINARIES_DIR}/boot-artifacts/spl.bin"
install -m 0644 "${ARTIFACT_DIR}/u-boot.bin" "${BINARIES_DIR}/boot-artifacts/u-boot.bin"
install -m 0644 "${BINARIES_DIR}/uboot-env.bin" "${BINARIES_DIR}/boot-artifacts/u-boot.env.bin"

install -m 0644 "${ARTIFACT_DIR}/fw_jump_add_uboot_head.bin" "${BOOT_DIR}/fw_jump_add_uboot_head.bin"
install -m 0644 "${BINARIES_DIR}/Image" "${BOOT_DIR}/Image"
install -m 0644 "${BINARIES_DIR}/${SDK_DTB}" "${BOOT_DIR}/k.dtb"
install -m 0644 "${BINARIES_DIR}/${SDK_DTB}" "${BOOT_DIR}/${SDK_DTB}"

fakeroot -- mkfs.ext4 -d "${BOOT_DIR}" -r 1 -N 0 -m 1 -L "boot" -O ^64bit "${BINARIES_DIR}/boot.ext4" 80M

support/scripts/genimage.sh -c "${GENIMAGE_CFG}"

if [ ! -f "${SYSIMAGE}" ]; then
	echo "TDVP: genimage finished but ${SYSIMAGE} was not created" >&2
	exit 1
fi

printf 'TDVP: generated %s\n' "${SYSIMAGE}" >&2

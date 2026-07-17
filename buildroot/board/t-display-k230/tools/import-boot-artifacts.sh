#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  import-boot-artifacts.sh <sdk-images-dir> [output-artifact-dir]

Inputs:
  <sdk-images-dir>/uboot/fn_u-boot-spl.bin
  <sdk-images-dir>/uboot/fn_ug_u-boot.bin
  <sdk-images-dir>/boot/fw_jump_add_uboot_head.bin

If fw_jump_add_uboot_head.bin is missing but fw_jump.bin exists, set
TDVP_MKIMAGE or put mkimage in PATH. The script will generate the wrapped
OpenSBI image using the K230 Linux SDK blinux-compatible command:

  mkimage -A riscv -O linux -T kernel -C none -a 0 -e 0 -n linux \
    -d fw_jump.bin fw_jump_add_uboot_head.bin

The default output directory is:
  buildroot/board/t-display-k230/boot-artifacts
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
	usage
	exit 0
fi

SDK_IMAGES_DIR="${1:-}"

if [ -z "${SDK_IMAGES_DIR}" ]; then
	usage
	exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT_DIR="${2:-${BOARD_DIR}/boot-artifacts}"

SDK_IMAGES_DIR="$(cd "${SDK_IMAGES_DIR}" && pwd)"
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

find_first_existing() {
	local candidate
	for candidate in "$@"; do
		if [ -f "${candidate}" ]; then
			printf '%s\n' "${candidate}"
			return 0
		fi
	done
	return 1
}

require_file() {
	local path="$1"
	local label="$2"

	if [ ! -f "${path}" ]; then
		printf 'TDVP: missing %-28s %s\n' "${label}" "${path}" >&2
		exit 1
	fi
}

SPL_SRC="${SDK_IMAGES_DIR}/uboot/fn_u-boot-spl.bin"
UBOOT_SRC="${SDK_IMAGES_DIR}/uboot/fn_ug_u-boot.bin"

require_file "${SPL_SRC}" "SPL artifact"
require_file "${UBOOT_SRC}" "U-Boot artifact"

FW_WRAPPED_SRC="$(
	find_first_existing \
		"${SDK_IMAGES_DIR}/boot/fw_jump_add_uboot_head.bin" \
		"${SDK_IMAGES_DIR}/fw_jump_add_uboot_head.bin" \
		|| true
)"

if [ -z "${FW_WRAPPED_SRC}" ]; then
	FW_JUMP_SRC="$(
		find_first_existing \
			"${SDK_IMAGES_DIR}/fw_jump.bin" \
			"${SDK_IMAGES_DIR}/boot/fw_jump.bin" \
			|| true
	)"

	if [ -z "${FW_JUMP_SRC}" ]; then
		printf 'TDVP: missing OpenSBI fw_jump wrapper and raw fw_jump.bin under %s\n' "${SDK_IMAGES_DIR}" >&2
		exit 1
	fi

	MKIMAGE="${TDVP_MKIMAGE:-$(command -v mkimage || true)}"
	if [ -z "${MKIMAGE}" ] || [ ! -x "${MKIMAGE}" ]; then
		cat >&2 <<EOF
TDVP: cannot generate fw_jump_add_uboot_head.bin because mkimage was not found.
TDVP: set TDVP_MKIMAGE to the U-Boot mkimage built by the pinned SDK.
TDVP: raw fw_jump.bin found at:
TDVP:   ${FW_JUMP_SRC}
EOF
		exit 1
	fi

	FW_WRAPPED_SRC="${OUT_DIR}/fw_jump_add_uboot_head.bin"
	"${MKIMAGE}" -A riscv -O linux -T kernel -C none -a 0 -e 0 -n linux \
		-d "${FW_JUMP_SRC}" "${FW_WRAPPED_SRC}"
fi

install -m 0644 "${SPL_SRC}" "${OUT_DIR}/spl.bin"
install -m 0644 "${UBOOT_SRC}" "${OUT_DIR}/u-boot.bin"

if [ "${FW_WRAPPED_SRC}" != "${OUT_DIR}/fw_jump_add_uboot_head.bin" ]; then
	install -m 0644 "${FW_WRAPPED_SRC}" "${OUT_DIR}/fw_jump_add_uboot_head.bin"
fi

MANIFEST="${OUT_DIR}/manifest.local"

{
	printf 'tdvp_boot_artifacts_manifest_version=1\n'
	printf 'generated_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
	printf 'sdk_images_dir=%s\n' "${SDK_IMAGES_DIR}"
	printf 'import_command=%q %q %q\n' "$0" "${SDK_IMAGES_DIR}" "${OUT_DIR}"
	printf '\n'
	printf '[files]\n'
	(
		cd "${OUT_DIR}"
		sha256sum spl.bin u-boot.bin fw_jump_add_uboot_head.bin
	)
} > "${MANIFEST}"

cat >&2 <<EOF
TDVP: imported K230 Linux SDK boot artifacts:
TDVP:   ${OUT_DIR}/spl.bin
TDVP:   ${OUT_DIR}/u-boot.bin
TDVP:   ${OUT_DIR}/fw_jump_add_uboot_head.bin
TDVP: wrote local manifest:
TDVP:   ${MANIFEST}
EOF

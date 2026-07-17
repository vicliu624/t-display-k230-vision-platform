#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  build-sdk-boot-artifacts.sh <k230-linux-sdk-root> [output-artifact-dir]

This helper runs the minimal K230 Linux SDK targets needed for TDVP boot
artifacts, then imports them into TDVP canonical names.

It intentionally builds only:
  opensbi
  uboot

It does not build the full SDK rootfs or demo image.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
	usage
	exit 0
fi

SDK_ROOT="${1:-}"

if [ -z "${SDK_ROOT}" ]; then
	usage
	exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARTIFACT_DIR="${2:-${SCRIPT_DIR}/../boot-artifacts}"
CONF="${TDVP_K230_SDK_CONF:-k230_canmv_v3_defconfig}"
EXPECTED_SDK_COMMIT="5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7"

SDK_ROOT="$(cd "${SDK_ROOT}" && pwd)"
ARTIFACT_DIR="$(mkdir -p "${ARTIFACT_DIR}" && cd "${ARTIFACT_DIR}" && pwd)"

if [ ! -f "${SDK_ROOT}/Makefile" ] || [ ! -d "${SDK_ROOT}/buildroot-overlay" ]; then
	printf 'TDVP: not a k230_linux_sdk root: %s\n' "${SDK_ROOT}" >&2
	exit 1
fi

if git -C "${SDK_ROOT}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	SDK_COMMIT="$(git -C "${SDK_ROOT}" rev-parse HEAD)"
	if [ "${SDK_COMMIT}" != "${EXPECTED_SDK_COMMIT}" ]; then
		cat >&2 <<EOF
TDVP: k230_linux_sdk commit mismatch.
TDVP: expected ${EXPECTED_SDK_COMMIT}
TDVP: actual   ${SDK_COMMIT}
TDVP: run: git -C ${SDK_ROOT} checkout ${EXPECTED_SDK_COMMIT}
EOF
		exit 1
	fi
fi

# Buildroot rejects Windows-injected PATH entries containing spaces. Keep SDK
# builds on a plain Linux PATH.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

cd "${SDK_ROOT}"

if [ ! -f "output/${CONF}/.config" ]; then
	make "CONF=${CONF}" "${CONF}"
fi

make "CONF=${CONF}" opensbi uboot

BASE_DIR="${SDK_ROOT}/output/${CONF}"
BUILD_DIR="${BASE_DIR}/build"
BINARIES_DIR="${BASE_DIR}/images"
BR2_CONFIG="${BASE_DIR}/.config"
UBOOT_BUILD_DIR="${BUILD_DIR}/uboot-2022.10"
K230_SDK_ROOT="${SDK_ROOT}"
CONFIG_GEN_SECURITY_IMG="${CONFIG_GEN_SECURITY_IMG:-}"

export BASE_DIR BUILD_DIR BINARIES_DIR BR2_CONFIG UBOOT_BUILD_DIR K230_SDK_ROOT CONFIG_GEN_SECURITY_IMG

require_file() {
	local path="$1"
	local label="$2"

	if [ ! -f "${path}" ]; then
		printf 'TDVP: missing %-28s %s\n' "${label}" "${path}" >&2
		exit 1
	fi
}

require_file "${BINARIES_DIR}/fw_jump.bin" "OpenSBI fw_jump.bin"
require_file "${UBOOT_BUILD_DIR}/u-boot.bin" "U-Boot raw binary"
require_file "${UBOOT_BUILD_DIR}/spl/u-boot-spl.bin" "U-Boot SPL raw binary"
require_file "${UBOOT_BUILD_DIR}/tools/mkimage" "U-Boot mkimage"

# Reuse the SDK's own firmware-header functions, but stop before the script's
# full post-image entrypoint. Skip the path initializers that depend on $0,
# because this helper intentionally sources the SDK script instead of executing
# it as a post-image script.
set +u
source <(awk '
	/^GENIMAGE_CFG_SD=/ { next }
	/^env_dir=/ { next }
	/^gen_uboot_bin$/ { exit }
	{ print }
' "${SDK_ROOT}/buildroot-overlay/board/canaan/k230-soc/post-image.sh")

gen_uboot_bin
set -u

TDVP_MKIMAGE="${UBOOT_BUILD_DIR}/tools/mkimage" \
	"${SCRIPT_DIR}/import-boot-artifacts.sh" "${BINARIES_DIR}" "${ARTIFACT_DIR}"

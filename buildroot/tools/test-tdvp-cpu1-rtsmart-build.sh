#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPU1_DIR="${SCRIPT_DIR}/../k230-sdk-overlay/board/tdvp/cpu1"
OUTPUT_DIR="${1:?Usage: test-tdvp-cpu1-rtsmart-build.sh <output-dir>}"
mkdir -p "${OUTPUT_DIR}"
OUTPUT_DIR="$(cd "${OUTPUT_DIR}" && pwd)"
export TDVP_CPU1_CACHE_ROOT="${TDVP_CPU1_CACHE_ROOT:-${OUTPUT_DIR}/.tdvp-cpu1}"
CHECKOUT="${TDVP_CPU1_SOURCE_DIR:-${TDVP_CPU1_CACHE_ROOT}/canmv_k230}"
SDK="${CHECKOUT}/canmv_k230"
RTSMART="${SDK}/src/rtsmart"
BSP="${RTSMART}/rtsmart/kernel/bsp/maix3"
TOOLCHAIN="${TDVP_CPU1_TOOLCHAIN_DIR:-${TDVP_CPU1_CACHE_ROOT}/toolchain}"
NM="${TOOLCHAIN}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-nm"

# This is intentionally a real build, using the exact post-image entry point,
# pinned SDK, peripheral configuration and mailbox application. No fake ELF
# or synthetic payload can satisfy this regression.
bash "${SCRIPT_DIR}/validate-k230-sdk-linux-patches.sh" "${CPU1_DIR}/patches"
bash "${CPU1_DIR}/build-rtsmart.sh" "${OUTPUT_DIR}/fw_payload.bin" \
	"${OUTPUT_DIR}/manifest" "${CPU1_DIR}/tdvp_cpu1_abi.h"
"${NM}" "${BSP}/rtthread.elf" > "${OUTPUT_DIR}/rtthread-symbols.txt"
grep -Eq ' [tT] tdvp_cpu1_service$' "${OUTPUT_DIR}/rtthread-symbols.txt"
grep -Eq ' [tT] dfs_file_open$' "${OUTPUT_DIR}/rtthread-symbols.txt"
if grep -Eq 'inotify_handler_|usbd_mtp' "${OUTPUT_DIR}/rtthread-symbols.txt"; then
	printf '%s\n' 'CPU1 regression: USB/MTP symbols leaked into compute-only firmware' >&2
	exit 1
fi
if grep -Eq '^#define (ENABLE_CHERRY_USB[^[:space:]]*|CHERRY_USB_DEVICE_ENABLE_CLASS_MTP|RT_USING_RTC[^[:space:]]*)([[:space:]]|$)' "${BSP}/rtconfig.h"; then
	printf '%s\n' 'CPU1 regression: USB/MTP or RTC was re-enabled to hide an optional dependency' >&2
	exit 1
fi

# Exercise the actual patched upstream Makefile with failing commands. The
# kernel case contains a stale binary: a broken status guard must not copy it.
# GNU make returns 2; its diagnostic must preserve the shell's 7/9, not expand
# $? to .parse_config or continue after a failed configuration generator.
TEMP_DIR="$(mktemp -d)"
trap 'status=$?; if [ "$status" -ne 0 ]; then cat "${TEMP_DIR}"/*.log >&2; fi; rm -rf "${TEMP_DIR}"; exit "$status"' EXIT
TEST_RT="${TEMP_DIR}/src/rtsmart"
TEST_BSP="${TEST_RT}/rtsmart/kernel/bsp/maix3"
mkdir -p "${TEST_BSP}/configs" "${TEST_RT}/mpp/include/comm" \
	"${TEMP_DIR}/include/generated" "${TEMP_DIR}/tools" "${TEMP_DIR}/bin"
cp "${RTSMART}/Makefile" "${TEST_RT}/Makefile"
touch "${TEMP_DIR}/tools/toolchain_rtsmart.mk" \
	"${TEMP_DIR}/include/generated/autoconf.h" "${TEST_BSP}/configs/test_defconfig"
printf '#!/bin/sh\nexit 7\n' > "${TEMP_DIR}/bin/scons"
printf '#!/bin/sh\nexit 9\n' > "${TEST_RT}/parse_config"
chmod +x "${TEMP_DIR}/bin/scons" "${TEST_RT}/parse_config"
TEST_MAKE=(env -u MAKEFLAGS -u MFLAGS -u GNUMAKEFLAGS
	"PATH=${TEMP_DIR}/bin:${PATH}" make -C "${TEST_RT}"
	"SDK_SRC_ROOT_DIR=${TEMP_DIR}" "SDK_TOOLS_DIR=${TEMP_DIR}/tools"
	"SDK_RTSMART_SRC_DIR=${TEST_RT}" "SDK_BUILD_IMAGES_DIR=${TEMP_DIR}/images"
	"SDK_RTSMART_BUILD_DIR=${TEMP_DIR}/build" SDK_DEFCONFIG=test_defconfig NCPUS=1)
for scenario in parser config kernel; do
	case "${scenario}" in
		parser) target=.parse_config; expected=9 ;;
		config)
			printf '#!/bin/sh\nexit 0\n' > "${TEST_RT}/parse_config"
			target=.parse_config; expected=7 ;;
		kernel)
			touch "${TEST_RT}/.parse_config"
			printf 'stale firmware\n' > "${TEST_BSP}/rtthread.bin"
			target=kernel; expected=7 ;;
	esac
	if "${TEST_MAKE[@]}" "${target}" > "${TEMP_DIR}/${scenario}.log" 2>&1; then
		printf 'CPU1 regression: %s failure was swallowed\n' "${scenario}" >&2
		exit 1
	fi
	grep -Eq "Error ${expected}([^0-9]|$)" "${TEMP_DIR}/${scenario}.log"
	if grep -Fq 'Illegal number' "${TEMP_DIR}/${scenario}.log"; then
		exit 1
	fi
	[ ! -e "${TEMP_DIR}/images/rtsmart/rtthread.bin" ]
	if [ "${scenario}" != kernel ]; then
		[ ! -e "${TEST_RT}/.parse_config" ]
	fi
done

TEST_SBI="${TEMP_DIR}/src/opensbi"
mkdir -p "${TEST_SBI}/opensbi" "${TEMP_DIR}/sbi-build/platform/kendryte/fpgac908/firmware"
cp "${SDK}/src/opensbi/Makefile" "${TEST_SBI}/Makefile"
touch "${TEMP_DIR}/tools/toolchain_linux.mk"
printf '#!/bin/sh\nexit 9\n' > "${TEST_SBI}/parse_config"
chmod +x "${TEST_SBI}/parse_config"
SBI_MAKE=(env -u MAKEFLAGS -u MFLAGS -u GNUMAKEFLAGS make -C "${TEST_SBI}"
	"SDK_TOOLS_DIR=${TEMP_DIR}/tools" "SDK_OPENSBI_SRC_DIR=${TEST_SBI}"
	"SDK_OPENSBI_BUILD_DIR=${TEMP_DIR}/sbi-build"
	"SDK_BUILD_IMAGES_DIR=${TEMP_DIR}/images" NCPUS=1)
if "${SBI_MAKE[@]}" .parse_config > "${TEMP_DIR}/sbi-parser.log" 2>&1; then
	exit 1
fi
grep -Eq 'Error 9([^0-9]|$)' "${TEMP_DIR}/sbi-parser.log"
[ ! -e "${TEST_SBI}/.parse_config" ]
touch "${TEST_SBI}/.parse_config" "${TEMP_DIR}/images/rtsmart/rtthread.bin"
printf 'all:\n\t@exit 7\n' > "${TEST_SBI}/opensbi/Makefile"
printf 'stale payload\n' > "${TEMP_DIR}/sbi-build/platform/kendryte/fpgac908/firmware/fw_payload.bin"
if "${SBI_MAKE[@]}" build > "${TEMP_DIR}/sbi-build.log" 2>&1; then
	exit 1
fi
grep -Eq 'Error 7([^0-9]|$)' "${TEMP_DIR}/sbi-build.log"
if grep -Fq 'Illegal number' "${TEMP_DIR}/sbi-build.log"; then
	exit 1
fi
[ ! -e "${TEMP_DIR}/sbi-build/opensbi.bin" ]
printf '%s\n' 'test-tdvp-cpu1-rtsmart-build: PASS real firmware linked without USB/MTP; five RT-Smart/OpenSBI failure paths preserved'

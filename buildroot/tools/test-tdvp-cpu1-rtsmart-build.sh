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
BUILD_BSP="${BSP}"
TOOLCHAIN="${TDVP_CPU1_TOOLCHAIN_DIR:-${TDVP_CPU1_CACHE_ROOT}/toolchain}"
NM="${TOOLCHAIN}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-nm"

# This is intentionally a real build, using the exact post-image entry point,
# pinned SDK, peripheral configuration and mailbox application. No fake ELF
# or synthetic payload can satisfy this regression.
bash "${SCRIPT_DIR}/validate-k230-sdk-linux-patches.sh" "${CPU1_DIR}/patches"
bash "${CPU1_DIR}/build-rtsmart.sh" "${OUTPUT_DIR}/fw_payload.bin" \
	"${OUTPUT_DIR}/manifest" "${CPU1_DIR}/tdvp_cpu1_abi.h"
TEMP_DIR="$(mktemp -d)"
trap 'status=$?; if [ "$status" -ne 0 ]; then cat "${TEMP_DIR}"/*.log >&2; fi; rm -rf "${TEMP_DIR}"; exit "$status"' EXIT
# Regress the vision hooks against pristine blobs from the exact pinned BSP.
# The production checkout is already patched; do not apply its patches twice
# or change the source used to build the actual firmware under test.
PINNED_BSP=canmv_k230/src/rtsmart/rtsmart/kernel/bsp/maix3
# The BSP directory also contains large unrelated prebuilt images. Restrict
# partial-clone extraction to source/header dependencies of these regressions.
# git archive prefetches unrelated blobs even with pathspecs on the CI Git
# version; git show addresses one exact pinned blob without that traversal.
for path in Kconfig board/board.h board/sdk_kernel_init.c c908/tick.h \
	drivers/interdrv/i2c/drv_i2c.c drivers/interdrv/gnne/ai_module.c \
	drivers/interdrv/gnne/gnne_dev.c drivers/interdrv/gnne/ai2d_dev.c \
	drivers/interdrv/hardlock/drv_hardlock.c drivers/interdrv/hardlock/drv_hardlock.h \
	drivers/interdrv/sysctl/sysctl_boot/sysctl_boot.h \
	drivers/interdrv/sysctl/sysctl_power/sysctl_pwr.h \
	drivers/interdrv/sysctl/sysctl_reset/sysctl_rst.h; do
	mkdir -p "${TEMP_DIR}/${PINNED_BSP}/$(dirname "${path}")"
	git -C "${CHECKOUT}" show "HEAD:${PINNED_BSP}/${path}" > "${TEMP_DIR}/${PINNED_BSP}/${path}"
done
BSP="${TEMP_DIR}/canmv_k230/src/rtsmart/rtsmart/kernel/bsp/maix3"
bash "${SCRIPT_DIR}/validate-k230-sdk-linux-patches.sh" "${CPU1_DIR}/vision"
bash "${SCRIPT_DIR}/test-tdvp-cpu1-i2c4-early.sh" "${BSP}"
bash "${SCRIPT_DIR}/test-tdvp-cpu1-ownership.sh" "${BSP}"
bash "${SCRIPT_DIR}/test-tdvp-cpu1-linux-owner.sh"
# Use exact pinned Git blobs for header-level regressions, never generated or
# modified headers from the production output or another SDK release.
MPP_CLOCK_HEADER=canmv_k230/src/rtsmart/mpp/kernel/mediafreq/src/sysctl/sysctl_media_clock/sysctl_media_clk.h
MPP_LAYOUT="${TEMP_DIR}/mpp"
mkdir -p "${MPP_LAYOUT}/kernel/mediafreq/src/sysctl/sysctl_media_clock"
git -C "${CHECKOUT}" show "HEAD:${MPP_CLOCK_HEADER}" \
	> "${MPP_LAYOUT}/kernel/mediafreq/src/sysctl/sysctl_media_clock/sysctl_media_clk.h"
bash "${SCRIPT_DIR}/test-tdvp-cpu1-camera-clock.sh" "${BSP}" "${MPP_LAYOUT}"
# The AI regression compiles against the matching pinned FFT ioctl ABI and
# patches real GNNE/AI2D/hardlock initializers, never substitute fixture code.
mkdir -p "${MPP_LAYOUT}/include/ioctl"
for header in k_type.h ioctl/k_ioctl.h ioctl/k_fft_ioctl.h; do
	git -C "${CHECKOUT}" show "HEAD:canmv_k230/src/rtsmart/mpp/include/${header}" \
		> "${MPP_LAYOUT}/include/${header}"
done
bash "${SCRIPT_DIR}/test-tdvp-cpu1-ai.sh" "${BSP}" "${MPP_LAYOUT}"
bash "${SCRIPT_DIR}/test-tdvp-cpu1-ai-waits.sh" "${BSP}"
"${NM}" "${BUILD_BSP}/rtthread.elf" > "${OUTPUT_DIR}/rtthread-symbols.txt"
grep -Eq ' [tT] tdvp_cpu1_service$' "${OUTPUT_DIR}/rtthread-symbols.txt"
grep -Eq ' [tT] dfs_file_open$' "${OUTPUT_DIR}/rtthread-symbols.txt"
if grep -Eq 'inotify_handler_|usbd_mtp' "${OUTPUT_DIR}/rtthread-symbols.txt"; then
	printf '%s\n' 'CPU1 regression: USB/MTP symbols leaked into AI/vision firmware' >&2
	exit 1
fi
if grep -Eq '^#define (ENABLE_CHERRY_USB[^[:space:]]*|CHERRY_USB_DEVICE_ENABLE_CLASS_MTP|RT_USING_RTC[^[:space:]]*)([[:space:]]|$)' "${BUILD_BSP}/rtconfig.h"; then
	printf '%s\n' 'CPU1 regression: USB/MTP or RTC was re-enabled to hide an optional dependency' >&2
	exit 1
fi

# Exercise the production entry a second time in the SAME SDK/output. ar -rc
# retains members absent from the new source list; plant a uniquely named
# obsolete member to prove the sensor archive is actually recreated. The
# fixture uses an existing target object, never a fabricated firmware payload.
CPU1_AR="${NM%-nm}-ar"
SENSOR_ARCHIVE="${RTSMART}/mpp/kernel/lib/libsensor.a"
SENSOR_OBJECT="${SDK}/output/k230_canmv_v3p0/rtsmart/tdvp-vision/mpi_sensor.o"
cp "${SENSOR_OBJECT}" "${TEMP_DIR}/tdvp_retired_sensor_member.o"
"${CPU1_AR}" -r "${SENSOR_ARCHIVE}" "${TEMP_DIR}/tdvp_retired_sensor_member.o"
"${CPU1_AR}" -t "${SENSOR_ARCHIVE}" | grep -Fxq tdvp_retired_sensor_member.o
bash "${CPU1_DIR}/build-rtsmart.sh" "${OUTPUT_DIR}/fw_payload.bin" \
	"${OUTPUT_DIR}/manifest" "${CPU1_DIR}/tdvp_cpu1_abi.h" > "${TEMP_DIR}/reused-build.log" 2>&1
if "${CPU1_AR}" -t "${SENSOR_ARCHIVE}" | grep -Fxq tdvp_retired_sensor_member.o; then
	echo 'CPU1 regression: retired sensor object survived the production rebuild' >&2
	exit 1
fi
echo 'CPU1 regression: PASS production entry rebuilt the same SDK/output and removed a stale archive member'

# Exercise the actual patched upstream Makefile with failing commands. The
# kernel case contains a stale binary: a broken status guard must not copy it.
# GNU make returns 2; its diagnostic must preserve the shell's 7/9, not expand
# $? to .parse_config or continue after a failed configuration generator.
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
printf '%s\n' 'test-tdvp-cpu1-rtsmart-build: PASS real AI/vision firmware built twice without USB/MTP; stale member removed; five RT-Smart/OpenSBI failure paths preserved'

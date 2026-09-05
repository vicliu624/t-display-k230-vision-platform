#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
	printf 'Usage: %s <firmware-output> <manifest-output> <abi-header>\n' "$0" >&2
	exit 2
fi

FIRMWARE_OUTPUT="$1"
MANIFEST_OUTPUT="$2"
ABI_HEADER="$3"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPU1_CACHE_ROOT="${TDVP_CPU1_CACHE_ROOT:-$(dirname "${FIRMWARE_OUTPUT}")/.tdvp-cpu1}"
CPU1_CHECKOUT_DIR="${TDVP_CPU1_SOURCE_DIR:-${CPU1_CACHE_ROOT}/canmv_k230}"
CPU1_SOURCE_DIR="${CPU1_CHECKOUT_DIR}/canmv_k230"
CPU1_TOOLCHAIN_DIR="${TDVP_CPU1_TOOLCHAIN_DIR:-${CPU1_CACHE_ROOT}/toolchain}"
CPU1_OPENSBI_TOOLCHAIN_DIR="${TDVP_CPU1_OPENSBI_TOOLCHAIN_DIR:-${CPU1_CACHE_ROOT}/opensbi-toolchain}"
CPU1_HOST_TOOLS_DIR="${CPU1_CACHE_ROOT}/host-tools"
CPU1_PYTHON="${TDVP_CPU1_PYTHON:-/usr/bin/python3}"
CPU1_REPOSITORY="https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt.git"
CPU1_COMMIT="abb07090ad8a666ed7a5e097b3c714b918731645"
TOOLCHAIN_ARCHIVE="riscv64-unknown-linux-musl-rv64imafdcv-lp64d-20230420.tar.bz2"
TOOLCHAIN_URL="https://github.com/kendryte/canmv_k230/releases/download/v1.1/${TOOLCHAIN_ARCHIVE}"
OPENSBI_TOOLCHAIN_NAME="Xuantie-900-gcc-linux-5.10.4-glibc-x86_64-V2.6.0"
OPENSBI_TOOLCHAIN_ARCHIVE="${OPENSBI_TOOLCHAIN_NAME}.tar.bz2"
OPENSBI_TOOLCHAIN_URL="https://github.com/kendryte/canmv_k230/releases/download/v1.1/${OPENSBI_TOOLCHAIN_ARCHIVE}"
CPU1_RUNTIME_BASE="0x10000000"
CPU1_RUNTIME_SIZE="0x04000000"
# The Linux reservation includes the mailbox. RT-Smart's allocator must not.
CPU1_ALLOCATABLE_SIZE="0x03ff0000"
CPU1_HEAP_SIZE="0x02000000"
CPU1_OPENSBI_CROSS_COMPILE="${TDVP_CPU1_OPENSBI_CROSS_COMPILE:-${CPU1_OPENSBI_TOOLCHAIN_DIR}/${OPENSBI_TOOLCHAIN_NAME}/bin/riscv64-unknown-linux-gnu-}"

fail() {
	printf 'TDVP CPU1 firmware: %s\n' "$*" >&2
	exit 1
}

require_file() {
	[ -s "$1" ] || fail "required file is missing or empty: $1"
}

# The upstream SDK inspects its parent make process for a -jN argument and
# rejects every value other than 1.  Buildroot's jobserver leaves its original
# -jN in recursive make metadata even if a child is invoked with -j1.  Start
# each SDK make tree without that metadata instead.  The SDK's mkenv.mk does
# not consult MAKEFLAGS directly: it runs `ps` on MAKEPPID.  GNU make preserves
# an imported MAKEPPID for a child make, so merely unsetting it lets the value
# from Buildroot be recreated.  Pin it to this script's Bash PID; that command
# line has no -j option and each SDK make tree then inherits the safe value.
run_sdk_make() {
	env -u MAKEFLAGS -u MFLAGS -u GNUMAKEFLAGS MAKEPPID="$$" make "$@"
}

require_file "${ABI_HEADER}"
mkdir -p "${CPU1_CACHE_ROOT}" "${CPU1_TOOLCHAIN_DIR}" "${CPU1_OPENSBI_TOOLCHAIN_DIR}" "$(dirname "${FIRMWARE_OUTPUT}")"

# The upstream OpenSBI image wrapper calls the host-side U-Boot mkimage tool
# through SDK_UBOOT_BUILD_DIR, even though this integration builds only the
# CPU1 firmware and does not build U-Boot itself.  Use the distro-provided
# compatible tool in that expected location, and fail before the expensive
# SDK build if either upstream host dependency is unavailable.
CPU1_MKIMAGE="$(command -v mkimage || true)"
[ -n "${CPU1_MKIMAGE}" ] || fail "U-Boot mkimage is unavailable; install u-boot-tools"
[ -x "${CPU1_PYTHON}" ] || fail "Python interpreter is unavailable: ${CPU1_PYTHON}"
"${CPU1_PYTHON}" -c 'from Cryptodome.Cipher import AES; from Cryptodome.PublicKey import RSA' \
	|| fail "Python Cryptodome module is unavailable; install python3-pycryptodome"

if [ ! -x "${CPU1_TOOLCHAIN_DIR}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-gcc" ]; then
	archive_path="${CPU1_TOOLCHAIN_DIR}/${TOOLCHAIN_ARCHIVE}"
	if [ ! -f "${archive_path}" ]; then
		curl --fail --location --retry 5 \
			--output "${archive_path}.part" "${TOOLCHAIN_URL}"
		mv "${archive_path}.part" "${archive_path}"
	fi
	tar -xjf "${archive_path}" -C "${CPU1_TOOLCHAIN_DIR}"
fi

if [ ! -x "${CPU1_OPENSBI_CROSS_COMPILE}gcc" ]; then
	archive_path="${CPU1_OPENSBI_TOOLCHAIN_DIR}/${OPENSBI_TOOLCHAIN_ARCHIVE}"
	if [ ! -f "${archive_path}" ]; then
		curl --fail --location --retry 5 \
			--output "${archive_path}.part" "${OPENSBI_TOOLCHAIN_URL}"
		mv "${archive_path}.part" "${archive_path}"
	fi
	tar -xjf "${archive_path}" -C "${CPU1_OPENSBI_TOOLCHAIN_DIR}"
fi

CPU1_CROSS_COMPILE="${CPU1_TOOLCHAIN_DIR}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-"
[ -x "${CPU1_CROSS_COMPILE}gcc" ] || fail "RT-Smart cross compiler is unavailable"
[ -x "${CPU1_OPENSBI_CROSS_COMPILE}gcc" ] || fail "OpenSBI Linux cross compiler is unavailable"

# The SDK evaluates toolchain_rtsmart.mk while processing the initial
# k230_canmv_v3p0_defconfig target.  Export the cache directory before that
# first make invocation so it finds the toolchain provisioned above instead of
# its unrelated per-user default (~/.kendryte/k230_toolchains).
export SDK_TOOLCHAIN_DIR="${CPU1_TOOLCHAIN_DIR}"

if [ ! -d "${CPU1_CHECKOUT_DIR}/.git" ]; then
	[ ! -e "${CPU1_CHECKOUT_DIR}" ] || fail "CPU1 source path is not a Git checkout: ${CPU1_CHECKOUT_DIR}"
	# Fetch source blobs in one pack but leave large prebuilt images and release
	# archives on the server.  Older Git versions otherwise request each sparse
	# file separately from a partial clone, making first-time CI builds slow.
	git clone --filter=blob:limit=1048576 --no-checkout "${CPU1_REPOSITORY}" "${CPU1_CHECKOUT_DIR}"
fi
if git -C "${CPU1_CHECKOUT_DIR}" sparse-checkout -h >/dev/null 2>&1; then
	git -C "${CPU1_CHECKOUT_DIR}" sparse-checkout init --no-cone
	git -C "${CPU1_CHECKOUT_DIR}" sparse-checkout set --no-cone \
		canmv_k230/Kconfig canmv_k230/Kconfig.canmv canmv_k230/Makefile canmv_k230/configs \
		canmv_k230/boards/Kconfig canmv_k230/boards/k230_canmv_v3p0 canmv_k230/tools \
		canmv_k230/src/applications canmv_k230/src/uboot canmv_k230/src/rtsmart/Makefile \
		canmv_k230/src/rtsmart/Kconfig canmv_k230/src/rtsmart/mpp/Kconfig \
		canmv_k230/src/rtsmart/parse_config canmv_k230/src/rtsmart/rtsmart \
		canmv_k230/src/opensbi
else
	# Git releases predating the sparse-checkout subcommand still understand the
	# underlying configuration.  Keep this fallback for older developer hosts.
	CPU1_GIT_DIR="$(git -C "${CPU1_CHECKOUT_DIR}" rev-parse --git-dir)"
	case "${CPU1_GIT_DIR}" in
		/*) ;;
		*) CPU1_GIT_DIR="${CPU1_CHECKOUT_DIR}/${CPU1_GIT_DIR}" ;;
	esac
	CPU1_SPARSE_FILE="${CPU1_GIT_DIR}/info/sparse-checkout"
	mkdir -p "$(dirname "${CPU1_SPARSE_FILE}")"
	git -C "${CPU1_CHECKOUT_DIR}" config core.sparseCheckout true
	{
		printf '%s\n' \
			'/canmv_k230/Kconfig' \
			'/canmv_k230/Kconfig.canmv' \
			'/canmv_k230/Makefile' \
			'/canmv_k230/configs/' \
			'/canmv_k230/boards/Kconfig' \
			'/canmv_k230/boards/k230_canmv_v3p0/' \
			'/canmv_k230/tools/' \
			'/canmv_k230/src/applications/' \
			'/canmv_k230/src/uboot/' \
			'/canmv_k230/src/rtsmart/Makefile' \
			'/canmv_k230/src/rtsmart/Kconfig' \
			'/canmv_k230/src/rtsmart/mpp/Kconfig' \
			'/canmv_k230/src/rtsmart/parse_config' \
			'/canmv_k230/src/rtsmart/rtsmart/' \
			'/canmv_k230/src/opensbi/'
	} > "${CPU1_SPARSE_FILE}"
fi
git -C "${CPU1_CHECKOUT_DIR}" fetch --depth=1 --filter=blob:limit=1048576 \
	origin "${CPU1_COMMIT}"
git -C "${CPU1_CHECKOUT_DIR}" checkout --detach --force "${CPU1_COMMIT}"
git -C "${CPU1_CHECKOUT_DIR}" read-tree -mu HEAD

# parse_config is an executable at the root of the RT-Smart source directory.
# Some Git sparse-checkout implementations keep the parent directory but omit
# that file-only pattern.  Restore exactly the pinned blob when that happens,
# instead of expanding the checkout to unrelated SDK trees.
CPU1_PARSE_CONFIG="${CPU1_SOURCE_DIR}/src/rtsmart/parse_config"
if [ ! -f "${CPU1_PARSE_CONFIG}" ]; then
	mkdir -p "$(dirname "${CPU1_PARSE_CONFIG}")"
	git -C "${CPU1_CHECKOUT_DIR}" show "${CPU1_COMMIT}:canmv_k230/src/rtsmart/parse_config" \
		> "${CPU1_PARSE_CONFIG}"
	chmod 0755 "${CPU1_PARSE_CONFIG}"
fi
require_file "${CPU1_PARSE_CONFIG}"

# The pinned SDK's mkenv.mk probes an optional third-party mirror with curl
# but supplies neither a connection nor a transfer deadline.  Once the CPU1
# build is allowed past its parallel-build guard, an unreachable mirror can
# otherwise leave post-image running forever.  A failed bounded probe preserves
# the upstream non-native branch while keeping the image build deterministic.
CPU1_MKENV="${CPU1_SOURCE_DIR}/tools/mkenv.mk"
require_file "${CPU1_MKENV}"
sed -i \
	's|curl --output /dev/null --silent --head --fail https://ai.b-bug.org/k230/|curl --connect-timeout 10 --max-time 30 --output /dev/null --silent --head --fail https://ai.b-bug.org/k230/|' \
	"${CPU1_MKENV}"
grep -Fq 'curl --connect-timeout 10 --max-time 30 --output /dev/null --silent --head --fail https://ai.b-bug.org/k230/' \
	"${CPU1_MKENV}" || fail "cannot bound the upstream CPU1 mirror probe"

# Ubuntu's python3-pycryptodome package deliberately exposes the Cryptodome
# namespace, whereas this pinned upstream image script imports Crypto.  The
# image path below is non-secure (-n), so its unconditional gmssl imports are
# unused and can be disabled without pulling an unpinned Python package into
# the reproducible host.  The secure-image guard below prevents using this
# compatibility path for encryption or signing.
FIRMWARE_GEN="${CPU1_SOURCE_DIR}/tools/firmware_gen.py"
require_file "${FIRMWARE_GEN}"
sed -i \
	-e 's/^from Crypto\./from Cryptodome./' \
	-e '/^from gmssl[ .]/s/^/# /' \
	"${FIRMWARE_GEN}"
grep -Fq 'from Cryptodome.Cipher import AES' "${FIRMWARE_GEN}" \
	|| fail "cannot adapt upstream firmware Crypto import"
if grep -Eq '^[[:space:]]*from gmssl([ .])' "${FIRMWARE_GEN}"; then
	fail "cannot disable unused upstream firmware gmssl imports"
fi

APPLICATION_DIR="${CPU1_SOURCE_DIR}/src/rtsmart/rtsmart/kernel/bsp/maix3/applications"
SCONSCRIPT="${APPLICATION_DIR}/SConscript"
RTSMART_BUILDING="${CPU1_SOURCE_DIR}/src/rtsmart/rtsmart/kernel/rt-thread/tools/building.py"
require_file "${SCONSCRIPT}"
require_file "${RTSMART_BUILDING}"
require_file "${CPU1_SOURCE_DIR}/src/rtsmart/Makefile"
require_file "${CPU1_SOURCE_DIR}/src/opensbi/Makefile"
mkdir -p "${CPU1_SOURCE_DIR}/src/rtsmart/mpp/include/comm"

# SCons 4.5 on the current Ubuntu runner exposes these construction variables
# as deque instances.  This pinned RT-Thread revision assumed list instances,
# so normalize both operands before the first RT-Smart SCons invocation.
sed -i \
	-e "/^[[:space:]]*CPPPATH = Env.get(/c\            CPPPATH = list(Env.get('CPPPATH', [''])) + list(group.get('LOCAL_CPPPATH', ['']))" \
	-e "/^[[:space:]]*CPPDEFINES = Env.get(/c\            CPPDEFINES = list(Env.get('CPPDEFINES', [''])) + list(group.get('LOCAL_CPPDEFINES', ['']))" \
	"${RTSMART_BUILDING}"
grep -Fq "CPPPATH = list(Env.get('CPPPATH', [''])) + list(group.get('LOCAL_CPPPATH', ['']))" \
	"${RTSMART_BUILDING}" || fail "cannot adapt RT-Smart CPPPATH for SCons 4"
grep -Fq "CPPDEFINES = list(Env.get('CPPDEFINES', [''])) + list(group.get('LOCAL_CPPDEFINES', ['']))" \
	"${RTSMART_BUILDING}" || fail "cannot adapt RT-Smart CPPDEFINES for SCons 4"

install -m 0644 "${SCRIPT_DIR}/tdvp_cpu1_service.c" "${APPLICATION_DIR}/tdvp_cpu1_service.c"
install -m 0644 "${SCRIPT_DIR}/tdvp_cpu1_main.c" "${APPLICATION_DIR}/main.c"
install -m 0644 "${ABI_HEADER}" "${APPLICATION_DIR}/tdvp_cpu1_abi.h"
if ! grep -Fq "tdvp_cpu1_service.c" "${SCONSCRIPT}"; then
	sed -i "/src[[:space:]]*+=[[:space:]]*Glob('mnt.c')/a src += Glob('tdvp_cpu1_service.c')" "${SCONSCRIPT}"
fi
grep -Fq "tdvp_cpu1_service.c" "${SCONSCRIPT}" || fail "cannot add CPU1 service to RT-Smart SConscript"

# Linux owns board peripherals. Retain the RT kernel/console, but exclude
# device drivers which would initialize the shared SD, USB, GPU or buses.
CPU1_RT_CONFIG="${CPU1_SOURCE_DIR}/src/rtsmart/rtsmart/kernel/bsp/maix3/configs/k230_canmv_v3p0_defconfig"
require_file "${CPU1_RT_CONFIG}"
sed -E -i \
	's/^(CONFIG_(RT_USING_(SDIO[01]?|MPP|GNNE|GPIO|PIN|PWM|HWTIMER|HW_TIMER[012]|I2C[0-4]?|I2C_BITOPS|SPI[012]?|RTC|RTC_PMU|ADC|WDT|PM|TOUCH|CANAAN_UART|UART_CANAAN_[1-4]|LORA|REALTEK|REGULATOR|TOUCH_GT9895|WS2812|WIFI)|RT_WLAN_.*|CHERRYUSB_.*|CHERRY_USB_.*|ENABLE_CHERRY_USB.*|ENABLE_CANMV_USB.*|ENABLE_CANMV_NETWORK_MGMT_DEV|ENABLE_CANMV_MISC_DEV))=y$/# \1 is not set/' \
	"${CPU1_RT_CONFIG}"

pushd "${CPU1_SOURCE_DIR}" >/dev/null
run_sdk_make k230_canmv_v3p0_defconfig
sed -i \
	-e 's/^CONFIG_RTT_CONSOLE_UART0=y/# CONFIG_RTT_CONSOLE_UART0 is not set/' \
	-e 's/^CONFIG_RT_AUTO_RESIZE_PARTITION=y/# CONFIG_RT_AUTO_RESIZE_PARTITION is not set/' .config
for setting in \
	"CONFIG_RTT_CONSOLE_UART3=y" \
	"CONFIG_RTT_CONSOLE_ID=3" \
	"CONFIG_OPENSBI_CONSOLE_UART_REG_ADDR=0x91403000" \
	"CONFIG_MEM_BASE_ADDR=${CPU1_RUNTIME_BASE}" \
	"CONFIG_MEM_RTSMART_BASE=${CPU1_RUNTIME_BASE}" \
	"CONFIG_MEM_RTSMART_SIZE=${CPU1_ALLOCATABLE_SIZE}" \
	"CONFIG_MEM_RTSMART_HEAP_SIZE=${CPU1_HEAP_SIZE}"; do
	key="${setting%%=*}"
	if grep -q "^${key}=" .config; then
		sed -i "s/^${key}=.*/${setting}/" .config
	else
		printf '%s\n' "${setting}" >> .config
	fi
done
run_sdk_make .autoconf
if grep -q '^CONFIG_GEN_SECURITY_IMG=y' .config; then
	fail "secure CPU1 images require the upstream gmssl signing environment"
fi

export SDK_SRC_ROOT_DIR="${CPU1_SOURCE_DIR}"
export SDK_TOOLCHAIN_DIR="${CPU1_TOOLCHAIN_DIR}"
export SDK_TOOLS_DIR="${CPU1_SOURCE_DIR}/tools"
export CONFIG_BOARD="k230_canmv_v3p0"
export SDK_DEFCONFIG="k230_canmv_v3p0_defconfig"
export SDK_BOARDS_DIR="${CPU1_SOURCE_DIR}/boards"
export SDK_BOARD_DIR="${SDK_BOARDS_DIR}/${CONFIG_BOARD}"
export SDK_BUILD_DIR="${CPU1_SOURCE_DIR}/output/${CONFIG_BOARD}"
export SDK_BUILD_IMAGES_DIR="${SDK_BUILD_DIR}/images"
export SDK_RTSMART_SRC_DIR="${CPU1_SOURCE_DIR}/src/rtsmart"
export SDK_RTSMART_BUILD_DIR="${SDK_BUILD_DIR}/rtsmart"
export SDK_UBOOT_BUILD_DIR="${CPU1_CACHE_ROOT}/host-uboot"
export SDK_OPENSBI_SRC_DIR="${CPU1_SOURCE_DIR}/src/opensbi"
export SDK_OPENSBI_BUILD_DIR="${SDK_BUILD_DIR}/opensbi"
export NCPUS="${TDVP_CPU1_JOBS:-2}"
mkdir -p "${SDK_BUILD_IMAGES_DIR}" "${SDK_RTSMART_BUILD_DIR}" "${SDK_UBOOT_BUILD_DIR}/tools" "${SDK_OPENSBI_BUILD_DIR}" "${CPU1_HOST_TOOLS_DIR}"
ln -sfn "${CPU1_MKIMAGE}" "${SDK_UBOOT_BUILD_DIR}/tools/mkimage"
ln -sfn "${CPU1_PYTHON}" "${CPU1_HOST_TOOLS_DIR}/python3"
require_file "${SDK_UBOOT_BUILD_DIR}/tools/mkimage"
export PATH="${CPU1_HOST_TOOLS_DIR}:${PATH}"

# These upstream stamps have no prerequisites and survive the cached source
# checkout. Regenerate headers/Kconfig for the new RAM/peripheral contract.
rm -f "${SDK_RTSMART_SRC_DIR}/.parse_config" "${SDK_OPENSBI_SRC_DIR}/.parse_config"
run_sdk_make -C "${SDK_RTSMART_SRC_DIR}" .parse_config CROSS_COMPILE="${CPU1_CROSS_COMPILE}"
CPU1_RT_HEADER="${APPLICATION_DIR}/../rtconfig.h"
require_file "${CPU1_RT_HEADER}"
if grep -Eq '^#define (RT_USING_(SDIO[01]?|MPP|GNNE|GPIO|I2C[0-4]?|SPI[012]?|WIFI|CANAAN_UART)|ENABLE_CHERRY_USB|ENABLE_CANMV_USB[^[:space:]]*)([[:space:]]|$)' "${CPU1_RT_HEADER}"; then
	fail "RT-Smart configuration still enables Linux-owned peripheral drivers"
fi
run_sdk_make -C "${SDK_RTSMART_SRC_DIR}" kernel CROSS_COMPILE="${CPU1_CROSS_COMPILE}"
# RT-Smart uses the upstream musl toolchain, whereas OpenSBI's Kendryte
# platform requires the XuanTie Linux toolchain installed by the CI host.
run_sdk_make -C "${SDK_OPENSBI_SRC_DIR}" all CROSS_COMPILE="${CPU1_OPENSBI_CROSS_COMPILE}"
# boot_baremetal sets a reset vector; it does not parse K230/uImage headers or
# decompress the wrapped opensbi_rtt_system.bin generated by the vendor SDK.
# Load the raw, linked OpenSBI+RT-Smart payload at its FW_TEXT_START instead.
CPU1_FIRMWARE="${SDK_OPENSBI_BUILD_DIR}/platform/kendryte/fpgac908/firmware/fw_payload.bin"
require_file "${CPU1_FIRMWARE}"
CPU1_ELF="${CPU1_FIRMWARE%.bin}.elf"
require_file "${CPU1_ELF}"
CPU1_ENTRY="$("${CPU1_OPENSBI_CROSS_COMPILE}readelf" -h "${CPU1_ELF}" | awk '/Entry point address:/ { print $NF }')"
[ "${CPU1_ENTRY}" = "${CPU1_RUNTIME_BASE}" ] || fail "raw firmware entry is not ${CPU1_RUNTIME_BASE}: ${CPU1_ENTRY}"
if [ "$(stat -c '%s' "${CPU1_FIRMWARE}")" -gt $((20 * 1024 * 1024)) ]; then
	fail "CPU1 firmware does not fit in the fixed 20 MiB raw SD slot"
fi
install -m 0644 "${CPU1_FIRMWARE}" "${FIRMWARE_OUTPUT}"

{
	printf 'tdvp_cpu1_manifest_version=1\n'
	printf 'source_repository=%s\n' "${CPU1_REPOSITORY}"
	printf 'source_commit=%s\n' "${CPU1_COMMIT}"
	printf 'runtime_base=%s\n' "${CPU1_RUNTIME_BASE}"
	printf 'runtime_size=%s\n' "${CPU1_RUNTIME_SIZE}"
	printf 'allocatable_size=%s\n' "${CPU1_ALLOCATABLE_SIZE}"
	printf 'firmware_format=opensbi-fw-payload-raw\n'
	printf 'entry_point=%s\n' "${CPU1_ENTRY}"
	printf 'mailbox_physical=0x13ff0000\n'
	printf 'firmware_size='
	stat -c '%s' "${FIRMWARE_OUTPUT}"
	printf 'firmware_sha256='
	sha256sum "${FIRMWARE_OUTPUT}" | awk '{print $1}'
} > "${MANIFEST_OUTPUT}"
popd >/dev/null

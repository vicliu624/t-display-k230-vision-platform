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
CPU1_REPOSITORY="https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt.git"
CPU1_COMMIT="abb07090ad8a666ed7a5e097b3c714b918731645"
TOOLCHAIN_ARCHIVE="riscv64-unknown-linux-musl-rv64imafdcv-lp64d-20230420.tar.bz2"
TOOLCHAIN_URL="https://github.com/kendryte/canmv_k230/releases/download/v1.1/${TOOLCHAIN_ARCHIVE}"
CPU1_RUNTIME_BASE="0x10000000"
CPU1_RUNTIME_SIZE="0x04000000"
CPU1_HEAP_SIZE="0x02000000"

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
mkdir -p "${CPU1_CACHE_ROOT}" "${CPU1_TOOLCHAIN_DIR}" "$(dirname "${FIRMWARE_OUTPUT}")"

if [ ! -x "${CPU1_TOOLCHAIN_DIR}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-gcc" ]; then
	archive_path="${CPU1_TOOLCHAIN_DIR}/${TOOLCHAIN_ARCHIVE}"
	if [ ! -f "${archive_path}" ]; then
		curl --fail --location --retry 5 \
			--output "${archive_path}.part" "${TOOLCHAIN_URL}"
		mv "${archive_path}.part" "${archive_path}"
	fi
	tar -xjf "${archive_path}" -C "${CPU1_TOOLCHAIN_DIR}"
fi

CPU1_CROSS_COMPILE="${CPU1_TOOLCHAIN_DIR}/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-"
[ -x "${CPU1_CROSS_COMPILE}gcc" ] || fail "RT-Smart cross compiler is unavailable"

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
			'/canmv_k230/src/rtsmart/parse_config/' \
			'/canmv_k230/src/rtsmart/rtsmart/' \
			'/canmv_k230/src/opensbi/'
	} > "${CPU1_SPARSE_FILE}"
fi
git -C "${CPU1_CHECKOUT_DIR}" fetch --depth=1 --filter=blob:limit=1048576 \
	origin "${CPU1_COMMIT}"
git -C "${CPU1_CHECKOUT_DIR}" checkout --detach --force "${CPU1_COMMIT}"
git -C "${CPU1_CHECKOUT_DIR}" read-tree -mu HEAD

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

APPLICATION_DIR="${CPU1_SOURCE_DIR}/src/rtsmart/rtsmart/kernel/bsp/maix3/applications"
SCONSCRIPT="${APPLICATION_DIR}/SConscript"
require_file "${SCONSCRIPT}"
require_file "${CPU1_SOURCE_DIR}/src/rtsmart/Makefile"
require_file "${CPU1_SOURCE_DIR}/src/opensbi/Makefile"
mkdir -p "${CPU1_SOURCE_DIR}/src/rtsmart/mpp/include/comm"
install -m 0644 "${SCRIPT_DIR}/tdvp_cpu1_service.c" "${APPLICATION_DIR}/tdvp_cpu1_service.c"
install -m 0644 "${ABI_HEADER}" "${APPLICATION_DIR}/tdvp_cpu1_abi.h"
if ! grep -Fq "tdvp_cpu1_service.c" "${SCONSCRIPT}"; then
	sed -i "/src[[:space:]]*+=[[:space:]]*Glob('mnt.c')/a src += Glob('tdvp_cpu1_service.c')" "${SCONSCRIPT}"
fi
grep -Fq "tdvp_cpu1_service.c" "${SCONSCRIPT}" || fail "cannot add CPU1 service to RT-Smart SConscript"

pushd "${CPU1_SOURCE_DIR}" >/dev/null
run_sdk_make k230_canmv_v3p0_defconfig
for setting in \
	"CONFIG_MEM_BASE_ADDR=${CPU1_RUNTIME_BASE}" \
	"CONFIG_MEM_RTSMART_BASE=${CPU1_RUNTIME_BASE}" \
	"CONFIG_MEM_RTSMART_SIZE=${CPU1_RUNTIME_SIZE}" \
	"CONFIG_MEM_RTSMART_HEAP_SIZE=${CPU1_HEAP_SIZE}"; do
	key="${setting%%=*}"
	if grep -q "^${key}=" .config; then
		sed -i "s/^${key}=.*/${setting}/" .config
	else
		printf '%s\n' "${setting}" >> .config
	fi
done
run_sdk_make .autoconf

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
export SDK_OPENSBI_SRC_DIR="${CPU1_SOURCE_DIR}/src/opensbi"
export SDK_OPENSBI_BUILD_DIR="${SDK_BUILD_DIR}/opensbi"
export NCPUS="${TDVP_CPU1_JOBS:-2}"
mkdir -p "${SDK_BUILD_IMAGES_DIR}" "${SDK_RTSMART_BUILD_DIR}" "${SDK_OPENSBI_BUILD_DIR}"

run_sdk_make -C "${SDK_RTSMART_SRC_DIR}" kernel CROSS_COMPILE="${CPU1_CROSS_COMPILE}"
run_sdk_make -C "${SDK_OPENSBI_SRC_DIR}" all CROSS_COMPILE="${CPU1_CROSS_COMPILE}"
CPU1_FIRMWARE="${SDK_BUILD_IMAGES_DIR}/opensbi/opensbi_rtt_system.bin"
require_file "${CPU1_FIRMWARE}"
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
	printf 'mailbox_physical=0x13ff0000\n'
	printf 'firmware_size='
	stat -c '%s' "${FIRMWARE_OUTPUT}"
	printf 'firmware_sha256='
	sha256sum "${FIRMWARE_OUTPUT}" | awk '{print $1}'
} > "${MANIFEST_OUTPUT}"
popd >/dev/null

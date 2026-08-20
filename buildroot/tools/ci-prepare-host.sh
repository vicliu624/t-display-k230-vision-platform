#!/usr/bin/env bash
set -euo pipefail

# CI never uses the SDK's mutable installer directly. This keeps the cached
# compiler in the workspace cache and verifies the vendor's published digest
# before making the pinned path available under /opt/toolchain.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CACHE_ROOT="${TDVP_CACHE_ROOT:-${PROJECT_DIR}/.ci-cache}"
TOOLCHAIN_NAME="Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V3.0.2"
TOOLCHAIN_ARCHIVE="${TOOLCHAIN_NAME}-20250410.tar.gz"
TOOLCHAIN_MD5="8cefc7e94f760eaecc3620ffb238bf4a"
TOOLCHAIN_DIR="${CACHE_ROOT}/toolchain/${TOOLCHAIN_NAME}"
TOOLCHAIN_ARCHIVE_PATH="${CACHE_ROOT}/toolchain/${TOOLCHAIN_ARCHIVE}"
PRIMARY_URI="https://ai.b-bug.org/k230/downloads/dl/gcc/${TOOLCHAIN_ARCHIVE}"
FALLBACK_URI="https://download.kendryte.com/k230/downloads/dl/gcc/${TOOLCHAIN_ARCHIVE}"

sudo apt-get update
sudo apt-get install -y \
    bc binutils bison build-essential bzip2 cpio curl diffutils e2fsprogs file flex gawk git \
    libncurses-dev libssl-dev make parted patch perl python3-pcpp rsync \
    unzip wget xz-utils

mkdir -p "${CACHE_ROOT}/toolchain"
if [ ! -x "${TOOLCHAIN_DIR}/bin/riscv64-unknown-linux-gnu-gcc" ]; then
    rm -rf "${TOOLCHAIN_DIR}"
    rm -f "${TOOLCHAIN_ARCHIVE_PATH}"
    if ! curl --fail --location --retry 3 --output "${TOOLCHAIN_ARCHIVE_PATH}" "${PRIMARY_URI}"; then
        curl --fail --location --retry 3 --output "${TOOLCHAIN_ARCHIVE_PATH}" "${FALLBACK_URI}"
    fi
    actual_md5="$(md5sum "${TOOLCHAIN_ARCHIVE_PATH}" | awk '{print $1}')"
    if [ "${actual_md5}" != "${TOOLCHAIN_MD5}" ]; then
        printf 'TDVP CI: toolchain digest mismatch: expected %s, got %s\n' \
            "${TOOLCHAIN_MD5}" "${actual_md5}" >&2
        exit 1
    fi
    tar -xf "${TOOLCHAIN_ARCHIVE_PATH}" -C "${CACHE_ROOT}/toolchain"
fi

sudo mkdir -p /opt/toolchain
sudo ln -sfn "${TOOLCHAIN_DIR}" "/opt/toolchain/${TOOLCHAIN_NAME}"
printf 'TDVP CI: host and pinned toolchain are ready\n'

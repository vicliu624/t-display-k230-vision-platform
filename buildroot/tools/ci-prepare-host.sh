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

# GitHub's mirror list can select an unreachable Azure HTTP endpoint. Keep
# the other runner mirrors, but use the official HTTPS archive for that entry.
if [[ -f /etc/apt/apt-mirrors.txt ]]; then
    sudo sed -i 's|http://azure.archive.ubuntu.com/ubuntu|https://archive.ubuntu.com/ubuntu|g' /etc/apt/apt-mirrors.txt
fi
apt_options=(-o Acquire::Retries=2 -o Acquire::http::Timeout=30
    -o Acquire::https::Timeout=30 -o APT::Update::Error-Mode=any)
sudo timeout --kill-after=30s 5m apt-get "${apt_options[@]}" update
sudo timeout --kill-after=30s 10m apt-get "${apt_options[@]}" install -y \
    bc binutils bison build-essential bzip2 cpio curl diffutils e2fsprogs file flex gawk git \
	libncurses-dev libssl-dev make parted patch perl python3-pcpp python3-pycryptodome rsync scons u-boot-tools \
    unzip wget xz-utils libarchive-dev pkg-config libmenu-cache-bin

command -v mkimage >/dev/null
/usr/bin/python3 -c 'from Cryptodome.Cipher import AES; from Cryptodome.PublicKey import RSA'

mkdir -p "${CACHE_ROOT}/toolchain"
if [ ! -x "${TOOLCHAIN_DIR}/bin/riscv64-unknown-linux-gnu-gcc" ]; then
    rm -rf "${TOOLCHAIN_DIR}"
    rm -f "${TOOLCHAIN_ARCHIVE_PATH}"
    download_options=(--fail --location --connect-timeout 30 --max-time 600
        --speed-limit 1024 --speed-time 60 --retry 2 --retry-max-time 600)
    if ! curl "${download_options[@]}" --output "${TOOLCHAIN_ARCHIVE_PATH}" "${PRIMARY_URI}"; then
        curl "${download_options[@]}" --output "${TOOLCHAIN_ARCHIVE_PATH}" "${FALLBACK_URI}"
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

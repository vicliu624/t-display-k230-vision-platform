#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
	printf '%s\n' 'Usage: collect-release-bundle.sh <sdk-worktree> <release-name>' >&2
	exit 2
fi

WORKTREE="$(cd "$1" && pwd)"
RELEASE_NAME="$2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROFILE="k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
IMAGES="${WORKTREE}/output/${PROFILE}/images"
RELEASE_DIR="${GITHUB_WORKSPACE:-${PROJECT_DIR}}/release/${RELEASE_NAME}"

bash "${SCRIPT_DIR}/assert-public-release.sh" "${WORKTREE}"
rm -rf "${RELEASE_DIR}"
mkdir -p "${RELEASE_DIR}"
cp "${IMAGES}/sysimage-sdcard.img" "${RELEASE_DIR}/${RELEASE_NAME}.img"
cp "${IMAGES}/sysimage-sdcard.img.gz" "${RELEASE_DIR}/${RELEASE_NAME}.img.gz"
cp "${IMAGES}/tdvp-image-manifest" "${RELEASE_DIR}/tdvp-image-manifest"
cp "${WORKTREE}/.tdvp/sdk-baseline-manifest" "${RELEASE_DIR}/tdvp-sdk-baseline-manifest"
cat > "${RELEASE_DIR}/README.txt" <<EOF
${RELEASE_NAME}

Write the uncompressed .img to the complete SD card device. The image contains
the U-Boot payload, boot partition, root filesystem, seatd, Labwc, sfwbar,
swaybg, foot, and the K230 hardware integration services. Standard XDG desktop
entries are used for application discovery; no second desktop-shell install is
required after flashing.
EOF
(
	cd "${RELEASE_DIR}"
	sha256sum "${RELEASE_NAME}.img" "${RELEASE_NAME}.img.gz" \
		tdvp-image-manifest tdvp-sdk-baseline-manifest README.txt > SHA256SUMS
)
printf 'TDVP product release bundle: %s\n' "${RELEASE_DIR}"

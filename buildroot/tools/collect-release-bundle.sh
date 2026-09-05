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
case "${RELEASE_NAME}" in
	''|.|..|*/*|*'\\'*)
		printf 'TDVP release collector: invalid release name: %s\n' "${RELEASE_NAME}" >&2
		exit 2
		;;
esac
# The release handoff must be visible to the Windows workspace owner.  The SDK
# worktree remains an ext4-only disposable build cache; never make it the
# delivery location.  CI uses the same repository-relative output path.
RELEASE_DIR="${PROJECT_DIR}/output/${RELEASE_NAME}"

bash "${SCRIPT_DIR}/assert-public-release.sh" "${WORKTREE}"
[ ! -e "${RELEASE_DIR}" ] || {
	printf 'TDVP release collector: destination already exists: %s\n' "${RELEASE_DIR}" >&2
	printf '%s\n' 'TDVP release collector: choose a new release name; existing output is preserved.' >&2
	exit 1
}
mkdir -p "${RELEASE_DIR}"
cp "${IMAGES}/sysimage-sdcard.img.gz" "${RELEASE_DIR}/${RELEASE_NAME}.img.gz"
cp "${IMAGES}/tdvp-image-manifest" "${RELEASE_DIR}/tdvp-image-manifest"
cp "${IMAGES}/tdvp-cpu1-rtsmart.bin" "${RELEASE_DIR}/tdvp-cpu1-rtsmart.bin"
cp "${IMAGES}/tdvp-cpu1-rtsmart.manifest" "${RELEASE_DIR}/tdvp-cpu1-rtsmart.manifest"
cp "${WORKTREE}/.tdvp/sdk-baseline-manifest" "${RELEASE_DIR}/tdvp-sdk-baseline-manifest"
cat > "${RELEASE_DIR}/README.txt" <<EOF
${RELEASE_NAME}

Write ${RELEASE_NAME}.img.gz to the complete SD-card device with a writer that
supports compressed images, or stream-decompress it directly to the device.
Do not first materialize and retain an uncompressed .img on the workstation.
The image contains the U-Boot payload, PARTUUID-rooted boot and root partitions,
systemd, NetworkManager, greetd, Labwc, PCManFM, Raspberry Pi wf-panel-pi,
Foot, nm-connection-editor, standard libcanberra event sounds and the signed
TDVP opkg feed trust bootstrap. On a larger unpartitioned card the root
partition expands once at first boot; the image does not create /data.

CPU1 runs the included RT-Smart/OpenSBI image from the SD card raw 10--30 MiB
slot. Linux remains CPU0-only; use /usr/local/bin/tdvp-cpu1ctl (status, ping,
or crc32) or libtdvp_cpu1.so.1 to submit supported CPU1 coprocessor commands.

After login, use the LilyGO Menu key for the categorized application menu and
Alt+F4 to close a full-screen application. Verify packages with:
  sudo tdvp-opkg update
The configured feed requires its embedded release public key; do not disable
signature verification.
EOF
(
	cd "${RELEASE_DIR}"
	sha256sum "${RELEASE_NAME}.img.gz" \
		tdvp-image-manifest tdvp-cpu1-rtsmart.bin tdvp-cpu1-rtsmart.manifest \
		tdvp-sdk-baseline-manifest README.txt > SHA256SUMS
)
printf 'TDVP product release bundle: %s\n' "${RELEASE_DIR}"

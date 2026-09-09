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
bash "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/verify-opkg-rootfs.sh" \
	"${IMAGES}/rootfs.ext2" "${RELEASE_DIR}"
cp "${WORKTREE}/output/${PROFILE}/build/tdvp-package-info.json" "${RELEASE_DIR}/tdvp-buildroot-packages.json"
cat > "${RELEASE_DIR}/README.txt" <<EOF
${RELEASE_NAME}

Write ${RELEASE_NAME}.img.gz to the complete SD-card device with a writer that
supports compressed images, or stream-decompress it directly to the device.
Do not first materialize and retain an uncompressed .img on the workstation.
The image contains the U-Boot payload, PARTUUID-rooted boot and root partitions,
systemd, NetworkManager, a VGLite greeter and Labwc desktop, PCManFM,
Raspberry Pi wf-panel-pi, Foot, nm-connection-editor, gtklock,
standard libcanberra event sounds and the signed
TDVP opkg feed trust bootstrap. On a larger unpartitioned card the root
partition expands once at first boot; the image does not create /data.

CPU1 runs the included RT-Smart/OpenSBI image from the SD card raw 10--30 MiB
slot. Linux remains CPU0-only. CPU1 owns GC2093 capture, vision buffers,
KPU, AI2D, FFT and AI memory. Linux clients use /dev/tdvp-vision for frames
and /dev/tdvp-ai for supported asynchronous jobs. The current AI interface
supports limited AI2D, FFT/IFFT and a fixed KWS reference model.
General model loading and speech-to-text remain future work.
The separate basic mailbox tool tdvp-cpu1ctl provides status, ping and crc32.

After login, use the LilyGO Menu key for the categorized application menu and
Alt+F4 to close a full-screen application. The default idle policy displays
the gtklock password window after 300 seconds and turns the screen off at
330 seconds. Wake the display and enter the logged-in account's password.

Package installation and upgrades are paused following the 2026-09-09
CPU0/RVV runtime-library incompatibility found during NetSurf acceptance.
Validate this image before resuming feed tests. The configured stable feed
uses the embedded public key; keep signature verification enabled.
See docs/package-feed-status.md in the matching source revision for evidence
and the remaining package/boot acceptance requirements.

tdvp-image-base.json records final image file hashes, modes and package owners.
tdvp-opkg-status and tdvp-opkg-info.tar.gz are exported from the packaged ext4
filesystem. tdvp-buildroot-packages.json records the selected source versions.
The image-owned tdvp-image-* packages are held and essential; applications
must depend on the published image's exact providers and add new files.

This bundle alone is not yet a software-feed build baseline: matching portable
SDK/sysroot publication and image/feed device acceptance are still pending.
Do not bind or promote a public software feed using a temporary CI artifact.
EOF
(
	cd "${RELEASE_DIR}"
	sha256sum "${RELEASE_NAME}.img.gz" \
		tdvp-image-manifest tdvp-cpu1-rtsmart.bin tdvp-cpu1-rtsmart.manifest \
		tdvp-sdk-baseline-manifest tdvp-image-base.json tdvp-opkg-status \
		tdvp-opkg-info.tar.gz tdvp-buildroot-packages.json README.txt > SHA256SUMS
)
printf 'TDVP product release bundle: %s\n' "${RELEASE_DIR}"

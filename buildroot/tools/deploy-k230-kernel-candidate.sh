#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REMOTE="${ROOT}/buildroot/tools/tdvp-remote.sh"

usage() {
	cat >&2 <<'EOF'
Usage:
  TDVP_REMOTE_PASSWORD=... deploy-k230-kernel-candidate.sh <Image> <k230-canmv-rm69a10.dtb>

Deploy a verified, matching K230 Linux Image and board DTB set to the existing
SD-card boot partition. The target keeps two equivalent DTB names for its
U-Boot environments: k.dtb and k230-canmv-rm69a10.dtb. Both are backed up and
replaced together. The tool verifies every staged and installed file by
SHA-256 before it reports success.
EOF
}

if [ "$#" -ne 2 ]; then
	usage
	exit 2
fi

IMAGE="$(readlink -f "$1")"
DTB="$(readlink -f "$2")"
if [ ! -f "${IMAGE}" ]; then
	printf 'kernel candidate does not exist: %s\n' "${IMAGE}" >&2
	exit 2
fi
if [ ! -f "${DTB}" ]; then
	printf 'DTB candidate does not exist: %s\n' "${DTB}" >&2
	exit 2
fi

IMAGE_SHA256="$(sha256sum "${IMAGE}" | awk '{print $1}')"
IMAGE_SIZE="$(stat -c '%s' "${IMAGE}")"
DTB_SHA256="$(sha256sum "${DTB}" | awk '{print $1}')"
DTB_SIZE="$(stat -c '%s' "${DTB}")"
REMOTE_STAGE="/tmp/tdvp-boot-candidate-$$-$(date -u +%Y%m%dT%H%M%SZ)"
BACKUP_ID="boot-$(date -u +%Y%m%dT%H%M%SZ)"

remote() {
	"${REMOTE}" "$1"
}

cleanup_remote_stage() {
	remote "rm -rf ${REMOTE_STAGE}" >/dev/null 2>&1 || true
}
trap cleanup_remote_stage EXIT

printf 'TDVP kernel deploy: candidate=%s\n' "${IMAGE}"
printf 'TDVP kernel deploy: SHA-256=%s size=%s\n' "${IMAGE_SHA256}" "${IMAGE_SIZE}"
printf 'TDVP DTB deploy: candidate=%s\n' "${DTB}"
printf 'TDVP DTB deploy: SHA-256=%s size=%s\n' "${DTB_SHA256}" "${DTB_SIZE}"

remote "set -eu; rm -rf ${REMOTE_STAGE}; mkdir -p ${REMOTE_STAGE}"
"${REMOTE}" --copy "${IMAGE}" "${REMOTE_STAGE}/Image"
"${REMOTE}" --copy "${DTB}" "${REMOTE_STAGE}/k230-canmv-rm69a10.dtb"

remote "set -eu; test \"\$(sha256sum ${REMOTE_STAGE}/Image | awk '{print \$1}')\" = \"${IMAGE_SHA256}\"; test \"\$(wc -c < ${REMOTE_STAGE}/Image | tr -d ' ')\" = \"${IMAGE_SIZE}\"; echo TDVP-remote-stage-verified"
remote "set -eu; test \"\$(sha256sum ${REMOTE_STAGE}/k230-canmv-rm69a10.dtb | awk '{print \$1}')\" = \"${DTB_SHA256}\"; test \"\$(wc -c < ${REMOTE_STAGE}/k230-canmv-rm69a10.dtb | tr -d ' ')\" = \"${DTB_SIZE}\"; echo TDVP-remote-dtb-stage-verified"

remote "set -eu; mount_dir=/mnt/tdvp-kernel-update; backup_dir=/var/lib/tdvp/boot-backup/${BACKUP_ID}; mkdir -p \"\$mount_dir\" \"\$backup_dir\"; if mountpoint -q \"\$mount_dir\"; then umount \"\$mount_dir\"; fi; mount /dev/mmcblk1p1 \"\$mount_dir\"; test -f \"\$mount_dir/Image\"; test -f \"\$mount_dir/k.dtb\"; test -f \"\$mount_dir/k230-canmv-rm69a10.dtb\"; cmp -s \"\$mount_dir/k.dtb\" \"\$mount_dir/k230-canmv-rm69a10.dtb\"; cp -p \"\$mount_dir/Image\" \"\$backup_dir/Image\"; cp -p \"\$mount_dir/k.dtb\" \"\$backup_dir/k.dtb\"; cp -p \"\$mount_dir/k230-canmv-rm69a10.dtb\" \"\$backup_dir/k230-canmv-rm69a10.dtb\"; sha256sum \"\$backup_dir/Image\" \"\$backup_dir/k.dtb\" \"\$backup_dir/k230-canmv-rm69a10.dtb\" > \"\$backup_dir/SHA256SUMS\"; cp ${REMOTE_STAGE}/Image \"\$mount_dir/Image.tdvp-new\"; cp ${REMOTE_STAGE}/k230-canmv-rm69a10.dtb \"\$mount_dir/k.dtb.tdvp-new\"; cp ${REMOTE_STAGE}/k230-canmv-rm69a10.dtb \"\$mount_dir/k230-canmv-rm69a10.dtb.tdvp-new\"; sync; mv -f \"\$mount_dir/Image.tdvp-new\" \"\$mount_dir/Image\"; mv -f \"\$mount_dir/k.dtb.tdvp-new\" \"\$mount_dir/k.dtb\"; mv -f \"\$mount_dir/k230-canmv-rm69a10.dtb.tdvp-new\" \"\$mount_dir/k230-canmv-rm69a10.dtb\"; sync; image_actual=\$(sha256sum \"\$mount_dir/Image\" | awk '{print \$1}'); k_dtb_actual=\$(sha256sum \"\$mount_dir/k.dtb\" | awk '{print \$1}'); board_dtb_actual=\$(sha256sum \"\$mount_dir/k230-canmv-rm69a10.dtb\" | awk '{print \$1}'); test \"\$image_actual\" = \"${IMAGE_SHA256}\"; test \"\$k_dtb_actual\" = \"${DTB_SHA256}\"; test \"\$board_dtb_actual\" = \"${DTB_SHA256}\"; umount \"\$mount_dir\"; echo TDVP-boot-deployed backup=\"\$backup_dir\" image_sha256=\"\$image_actual\" dtb_sha256=\"\$board_dtb_actual\""

printf 'TDVP boot deploy: complete. Reboot separately after reviewing the backup path above.\n'

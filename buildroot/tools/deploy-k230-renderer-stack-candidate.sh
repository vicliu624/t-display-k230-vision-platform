#!/usr/bin/env bash
set -euo pipefail

# Install only the K230 renderer candidate files from an already-built
# Buildroot target tree.  This intentionally is not an image writer: it keeps
# the expanded rootfs and /home untouched, snapshots the previous renderer
# stack on the target, checks every staged SHA-256, and uses same-filesystem
# renames for each replacement.  Pair this with deploy-k230-kernel-candidate.sh
# when the candidate also contains a kernel/DTB change.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REMOTE="${ROOT}/buildroot/tools/tdvp-remote.sh"

usage() {
	cat >&2 <<'EOF'
Usage:
  TDVP_REMOTE_PASSWORD=... deploy-k230-renderer-stack-candidate.sh <Buildroot-target-dir>

Deploy the bounded renderer/Gate 0.5/Gate 1 runtime from an existing Buildroot
target directory.  It does not create /etc/tdvp/labwc/vglite-enabled, select
the VGLite profile, start/stop greetd, or reboot.  Those are separate gate
decisions after the new kernel has passed direct-KMS acceptance.
EOF
}

if [ "$#" -ne 1 ]; then
	usage
	exit 2
fi

TARGET_DIR="$(readlink -f "$1")"
if [ ! -d "${TARGET_DIR}" ]; then
	printf 'renderer candidate target directory does not exist: %s\n' "${TARGET_DIR}" >&2
	exit 2
fi

# Keep this list intentionally small and ownership-oriented.  It includes the
# actual compositor runtime, the profile/session boundary, the binaries and
# systemd units needed to execute Gate 0.5/Gate 1, and one read-only KMS
# capability observer for the later direct-scanout/fence decision. It does not
# carry user configuration, desktop data, or arbitrary files from the rootfs
# image.
FILES=(
	/usr/bin/labwc
	/usr/lib/libwlroots-0.18.so
	/usr/lib/libvg_lite.so
	/usr/bin/tdvp-display-smoke
	/usr/bin/tdvp-vblank-observer
	/usr/bin/tdvp-kms-capability-observer
	/usr/bin/tdvp-vglite-probe
	/usr/bin/tdvp-vglite-client-churn
	/usr/bin/tdvp-vglite-watchdog-observer
	/usr/bin/tdvp-vglite-session-gate
	/usr/bin/tdvp-vglite-diagnostics-report
	/usr/bin/tdvp-vglite-inflight-close-gate
	/usr/bin/tdvp-wayland-shm-bench
	/usr/bin/tdvp-wayland-shm-bench-session
	/usr/local/bin/tdvp-labwc-session
	/usr/local/bin/tdvp-renderer-profile
	/usr/libexec/tdvp/tdvp-kms-acceptance
	/usr/libexec/tdvp/tdvp-kms-maintenance-recover
	/usr/lib/systemd/system/tdvp-kms-acceptance.service
	/usr/lib/systemd/system/tdvp-kms-maintenance.service
	/usr/lib/udev/rules.d/60-tdvp-vg-lite.rules
	/etc/tdvp/labwc/environment
)

for path in "${FILES[@]}"; do
	if [ ! -f "${TARGET_DIR}${path}" ]; then
		printf 'renderer candidate is missing required file: %s\n' "${TARGET_DIR}${path}" >&2
		exit 2
fi
done

candidate_mode() {
	case "$1" in
		/etc/tdvp/labwc/environment|\
		/usr/lib/systemd/system/tdvp-kms-acceptance.service|\
		/usr/lib/systemd/system/tdvp-kms-maintenance.service|\
		/usr/lib/udev/rules.d/60-tdvp-vg-lite.rules)
			printf '%s\n' 644
			;;
		*)
			# Buildroot installs executables and shared objects in this bounded
			# runtime set as 0755. Do not infer it from a host-side staging
			# filesystem: Windows/NTFS extraction can discard POSIX execute bits.
			printf '%s\n' 755
			;;
	esac
}

REMOTE_STAGE="/tmp/tdvp-renderer-candidate-$$-$(date -u +%Y%m%dT%H%M%SZ)"
BACKUP_ID="renderer-stack-$(date -u +%Y%m%dT%H%M%SZ)"

remote() {
	"${REMOTE}" "$1"
}

cleanup_remote_stage() {
	remote "rm -rf ${REMOTE_STAGE}" >/dev/null 2>&1 || true
}

local_manifest="$(mktemp)"
cleanup_local_manifest() {
	rm -f "${local_manifest}"
}

# Uploading each of the bounded candidate files in a separate SSH/SCP session
# is needlessly fragile over an intermittent remote link. Build one archive
# from the fixed FILES list, transfer it once to the target's namespaced /tmp
# stage, then retain the existing per-file manifest verification before any
# backup or active-file mutation. The archive is never extracted outside that
# freshly created stage directory.
payload_archive="$(mktemp)"
cleanup_payload_archive() {
	rm -f "${payload_archive}"
}

cleanup() {
	cleanup_local_manifest
	cleanup_payload_archive
	cleanup_remote_stage
}
trap cleanup EXIT

# Manifest is <sha256><space><octal-mode><space><absolute-target-path>.  All
# paths are generated above, so the target-side shell never parses user input
# as a command or accepts a target outside the renderer stack list.
for path in "${FILES[@]}"; do
	candidate="${TARGET_DIR}${path}"
	sha256sum "${candidate}" | awk -v mode="$(candidate_mode "${path}")" -v path="${path}" \
		'{print $1 " " mode " " path}' >>"${local_manifest}"
done
tar -C "${TARGET_DIR}" -cf "${payload_archive}" "${FILES[@]#/}"

printf 'TDVP renderer deploy: candidate=%s\n' "${TARGET_DIR}"
printf 'TDVP renderer deploy: file_count=%s backup_id=%s\n' "${#FILES[@]}" "${BACKUP_ID}"
cat "${local_manifest}"

remote "set -eu; rm -rf ${REMOTE_STAGE}; mkdir -p ${REMOTE_STAGE}/root"
"${REMOTE}" --copy "${local_manifest}" "${REMOTE_STAGE}/MANIFEST"
"${REMOTE}" --copy "${payload_archive}" "${REMOTE_STAGE}/PAYLOAD.tar"
remote "set -eu; tar -xf ${REMOTE_STAGE}/PAYLOAD.tar -C ${REMOTE_STAGE}/root; rm -f ${REMOTE_STAGE}/PAYLOAD.tar"

# Validate the candidate payload before making a backup or changing a target
# file.  sha256sum and cmp are available from the target's BusyBox baseline.
remote "set -eu; while IFS=' ' read -r expected mode path; do test -n \"\$expected\"; test -n \"\$mode\"; case \"\$path\" in /usr/bin/labwc|/usr/lib/libwlroots-0.18.so|/usr/lib/libvg_lite.so|/usr/bin/tdvp-display-smoke|/usr/bin/tdvp-vblank-observer|/usr/bin/tdvp-kms-capability-observer|/usr/bin/tdvp-vglite-probe|/usr/bin/tdvp-vglite-client-churn|/usr/bin/tdvp-vglite-watchdog-observer|/usr/bin/tdvp-vglite-session-gate|/usr/bin/tdvp-vglite-diagnostics-report|/usr/bin/tdvp-vglite-inflight-close-gate|/usr/bin/tdvp-wayland-shm-bench|/usr/bin/tdvp-wayland-shm-bench-session|/usr/local/bin/tdvp-labwc-session|/usr/local/bin/tdvp-renderer-profile|/usr/libexec/tdvp/tdvp-kms-acceptance|/usr/libexec/tdvp/tdvp-kms-maintenance-recover|/usr/lib/systemd/system/tdvp-kms-acceptance.service|/usr/lib/systemd/system/tdvp-kms-maintenance.service|/usr/lib/udev/rules.d/60-tdvp-vg-lite.rules|/etc/tdvp/labwc/environment) ;; *) echo \"invalid manifest target: \$path\" >&2; exit 1;; esac; test \"\$(sha256sum ${REMOTE_STAGE}/root\$path | awk '{print \$1}')\" = \"\$expected\"; done < ${REMOTE_STAGE}/MANIFEST; echo TDVP-renderer-stage-verified"

# Back up every existing file before replacement.  A newly introduced file is
# explicitly recorded as absent so a rollback tool can remove it; the backup
# never follows or modifies /home.  The manifest and duplicate payload hashes
# are left under /var/lib/tdvp for local recovery after a network outage.
remote "set -eu; backup=/var/lib/tdvp/renderer-stack-backup/${BACKUP_ID}; mkdir -p \"\$backup/root\"; umask 077; : > \"\$backup/previous.manifest\"; while IFS=' ' read -r expected mode path; do if [ -e \"\$path\" ]; then test -f \"\$path\"; mkdir -p \"\$backup/root\$(dirname \"\$path\")\"; cp -p \"\$path\" \"\$backup/root\$path\"; actual=\$(sha256sum \"\$path\" | awk '{print \$1}'); printf 'present %s %s %s\\n' \"\$actual\" \"\$mode\" \"\$path\" >> \"\$backup/previous.manifest\"; else printf 'absent - %s %s\\n' \"\$mode\" \"\$path\" >> \"\$backup/previous.manifest\"; fi; done < ${REMOTE_STAGE}/MANIFEST; find \"\$backup/root\" -type f -exec sha256sum {} \\; > \"\$backup/SHA256SUMS\"; cp ${REMOTE_STAGE}/MANIFEST \"\$backup/candidate.manifest\"; sync; echo TDVP-renderer-backup-created=\"\$backup\""

# All staged files are now known-good and every old byte has a board-local
# backup.  Copy to a temporary sibling, apply the candidate's mode, then
# rename.  The active Labwc image keeps its already-mapped old files until the
# separately requested reboot, avoiding an in-session renderer transition.
remote "set -eu; while IFS=' ' read -r expected mode path; do temporary=\"\${path}.tdvp-new.\$\$\"; rm -f \"\$temporary\"; cp ${REMOTE_STAGE}/root\$path \"\$temporary\"; chmod \"\$mode\" \"\$temporary\"; test \"\$(sha256sum \"\$temporary\" | awk '{print \$1}')\" = \"\$expected\"; mv -f \"\$temporary\" \"\$path\"; test \"\$(sha256sum \"\$path\" | awk '{print \$1}')\" = \"\$expected\"; done < ${REMOTE_STAGE}/MANIFEST; mkdir -p /var/lib/tdvp/renderer-stack-state; cp ${REMOTE_STAGE}/MANIFEST /var/lib/tdvp/renderer-stack-state/active.manifest; printf 'backup_id=%s\\ninstalled_epoch=%s\\n' '${BACKUP_ID}' \"\$(date +%s)\" > /var/lib/tdvp/renderer-stack-state/active; systemctl daemon-reload; sync; echo TDVP-renderer-deployed backup=/var/lib/tdvp/renderer-stack-backup/${BACKUP_ID}"

printf 'TDVP renderer deploy: complete. The current session still uses its mapped old binaries; reboot separately before running Gate 0.5.\n'

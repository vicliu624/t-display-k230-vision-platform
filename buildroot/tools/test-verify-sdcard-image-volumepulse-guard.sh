#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERIFY="${SCRIPT_DIR}/../k230-sdk-overlay/board/tdvp/verify-sdcard-image.sh"

fail() {
	printf 'test-verify-sdcard-image-volumepulse-guard: FAIL: %s\n' "$*" >&2
	exit 1
}

[ -f "${VERIFY}" ] || fail "missing verifier: ${VERIFY}"
bash -n "${VERIFY}"

grep -Fq 'reject_rootfs_content()' "${VERIFY}" ||
	fail 'missing binary-content rejection helper'
grep -Fq 'dump ${path} ${temporary_file}' "${VERIFY}" ||
	fail 'negative binary check no longer extracts the rootfs payload'
grep -Fq "reject_rootfs_content '/usr/lib/wf-panel-pi/libvolumepulse.so' 'systemctl --user -q is-active pipewire-pulse.service'" \
	"${VERIFY}" || fail 'volumepulse user-systemd probe is no longer rejected from the final rootfs'
grep -Fq "require_rootfs_content '/usr/lib/wf-panel-pi/libvolumepulse.so' 'audio-volume-change'" \
	"${VERIFY}" || fail 'volumepulse functional audio contract is no longer required'

printf '%s\n' 'test-verify-sdcard-image-volumepulse-guard: PASS'

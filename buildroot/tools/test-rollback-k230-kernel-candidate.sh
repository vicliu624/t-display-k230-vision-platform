#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROLLBACK="${SCRIPT_DIR}/rollback-k230-kernel-candidate.sh"

fail() {
	printf 'test-rollback-k230-kernel-candidate: FAIL: %s\n' "$*" >&2
	exit 1
}

[ -f "${ROLLBACK}" ] || fail "missing rollback helper"
bash -n "${ROLLBACK}"

# The ID check is deliberately done before the remote helper can receive a
# value. This prevents a backup path from becoming a remote shell argument.
if TDVP_REMOTE_PASSWORD=test bash "${ROLLBACK}" '../boot-20260101T000000Z' >/dev/null 2>&1; then
	fail "path-traversal backup id was accepted"
fi
if TDVP_REMOTE_PASSWORD=test bash "${ROLLBACK}" 'boot-20260101T000000Z;reboot' >/dev/null 2>&1; then
	fail "shell-metacharacter backup id was accepted"
fi

grep -Fq 'sha256sum \"\$file\"' "${ROLLBACK}" ||
	fail "backup payload is not verified before restore"
grep -Fq 'mount /dev/mmcblk1p1 \"\$mount_dir\"' "${ROLLBACK}" ||
	fail "rollback does not target the established boot partition"
grep -Fq 'tdvp-rollback.' "${ROLLBACK}" ||
	fail "rollback does not use temporary sibling files"
grep -Fq 'cmp -s \"\$mount_dir/k.dtb\" \"\$mount_dir/k230-canmv-rm69a10.dtb\"' "${ROLLBACK}" ||
	fail "rollback does not verify both DTB aliases"
grep -Fq 'Reboot separately' "${ROLLBACK}" ||
	fail "rollback unexpectedly hides its reboot boundary"

printf '%s\n' 'test-rollback-k230-kernel-candidate: PASS'

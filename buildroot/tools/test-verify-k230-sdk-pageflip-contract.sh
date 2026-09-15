#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERIFY="${SCRIPT_DIR}/verify-k230-sdk-pageflip-contract.sh"
PATCH_NAME='0053-tdvp-drm-canaan-page-flip-lifecycle.patch'
KERNEL_SOURCE="${TDVP_KERNEL_SOURCE:-${PROJECT_DIR}/.tmp/linux-xuantie-kernel-tdvp-clean}"

fail() {
	printf 'test-verify-k230-sdk-pageflip-contract: FAIL: %s\n' "$*" >&2
	exit 1
}

[ -x "$VERIFY" ] || fail "missing verifier: $VERIFY"
[ -d "$KERNEL_SOURCE" ] || fail "set TDVP_KERNEL_SOURCE to a clean 7d4e1f kernel checkout"
git -C "$KERNEL_SOURCE" rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
	fail "not a Git kernel checkout: $KERNEL_SOURCE"

temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/tdvp-pageflip-contract-test.XXXXXX")"
cleanup() {
	rm -rf "$temporary_dir"
}
trap cleanup EXIT HUP INT TERM

sdk_worktree="${temporary_dir}/sdk"
output_dir="${sdk_worktree}/output/profile"
mkdir -p "${sdk_worktree}/buildroot-overlay/linux" \
	"${sdk_worktree}/output/buildroot-2025.02.1/linux" \
	"$output_dir"
cp "${PROJECT_DIR}/buildroot/k230-sdk-overlay/linux/${PATCH_NAME}" \
	"${sdk_worktree}/buildroot-overlay/linux/${PATCH_NAME}"
cp "${PROJECT_DIR}/buildroot/k230-sdk-overlay/linux/${PATCH_NAME}" \
	"${sdk_worktree}/output/buildroot-2025.02.1/linux/${PATCH_NAME}"
kernel_commit="$(git -C "$KERNEL_SOURCE" rev-parse HEAD)"
printf 'BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="%s"\n' "$kernel_commit" > "${output_dir}/.config"

before_status="$(git -C "$KERNEL_SOURCE" status --porcelain)"
TDVP_KERNEL_SOURCE="$KERNEL_SOURCE" "$VERIFY" "$sdk_worktree" "$output_dir" >/dev/null
after_status="$(git -C "$KERNEL_SOURCE" status --porcelain)"
[ "$before_status" = "$after_status" ] ||
	fail 'verifier modified the kernel checkout instead of using an alternate index'

printf 'BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="invalid"\n' > "${output_dir}/.config"
if TDVP_KERNEL_SOURCE="$KERNEL_SOURCE" "$VERIFY" "$sdk_worktree" "$output_dir" >/dev/null 2>&1; then
	fail 'verifier accepted a kernel source commit that differs from Buildroot config'
fi

printf '%s\n' 'test-verify-k230-sdk-pageflip-contract: PASS'

#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RECONCILE="${SCRIPT_DIR}/reconcile-k230-sdk-linux-patches.sh"
OVERLAY_SOURCE="${PROJECT_DIR}/buildroot/k230-sdk-overlay/linux"

if [ -n "${TDVP_RECONCILE_TEST_TMPDIR:-}" ]; then
	[ -d "${TDVP_RECONCILE_TEST_TMPDIR}" ] || {
		printf '%s\n' "TDVP_RECONCILE_TEST_TMPDIR is not a directory: ${TDVP_RECONCILE_TEST_TMPDIR}" >&2
		exit 2
	}
	TEMP_DIR="${TDVP_RECONCILE_TEST_TMPDIR}/tdvp-reconcile-linux-patches-test.$$"
	mkdir -p "${TEMP_DIR}"
else
	TEMP_DIR="$(mktemp -d)"
fi

WORKTREE="${TEMP_DIR}/sdk-worktree"
PATCH_DIR="${WORKTREE}/output/buildroot-2025.02.1/linux"
OVERLAY_DIR="${WORKTREE}/buildroot-overlay/linux"
RETIRED_CPU1_PATCH="0053-tdvp-cpu1-rtsmart-mailbox.patch"

cleanup() {
	rm -rf "${TEMP_DIR}"
}
trap cleanup EXIT

mkdir -p "${PATCH_DIR}" "${OVERLAY_DIR}"
# Use the reviewed payloads so production reconciliation validates the real
# queue as well as removing retired names. A synthetic patch can hide a
# structural error in an active patch before Buildroot consumes it.
cp -a "${OVERLAY_SOURCE}/." "${OVERLAY_DIR}/"

# Simulate an SDK output directory reused from the pre-VGLite CPU1 branch.
# The old 0053 and current 0063 payloads are deliberately byte-identical;
# retaining both would cause Buildroot to apply the mailbox change twice.
cp -a "${OVERLAY_DIR}/0063-tdvp-cpu1-rtsmart-mailbox.patch" \
	"${PATCH_DIR}/${RETIRED_CPU1_PATCH}"

bash "${RECONCILE}" "${WORKTREE}" >/dev/null

[ ! -e "${PATCH_DIR}/${RETIRED_CPU1_PATCH}" ] || {
	printf '%s\n' "test-reconcile-k230-sdk-linux-patches: FAIL retained ${RETIRED_CPU1_PATCH}" >&2
	exit 1
}

for active_patch in \
	0053-tdvp-drm-canaan-page-flip-lifecycle.patch \
	0054-tdvp-vglite-per-client-resource-ownership.patch \
	0055-tdvp-vglite-no-reset-on-secondary-open.patch \
	0056-tdvp-vglite-serialize-submit-through-wait.patch \
	0057-tdvp-vglite-command-engine-guard-edges.patch \
	0058-tdvp-vglite-abort-inflight-close-safely.patch \
	0059-tdvp-vglite-bound-infinite-wait-watchdog.patch \
	0060-tdvp-vglite-atomic-interrupt-flags.patch \
	0061-tdvp-vglite-enforce-single-context-open.patch \
	0062-tdvp-vglite-yield-after-early-completion.patch \
	0063-tdvp-cpu1-rtsmart-mailbox.patch \
	0064-tdvp-riscv-dts-use-scalar-cpu0.patch; do
	cmp -s "${OVERLAY_DIR}/${active_patch}" "${PATCH_DIR}/${active_patch}" || {
		printf '%s\n' "test-reconcile-k230-sdk-linux-patches: FAIL active patch differs: ${active_patch}" >&2
		exit 1
	}
done

printf '%s\n' 'test-reconcile-k230-sdk-linux-patches: PASS real queue validated; stale CPU1 patch removed'

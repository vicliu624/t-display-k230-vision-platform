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
mkdir -p "${OVERLAY_DIR}/vendor"

write_valid_patch() {
	cat > "$1" <<'EOF'
diff --git a/test-file b/test-file
--- a/test-file
+++ b/test-file
@@ -1 +1 @@
-before
+after
EOF
}

# Keep the fixture aligned to the real overlay's active names while using
# minimal valid patch payloads.  This isolates stale-name reconciliation from
# the separate content validation covered by the production overlay checks.
while IFS= read -r -d '' overlay_patch; do
	write_valid_patch "${OVERLAY_DIR}/$(basename "${overlay_patch}")"
done < <(find "${OVERLAY_SOURCE}" -maxdepth 1 -type f -name '*.patch' -print0)
write_valid_patch "${OVERLAY_DIR}/vendor/0019-dts-add-nonai2d.patch"

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
	0063-tdvp-cpu1-rtsmart-mailbox.patch; do
	cmp -s "${OVERLAY_DIR}/${active_patch}" "${PATCH_DIR}/${active_patch}" || {
		printf '%s\n' "test-reconcile-k230-sdk-linux-patches: FAIL active patch differs: ${active_patch}" >&2
		exit 1
	}
done

printf '%s\n' 'test-reconcile-k230-sdk-linux-patches: PASS stale CPU1 patch removed'

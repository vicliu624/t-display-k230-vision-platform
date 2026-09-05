#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERIFY="${SCRIPT_DIR}/verify-tdvp-renderer-stack-lock.sh"
SDK_VG_LITE="${PROJECT_DIR}/vendor/k230_linux_sdk/buildroot-overlay/package/vg_lite"
OVERLAY_VG_LITE="${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vg_lite"
if [ -n "${TDVP_RENDERER_STACK_TEST_TMPDIR:-}" ]; then
	[ -d "${TDVP_RENDERER_STACK_TEST_TMPDIR}" ] || {
		printf '%s\n' "TDVP_RENDERER_STACK_TEST_TMPDIR is not a directory: ${TDVP_RENDERER_STACK_TEST_TMPDIR}" >&2
		exit 2
	}
	TEMP_DIR="${TDVP_RENDERER_STACK_TEST_TMPDIR}/tdvp-renderer-stack-lock-test.$$"
	mkdir -p "${TEMP_DIR}"
else
	TEMP_DIR="$(mktemp -d)"
fi
WORKTREE="${TEMP_DIR}/sdk-worktree"
BASELINE="${TEMP_DIR}/renderer-stack.before"
MUTATED="${TEMP_DIR}/renderer-stack.after"

cleanup() {
	rm -rf "${TEMP_DIR}"
}
trap cleanup EXIT

mkdir -p "${WORKTREE}/buildroot-overlay/package"
mkdir -p "${WORKTREE}/buildroot-overlay/package/vg_lite"
cp -a "${SDK_VG_LITE}/." "${WORKTREE}/buildroot-overlay/package/vg_lite/"
# Match prepare-k230-sdk-worktree.sh: TDVP owns the recipe while the SDK owns
# the VGLite source subdirectories that remain after the overlay merge.
cp -a "${OVERLAY_VG_LITE}/." "${WORKTREE}/buildroot-overlay/package/vg_lite/"

TDVP_RENDERER_STACK_TMPDIR="${TEMP_DIR}" \
	bash "${VERIFY}" --stage "${WORKTREE}" > "${BASELINE}"
grep -Fqx 'renderer_stack_wlroots_commit=94bca3e871ec4cce73afbef7bad4d962331ab9bb' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_page_flip_patch_file=buildroot/k230-sdk-overlay/linux/0053-tdvp-drm-canaan-page-flip-lifecycle.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_page_flip_patch_sha256=1bbbdf04294c0e919929e42dd1046b47abe26047ce98de430c66c8124a8eebec' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_patch_file=buildroot/k230-sdk-overlay/linux/0054-tdvp-vglite-per-client-resource-ownership.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_patch_sha256=de6516114beabbdffacb3a8e0b9d5f62b2004f2e3be78ddf952c2791f89c3fc3' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_open_lifetime_patch_file=buildroot/k230-sdk-overlay/linux/0055-tdvp-vglite-no-reset-on-secondary-open.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_open_lifetime_patch_sha256=b74248c90334a1f73d6f6beb07eec1fda0ab11451938dd4b9e9309ea28aa04ca' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_submit_wait_patch_file=buildroot/k230-sdk-overlay/linux/0056-tdvp-vglite-serialize-submit-through-wait.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_submit_wait_patch_sha256=01c4fd82fd126a7723c336758c45c20cf1722c427a23997c97cf4ccd93351f41' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_guard_edges_patch_file=buildroot/k230-sdk-overlay/linux/0057-tdvp-vglite-command-engine-guard-edges.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_guard_edges_patch_sha256=b9e0bd17b19dbc7d9f88b43f0ca05621e8f0ff8efc4bcf89d6e2cad0124a3634' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_inflight_close_patch_file=buildroot/k230-sdk-overlay/linux/0058-tdvp-vglite-abort-inflight-close-safely.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_inflight_close_patch_sha256=2e488514a3af16f4cb1b2f5f1deaed4b3cecf8a79d062ae3ca2adfca38183784' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_wait_watchdog_patch_file=buildroot/k230-sdk-overlay/linux/0059-tdvp-vglite-bound-infinite-wait-watchdog.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_wait_watchdog_patch_sha256=e8c2e71065701289b930be91b24c8914cf02c01e2b956fc5269d0ef93f04b27f' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_interrupt_atomicity_patch_file=buildroot/k230-sdk-overlay/linux/0060-tdvp-vglite-atomic-interrupt-flags.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_interrupt_atomicity_patch_sha256=04239961629c2e323a3466913668121c25c7b4f4f6785e4082fd608953e80ce5' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_single_context_patch_file=buildroot/k230-sdk-overlay/linux/0061-tdvp-vglite-enforce-single-context-open.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_single_context_patch_sha256=601a5f3fb452225846f09f51bf1b79ea3a52c19ce7d4cd1ea78acfa2447fda49' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_completion_idle_yield_patch_file=buildroot/k230-sdk-overlay/linux/0062-tdvp-vglite-yield-after-early-completion.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_kernel_vglite_completion_idle_yield_patch_sha256=e8f06d7721703f6a80ea442a29530a86941d94b476db3bf83cac58163a00b17b' "${BASELINE}"
grep -Fqx 'renderer_stack_wlroots_patch_file=buildroot/k230-sdk-overlay/package/wlroots/0001-tdvp-vglite-linear-shm-lifecycle.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_wlroots_patch_sha256=6c13ba0f76cd68e07badc89ac6f5e0bc997f0ad8b48a918abdc1ca147d22aecb' "${BASELINE}"
grep -Fqx 'renderer_stack_labwc_commit=9af441ecd36bbee66d4df46baa7b482872d989f2' "${BASELINE}"
grep -Fqx 'renderer_stack_labwc_vglite_recovery_patch_file=buildroot/k230-sdk-overlay/package/labwc/0004-tdvp-vglite-render-failure-recovery.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_labwc_vglite_recovery_patch_sha256=995b2771ab2ddbe46d7566d00aab7ddf1d4b95ff6faa280aef525d27d0d4c7b2' "${BASELINE}"
grep -Fqx 'renderer_stack_vglite_close_on_exec_patch_file=buildroot/k230-sdk-overlay/package/vg_lite/0001-tdvp-vglite-close-on-exec.patch' "${BASELINE}"
grep -Fqx 'renderer_stack_vglite_close_on_exec_patch_sha256=3ba530078ea2a5cb6153c2657702d7ac0ab0cc16a1c3b136b612e144d4a00782' "${BASELINE}"
grep -Eq '^renderer_stack_vglite_sdk_git_state=(clean|dirty)$' "${BASELINE}"
grep -Eq '^renderer_stack_vglite_package_sha256=[0-9a-f]{64}$' "${BASELINE}"
grep -Eq '^renderer_stack_vglite_package_files=[1-9][0-9]*$' "${BASELINE}"

printf '%s\n' '/* test-only staged VGLite mutation */' \
	>> "${WORKTREE}/buildroot-overlay/package/vg_lite/VGLite/vg_lite.c"
TDVP_RENDERER_STACK_TMPDIR="${TEMP_DIR}" \
	bash "${VERIFY}" --stage "${WORKTREE}" > "${MUTATED}"

before_sha="$(sed -n 's/^renderer_stack_vglite_package_sha256=//p' "${BASELINE}")"
after_sha="$(sed -n 's/^renderer_stack_vglite_package_sha256=//p' "${MUTATED}")"
[ -n "${before_sha}" ] && [ -n "${after_sha}" ] && [ "${before_sha}" != "${after_sha}" ] || {
	printf '%s\n' 'test-tdvp-renderer-stack-lock: FAIL VGLite package fingerprint did not change' >&2
	exit 1
}

printf '%s\n' 'test-tdvp-renderer-stack-lock: PASS'

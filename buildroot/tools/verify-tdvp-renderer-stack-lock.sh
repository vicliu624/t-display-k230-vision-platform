#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  verify-tdvp-renderer-stack-lock.sh --stage <sdk-worktree>

Validate the immutable wlroots/Labwc/SDK identities that make up the TDVP
VGLite renderer stack, then emit the effective VGLite package fingerprint for
the staged SDK worktree. The output is deliberately manifest-friendly.
EOF
}

if [ "$#" -ne 2 ] || [ "$1" != "--stage" ]; then
	usage
	exit 2
fi

WORKTREE="$(cd "$2" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SOURCE_LOCK="${PROJECT_DIR}/buildroot/sdk-sources.lock"
SDK_DIR="${PROJECT_DIR}/vendor/k230_linux_sdk"
OVERLAY_DIR="${PROJECT_DIR}/buildroot/k230-sdk-overlay"
STAGED_VG_LITE_DIR="${WORKTREE}/buildroot-overlay/package/vg_lite"
WLROOTS_MK="${OVERLAY_DIR}/package/wlroots/wlroots.mk"
WLROOTS_HASH="${OVERLAY_DIR}/package/wlroots/wlroots.hash"
WLROOTS_PATCH_DIR="${OVERLAY_DIR}/package/wlroots"
LABWC_MK="${OVERLAY_DIR}/package/labwc/labwc.mk"

fail() {
	printf 'TDVP renderer stack lock: %s\n' "$*" >&2
	exit 1
}

require_file() {
	[ -f "$1" ] || fail "missing file: $1"
}

require_content() {
	grep -aFq -- "$2" "$1" || fail "missing content in $1: $2"
}

lock_value() {
	local section="$1"
	local key="$2"
	local value

	value="$(awk -v section="${section}" -v key="${key}" '
		$0 == "[" section "]" { in_section = 1; next }
		/^\[/ { in_section = 0 }
		in_section && index($0, key " = ") == 1 {
			print substr($0, length(key) + 4)
			exit
		}
	' "${SOURCE_LOCK}")"
	[ -n "${value}" ] || fail "missing ${section}.${key} in ${SOURCE_LOCK}"
	printf '%s\n' "${value}"
}

normalized_file_sha256() {
	local file="$1"

	if LC_ALL=C grep -Iq . "${file}"; then
		sed 's/\r$//' "${file}" | sha256sum | awk '{print $1}'
	else
		sha256sum "${file}" | awk '{print $1}'
	fi
}

temporary_file() {
	if [ -n "${TDVP_RENDERER_STACK_TMPDIR:-}" ]; then
		[ -d "${TDVP_RENDERER_STACK_TMPDIR}" ] ||
			fail "TDVP_RENDERER_STACK_TMPDIR is not a directory: ${TDVP_RENDERER_STACK_TMPDIR}"
		printf '%s/vglite-package-manifest.%s\n' \
			"${TDVP_RENDERER_STACK_TMPDIR}" "$$"
		return
	fi

	mktemp
}

write_tree_manifest() {
	local source_dir="$1"

	(
		cd "${source_dir}"
		find . -type f -print0 | LC_ALL=C sort -z |
			while IFS= read -r -d '' file; do
				hash="$(normalized_file_sha256 "${file}")"
				printf '%s  %s\n' "${hash}" "${file#./}"
			done
	)
}

require_file "${SOURCE_LOCK}"
require_file "${WLROOTS_MK}"
require_file "${WLROOTS_HASH}"
require_file "${LABWC_MK}"
require_file "${STAGED_VG_LITE_DIR}/Config.in"
require_file "${STAGED_VG_LITE_DIR}/vg_lite.mk"
require_file "${STAGED_VG_LITE_DIR}/0001-tdvp-vglite-close-on-exec.patch"
require_file "${STAGED_VG_LITE_DIR}/VGLite/vg_lite.c"
require_file "${STAGED_VG_LITE_DIR}/inc/vg_lite.h"

sdk_source="$(lock_value k230_linux_sdk source)"
sdk_commit="$(lock_value k230_linux_sdk commit)"
kernel_source="$(lock_value linux_kernel source)"
kernel_commit="$(lock_value linux_kernel commit)"
kernel_page_flip_patch_file="$(lock_value linux_kernel page_flip_lifecycle_patch)"
kernel_page_flip_patch_sha256="$(lock_value linux_kernel page_flip_lifecycle_patch_sha256)"
kernel_vglite_patch_file="$(lock_value linux_kernel vglite_resource_ownership_patch)"
kernel_vglite_patch_sha256="$(lock_value linux_kernel vglite_resource_ownership_patch_sha256)"
kernel_vglite_open_lifetime_patch_file="$(lock_value linux_kernel vglite_open_lifetime_patch)"
kernel_vglite_open_lifetime_patch_sha256="$(lock_value linux_kernel vglite_open_lifetime_patch_sha256)"
kernel_vglite_submit_wait_patch_file="$(lock_value linux_kernel vglite_submit_wait_lease_patch)"
kernel_vglite_submit_wait_patch_sha256="$(lock_value linux_kernel vglite_submit_wait_lease_patch_sha256)"
kernel_vglite_guard_edges_patch_file="$(lock_value linux_kernel vglite_command_engine_guard_edges_patch)"
kernel_vglite_guard_edges_patch_sha256="$(lock_value linux_kernel vglite_command_engine_guard_edges_patch_sha256)"
kernel_vglite_inflight_close_patch_file="$(lock_value linux_kernel vglite_inflight_close_recovery_patch)"
kernel_vglite_inflight_close_patch_sha256="$(lock_value linux_kernel vglite_inflight_close_recovery_patch_sha256)"
kernel_vglite_wait_watchdog_patch_file="$(lock_value linux_kernel vglite_wait_watchdog_patch)"
kernel_vglite_wait_watchdog_patch_sha256="$(lock_value linux_kernel vglite_wait_watchdog_patch_sha256)"
kernel_vglite_interrupt_atomicity_patch_file="$(lock_value linux_kernel vglite_interrupt_atomicity_patch)"
kernel_vglite_interrupt_atomicity_patch_sha256="$(lock_value linux_kernel vglite_interrupt_atomicity_patch_sha256)"
kernel_vglite_single_context_patch_file="$(lock_value linux_kernel vglite_single_context_open_patch)"
kernel_vglite_single_context_patch_sha256="$(lock_value linux_kernel vglite_single_context_open_patch_sha256)"
kernel_vglite_completion_idle_yield_patch_file="$(lock_value linux_kernel vglite_completion_idle_yield_patch)"
kernel_vglite_completion_idle_yield_patch_sha256="$(lock_value linux_kernel vglite_completion_idle_yield_patch_sha256)"
wlroots_source="$(lock_value wlroots_vglite source)"
wlroots_commit="$(lock_value wlroots_vglite commit)"
wlroots_version="$(lock_value wlroots_vglite version)"
wlroots_abi="$(lock_value wlroots_vglite abi)"
wlroots_git4_sha256="$(lock_value wlroots_vglite git4_archive_sha256)"
wlroots_renderer="$(lock_value wlroots_vglite renderer)"
wlroots_patch_file="$(lock_value wlroots_vglite patch_file)"
wlroots_patch_sha256="$(lock_value wlroots_vglite patch_sha256)"
labwc_source="$(lock_value labwc source)"
labwc_commit="$(lock_value labwc commit)"
labwc_abi="$(lock_value labwc wlroots_abi)"
labwc_vglite_recovery_patch_file="$(lock_value labwc vglite_render_recovery_patch)"
labwc_vglite_recovery_patch_sha256="$(lock_value labwc vglite_render_recovery_patch_sha256)"
vglite_sdk_source="$(lock_value vglite_userspace sdk_source)"
vglite_sdk_commit="$(lock_value vglite_userspace sdk_commit)"
vglite_sdk_package_path="$(lock_value vglite_userspace sdk_package_path)"
vglite_runtime_library="$(lock_value vglite_userspace runtime_library)"
vglite_close_on_exec_patch_file="$(lock_value vglite_userspace close_on_exec_patch)"
vglite_close_on_exec_patch_sha256="$(lock_value vglite_userspace close_on_exec_patch_sha256)"

[ "${sdk_source}" = "${vglite_sdk_source}" ] ||
	fail "VGLite SDK source does not match k230_linux_sdk source"
[ "${sdk_commit}" = "${vglite_sdk_commit}" ] ||
	fail "VGLite SDK commit does not match k230_linux_sdk commit"
[ "${kernel_page_flip_patch_file}" = "buildroot/k230-sdk-overlay/linux/0053-tdvp-drm-canaan-page-flip-lifecycle.patch" ] ||
	fail "unexpected page-flip kernel patch path: ${kernel_page_flip_patch_file}"
[ "${kernel_vglite_patch_file}" = "buildroot/k230-sdk-overlay/linux/0054-tdvp-vglite-per-client-resource-ownership.patch" ] ||
	fail "unexpected VGLite kernel patch path: ${kernel_vglite_patch_file}"
[ "${kernel_vglite_open_lifetime_patch_file}" = "buildroot/k230-sdk-overlay/linux/0055-tdvp-vglite-no-reset-on-secondary-open.patch" ] ||
	fail "unexpected VGLite open-lifetime patch path: ${kernel_vglite_open_lifetime_patch_file}"
[ "${kernel_vglite_submit_wait_patch_file}" = "buildroot/k230-sdk-overlay/linux/0056-tdvp-vglite-serialize-submit-through-wait.patch" ] ||
	fail "unexpected VGLite submit/wait patch path: ${kernel_vglite_submit_wait_patch_file}"
[ "${kernel_vglite_guard_edges_patch_file}" = "buildroot/k230-sdk-overlay/linux/0057-tdvp-vglite-command-engine-guard-edges.patch" ] ||
	fail "unexpected VGLite guard-edges patch path: ${kernel_vglite_guard_edges_patch_file}"
[ "${kernel_vglite_inflight_close_patch_file}" = "buildroot/k230-sdk-overlay/linux/0058-tdvp-vglite-abort-inflight-close-safely.patch" ] ||
	fail "unexpected VGLite in-flight-close patch path: ${kernel_vglite_inflight_close_patch_file}"
[ "${kernel_vglite_wait_watchdog_patch_file}" = "buildroot/k230-sdk-overlay/linux/0059-tdvp-vglite-bound-infinite-wait-watchdog.patch" ] ||
	fail "unexpected VGLite wait-watchdog patch path: ${kernel_vglite_wait_watchdog_patch_file}"
[ "${kernel_vglite_interrupt_atomicity_patch_file}" = "buildroot/k230-sdk-overlay/linux/0060-tdvp-vglite-atomic-interrupt-flags.patch" ] ||
	fail "unexpected VGLite interrupt-atomicity patch path: ${kernel_vglite_interrupt_atomicity_patch_file}"
[ "${kernel_vglite_single_context_patch_file}" = "buildroot/k230-sdk-overlay/linux/0061-tdvp-vglite-enforce-single-context-open.patch" ] ||
	fail "unexpected VGLite single-context patch path: ${kernel_vglite_single_context_patch_file}"
[ "${kernel_vglite_completion_idle_yield_patch_file}" = "buildroot/k230-sdk-overlay/linux/0062-tdvp-vglite-yield-after-early-completion.patch" ] ||
	fail "unexpected VGLite completion-idle-yield patch path: ${kernel_vglite_completion_idle_yield_patch_file}"
[ "${wlroots_abi}" = "${labwc_abi}" ] ||
	fail "Labwc and wlroots ABI locks differ"
[ "${wlroots_renderer}" = "vglite" ] ||
	fail "unsupported renderer lock: ${wlroots_renderer}"
[ "${wlroots_patch_file}" = "buildroot/k230-sdk-overlay/package/wlroots/0001-tdvp-vglite-linear-shm-lifecycle.patch" ] ||
	fail "unexpected wlroots VGLite patch path: ${wlroots_patch_file}"
[ "${labwc_vglite_recovery_patch_file}" = "buildroot/k230-sdk-overlay/package/labwc/0004-tdvp-vglite-render-failure-recovery.patch" ] ||
	fail "unexpected Labwc VGLite recovery patch path: ${labwc_vglite_recovery_patch_file}"
[ "${vglite_sdk_package_path}" = "buildroot-overlay/package/vg_lite" ] ||
	fail "unexpected VGLite SDK package path: ${vglite_sdk_package_path}"
[ "${vglite_runtime_library}" = "/usr/lib/libvg_lite.so" ] ||
	fail "unexpected VGLite runtime library: ${vglite_runtime_library}"
[ "${vglite_close_on_exec_patch_file}" = "buildroot/k230-sdk-overlay/package/vg_lite/0001-tdvp-vglite-close-on-exec.patch" ] ||
	fail "unexpected VGLite close-on-exec patch path: ${vglite_close_on_exec_patch_file}"

require_content "${WLROOTS_MK}" "WLROOTS_VERSION = ${wlroots_commit}"
require_content "${WLROOTS_MK}" "WLROOTS_SITE = ${wlroots_source}"
require_content "${WLROOTS_MK}" "WLROOTS_RENDERERS = vglite"
require_content "${WLROOTS_HASH}" "${wlroots_git4_sha256}  wlroots-${wlroots_commit}-git4.tar.gz"
kernel_page_flip_patch_path="${PROJECT_DIR}/${kernel_page_flip_patch_file}"
require_file "${kernel_page_flip_patch_path}"
[ "$(normalized_file_sha256 "${kernel_page_flip_patch_path}")" = "${kernel_page_flip_patch_sha256}" ] ||
	fail "page-flip kernel patch fingerprint does not match sdk-sources.lock"
require_content "${kernel_page_flip_patch_path}" 'Keep a vblank reference before making the VO GO write visible.'
require_content "${kernel_page_flip_patch_path}" 'VO_DISP_IRQ1_STATUS'
kernel_vglite_patch_path="${PROJECT_DIR}/${kernel_vglite_patch_file}"
require_file "${kernel_vglite_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_patch_path}")" = "${kernel_vglite_patch_sha256}" ] ||
	fail "VGLite kernel patch fingerprint does not match sdk-sources.lock"
require_content "${kernel_vglite_patch_path}" 'keep resources scoped to the owning file'
require_content "${kernel_vglite_patch_path}" 'active_client->mapped_list_head'
kernel_vglite_open_lifetime_patch_path="${PROJECT_DIR}/${kernel_vglite_open_lifetime_patch_file}"
require_file "${kernel_vglite_open_lifetime_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_open_lifetime_patch_path}")" = "${kernel_vglite_open_lifetime_patch_sha256}" ] ||
	fail "VGLite open-lifetime patch fingerprint does not match sdk-sources.lock"
kernel_vglite_submit_wait_patch_path="${PROJECT_DIR}/${kernel_vglite_submit_wait_patch_file}"
require_file "${kernel_vglite_submit_wait_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_submit_wait_patch_path}")" = "${kernel_vglite_submit_wait_patch_sha256}" ] ||
	fail "VGLite submit/wait patch fingerprint does not match sdk-sources.lock"
kernel_vglite_guard_edges_patch_path="${PROJECT_DIR}/${kernel_vglite_guard_edges_patch_file}"
require_file "${kernel_vglite_guard_edges_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_guard_edges_patch_path}")" = "${kernel_vglite_guard_edges_patch_sha256}" ] ||
	fail "VGLite guard-edges patch fingerprint does not match sdk-sources.lock"
kernel_vglite_inflight_close_patch_path="${PROJECT_DIR}/${kernel_vglite_inflight_close_patch_file}"
require_file "${kernel_vglite_inflight_close_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_inflight_close_patch_path}")" = "${kernel_vglite_inflight_close_patch_sha256}" ] ||
	fail "VGLite in-flight-close patch fingerprint does not match sdk-sources.lock"
kernel_vglite_wait_watchdog_patch_path="${PROJECT_DIR}/${kernel_vglite_wait_watchdog_patch_file}"
require_file "${kernel_vglite_wait_watchdog_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_wait_watchdog_patch_path}")" = "${kernel_vglite_wait_watchdog_patch_sha256}" ] ||
	fail "VGLite wait-watchdog patch fingerprint does not match sdk-sources.lock"
kernel_vglite_interrupt_atomicity_patch_path="${PROJECT_DIR}/${kernel_vglite_interrupt_atomicity_patch_file}"
require_file "${kernel_vglite_interrupt_atomicity_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_interrupt_atomicity_patch_path}")" = "${kernel_vglite_interrupt_atomicity_patch_sha256}" ] ||
	fail "VGLite interrupt-atomicity patch fingerprint does not match sdk-sources.lock"
kernel_vglite_single_context_patch_path="${PROJECT_DIR}/${kernel_vglite_single_context_patch_file}"
require_file "${kernel_vglite_single_context_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_single_context_patch_path}")" = "${kernel_vglite_single_context_patch_sha256}" ] ||
	fail "VGLite single-context patch fingerprint does not match sdk-sources.lock"
kernel_vglite_completion_idle_yield_patch_path="${PROJECT_DIR}/${kernel_vglite_completion_idle_yield_patch_file}"
require_file "${kernel_vglite_completion_idle_yield_patch_path}"
[ "$(normalized_file_sha256 "${kernel_vglite_completion_idle_yield_patch_path}")" = "${kernel_vglite_completion_idle_yield_patch_sha256}" ] ||
	fail "VGLite completion-idle-yield patch fingerprint does not match sdk-sources.lock"
wlroots_patch_path="${PROJECT_DIR}/${wlroots_patch_file}"
require_file "${wlroots_patch_path}"
[ "$(normalized_file_sha256 "${wlroots_patch_path}")" = "${wlroots_patch_sha256}" ] ||
	fail "wlroots VGLite patch fingerprint does not match sdk-sources.lock"
require_content "${wlroots_patch_path}" 'TDVP_VGLITE_DIAG stage=texture_upload'
require_content "${wlroots_patch_path}" 'texture_clip_rects=%zu'
require_content "${wlroots_patch_path}" 'solid_blit_attempts=%zu'
require_content "${wlroots_patch_path}" 'wlr_buffer_unlock(buffer);'
require_content "${wlroots_patch_path}" 'A non-NULL but empty clip means "draw nowhere", not "draw everywhere".'
require_content "${wlroots_patch_path}" 'data == NULL || texture->source.memory == NULL'
require_content "${wlroots_patch_path}" 'VGLite client texture has no CPU-accessible data'
require_content "${wlroots_patch_path}" 'copy_calls=%zu'
require_content "${wlroots_patch_path}" 'Copy it once instead of issuing one memcpy per row'
require_content "${wlroots_patch_path}" 'restore_default_render_state'
require_content "${wlroots_patch_path}" 'state_recovery_attempted=%d state_recovery_ok=%d'
require_content "${wlroots_patch_path}" 'pass->buffer->renderer->context_failed = true;'
require_content "${wlroots_patch_path}" 'VGLite renderer rejected a pass after a previous vg_lite_finish failure'
require_content "${wlroots_patch_path}" 'Skipping vg_lite_finish during failed-context teardown'
require_content "${wlroots_patch_path}" 'VGLite texture readback rejected after a previous vg_lite_finish failure'
require_content "${wlroots_patch_path}" 'VGLite texture readback could not synchronize GPU: %d; renderer quarantined'
require_content "${wlroots_patch_path}" 'VGLite texture update rejected after a previous vg_lite_finish failure'
require_content "${wlroots_patch_path}" 'VGLite texture creation rejected after a previous vg_lite_finish failure'
require_content "${wlroots_patch_path}" 'texture->renderer->context_failed = true;'
require_content "${wlroots_patch_path}" 'TDVP_VGLITE_DIAG stage=texture_readback_finish result=%d '
require_content "${wlroots_patch_path}" 'renderer_quarantined=1'
labwc_vglite_recovery_patch_path="${PROJECT_DIR}/${labwc_vglite_recovery_patch_file}"
require_file "${labwc_vglite_recovery_patch_path}"
[ "$(normalized_file_sha256 "${labwc_vglite_recovery_patch_path}")" = "${labwc_vglite_recovery_patch_sha256}" ] ||
	fail "Labwc VGLite recovery patch fingerprint does not match sdk-sources.lock"
require_content "${labwc_vglite_recovery_patch_path}" 'TDVP_LABWC_VGLITE_FAILURE_RECOVERY'
require_content "${labwc_vglite_recovery_patch_path}" 'TDVP VGLite failure limit reached'
require_content "${LABWC_MK}" "LABWC_VERSION = ${labwc_commit}"
require_content "${LABWC_MK}" "LABWC_SITE = ${labwc_source}"
require_content "${STAGED_VG_LITE_DIR}/vg_lite.mk" 'VG_LITE_SITE = $(realpath $(TOPDIR))"/package/vg_lite"'
require_content "${STAGED_VG_LITE_DIR}/vg_lite.mk" 'VG_LITE_SITE_METHOD = local'
require_content "${STAGED_VG_LITE_DIR}/vg_lite.mk" 'VG_LITE_DEPENDENCIES += libdrm'
require_content "${STAGED_VG_LITE_DIR}/vg_lite.mk" 'VG_LITE_PATCH = $(VG_LITE_SITE)/0001-tdvp-vglite-close-on-exec.patch'
# SITE_METHOD=local follows Buildroot's override-source rsync route, which
# deliberately bypasses generic-package's normal patch stage. The hook is the
# delivery contract: without it the locked patch would be present but unused.
require_content "${STAGED_VG_LITE_DIR}/vg_lite.mk" 'VG_LITE_POST_RSYNC_HOOKS += VG_LITE_APPLY_TDVP_CLOSE_ON_EXEC_PATCH'
require_content "${STAGED_VG_LITE_DIR}/vg_lite.mk" '$(APPLY_PATCHES) $(@D) $(VG_LITE_SITE) $(notdir $(VG_LITE_PATCH))'
vglite_close_on_exec_patch_path="${PROJECT_DIR}/${vglite_close_on_exec_patch_file}"
require_file "${vglite_close_on_exec_patch_path}"
[ "$(normalized_file_sha256 "${vglite_close_on_exec_patch_path}")" = "${vglite_close_on_exec_patch_sha256}" ] ||
	fail "VGLite close-on-exec patch fingerprint does not match sdk-sources.lock"
require_content "${vglite_close_on_exec_patch_path}" 'open("/dev/vg_lite", O_RDWR | O_CLOEXEC)'

# The SDK checkout may carry a deliberately reviewed local patch. Do not
# silently discard it: record its state and, more importantly, hash the exact
# package tree copied into the disposable worktree and passed to Buildroot.
sdk_vglite_git_state="unavailable"
if git -C "${SDK_DIR}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
	if git -C "${SDK_DIR}" status --porcelain -- \
		"${vglite_sdk_package_path}" | grep -q .; then
		sdk_vglite_git_state="dirty"
	else
		sdk_vglite_git_state="clean"
	fi
fi

renderer_stack_lock_sha256="$(sed 's/\r$//' "${SOURCE_LOCK}" | sha256sum | awk '{print $1}')"
vglite_package_manifest="$(temporary_file)"
trap 'rm -f "${vglite_package_manifest}"' EXIT
write_tree_manifest "${STAGED_VG_LITE_DIR}" > "${vglite_package_manifest}"
vglite_package_sha256="$(sha256sum "${vglite_package_manifest}" | awk '{print $1}')"
vglite_package_files="$(wc -l < "${vglite_package_manifest}" | tr -d '[:space:]')"

printf 'renderer_stack_lock=buildroot/sdk-sources.lock\n'
printf 'renderer_stack_lock_sha256=%s\n' "${renderer_stack_lock_sha256}"
printf 'renderer_stack_sdk_source=%s\n' "${sdk_source}"
printf 'renderer_stack_sdk_commit=%s\n' "${sdk_commit}"
printf 'renderer_stack_kernel_source=%s\n' "${kernel_source}"
printf 'renderer_stack_kernel_commit=%s\n' "${kernel_commit}"
printf 'renderer_stack_kernel_page_flip_patch_file=%s\n' "${kernel_page_flip_patch_file}"
printf 'renderer_stack_kernel_page_flip_patch_sha256=%s\n' "${kernel_page_flip_patch_sha256}"
printf 'renderer_stack_kernel_vglite_patch_file=%s\n' "${kernel_vglite_patch_file}"
printf 'renderer_stack_kernel_vglite_patch_sha256=%s\n' "${kernel_vglite_patch_sha256}"
printf 'renderer_stack_kernel_vglite_open_lifetime_patch_file=%s\n' "${kernel_vglite_open_lifetime_patch_file}"
printf 'renderer_stack_kernel_vglite_open_lifetime_patch_sha256=%s\n' "${kernel_vglite_open_lifetime_patch_sha256}"
printf 'renderer_stack_kernel_vglite_submit_wait_patch_file=%s\n' "${kernel_vglite_submit_wait_patch_file}"
printf 'renderer_stack_kernel_vglite_submit_wait_patch_sha256=%s\n' "${kernel_vglite_submit_wait_patch_sha256}"
printf 'renderer_stack_kernel_vglite_guard_edges_patch_file=%s\n' "${kernel_vglite_guard_edges_patch_file}"
printf 'renderer_stack_kernel_vglite_guard_edges_patch_sha256=%s\n' "${kernel_vglite_guard_edges_patch_sha256}"
printf 'renderer_stack_kernel_vglite_inflight_close_patch_file=%s\n' "${kernel_vglite_inflight_close_patch_file}"
printf 'renderer_stack_kernel_vglite_inflight_close_patch_sha256=%s\n' "${kernel_vglite_inflight_close_patch_sha256}"
printf 'renderer_stack_kernel_vglite_wait_watchdog_patch_file=%s\n' "${kernel_vglite_wait_watchdog_patch_file}"
printf 'renderer_stack_kernel_vglite_wait_watchdog_patch_sha256=%s\n' "${kernel_vglite_wait_watchdog_patch_sha256}"
printf 'renderer_stack_kernel_vglite_interrupt_atomicity_patch_file=%s\n' "${kernel_vglite_interrupt_atomicity_patch_file}"
printf 'renderer_stack_kernel_vglite_interrupt_atomicity_patch_sha256=%s\n' "${kernel_vglite_interrupt_atomicity_patch_sha256}"
printf 'renderer_stack_kernel_vglite_single_context_patch_file=%s\n' "${kernel_vglite_single_context_patch_file}"
printf 'renderer_stack_kernel_vglite_single_context_patch_sha256=%s\n' "${kernel_vglite_single_context_patch_sha256}"
printf 'renderer_stack_kernel_vglite_completion_idle_yield_patch_file=%s\n' "${kernel_vglite_completion_idle_yield_patch_file}"
printf 'renderer_stack_kernel_vglite_completion_idle_yield_patch_sha256=%s\n' "${kernel_vglite_completion_idle_yield_patch_sha256}"
printf 'renderer_stack_vglite_sdk_git_state=%s\n' "${sdk_vglite_git_state}"
printf 'renderer_stack_wlroots_source=%s\n' "${wlroots_source}"
printf 'renderer_stack_wlroots_commit=%s\n' "${wlroots_commit}"
printf 'renderer_stack_wlroots_version=%s\n' "${wlroots_version}"
printf 'renderer_stack_wlroots_abi=%s\n' "${wlroots_abi}"
printf 'renderer_stack_wlroots_git4_sha256=%s\n' "${wlroots_git4_sha256}"
printf 'renderer_stack_wlroots_patch_file=%s\n' "${wlroots_patch_file}"
printf 'renderer_stack_wlroots_patch_sha256=%s\n' "${wlroots_patch_sha256}"
printf 'renderer_stack_labwc_source=%s\n' "${labwc_source}"
printf 'renderer_stack_labwc_commit=%s\n' "${labwc_commit}"
printf 'renderer_stack_labwc_wlroots_abi=%s\n' "${labwc_abi}"
printf 'renderer_stack_labwc_vglite_recovery_patch_file=%s\n' "${labwc_vglite_recovery_patch_file}"
printf 'renderer_stack_labwc_vglite_recovery_patch_sha256=%s\n' "${labwc_vglite_recovery_patch_sha256}"
printf 'renderer_stack_vglite_package_path=%s\n' "${vglite_sdk_package_path}"
printf 'renderer_stack_vglite_close_on_exec_patch_file=%s\n' "${vglite_close_on_exec_patch_file}"
printf 'renderer_stack_vglite_close_on_exec_patch_sha256=%s\n' "${vglite_close_on_exec_patch_sha256}"
printf 'renderer_stack_vglite_package_sha256=%s\n' "${vglite_package_sha256}"
printf 'renderer_stack_vglite_package_files=%s\n' "${vglite_package_files}"

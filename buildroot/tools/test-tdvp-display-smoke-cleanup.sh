#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
project_root=$(CDPATH= cd -- "${script_dir}/../.." && pwd)
source_file="${project_root}/buildroot/k230-sdk-overlay/package/tdvp-display-smoke/src/tdvp-display-smoke.c"
image_guard_file="${project_root}/buildroot/k230-sdk-overlay/board/tdvp/verify-sdcard-image.sh"

test -f "${source_file}"
test -f "${image_guard_file}"

require_content() {
	grep -Fq -- "$2" "$1" || {
		printf 'test-tdvp-display-smoke-cleanup: missing %s\n' "$2" >&2
		exit 1
	}
}

require_content "${source_file}" 'static int tdvp_disable_test_scanout'
require_content "${source_file}" '"FB_ID", 0'
require_content "${source_file}" '"CRTC_ID", 0'
require_content "${source_file}" 'DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT'
require_content "${source_file}" 'tdvp_wait_for_page_flip(display, &page_flip,'
require_content "${source_file}" 'static int tdvp_wait_for_post_detach_vblank'
require_content "${source_file}" 'DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT'
require_content "${source_file}" 'drmWaitVBlank(post-detach guard)'
require_content "${source_file}" 'guard vblank sequence %u did not advance after detach sequence %u'
require_content "${source_file}" 'sequence_delta == 0 || sequence_delta > INT32_MAX'
require_content "${source_file}" 'tdvp_wait_for_post_detach_vblank(display,'
require_content "${source_file}" 'PASS test plane detached and guard vblank observed before buffer release'
require_content "${source_file}" 'display->page_flip_pending = true;'
require_content "${source_file}" 'display->page_flip_pending = false;'
require_content "${source_file}" 'refusing teardown while a page flip is pending'
require_content "${source_file}" 'deferring FB/dumb-buffer destruction to DRM close'
require_content "${image_guard_file}" "require_fs_path \"\${ROOTFS}\" '/usr/bin/tdvp-display-smoke'"
require_content "${image_guard_file}" "require_rootfs_content '/usr/bin/tdvp-display-smoke' 'guard vblank sequence %u did not advance after detach sequence %u'"

# The post-counter detach may not disable the CRTC: tdvp-kms-acceptance samples
# 120 further vblanks before it returns greetd to DRM master.
detach_block=$(awk '
/^static int tdvp_disable_test_scanout/ { capture = 1 }
capture {
	print
	open_braces += gsub(/\{/, "{")
	close_braces += gsub(/\}/, "}")
	if (open_braces > 0 && open_braces == close_braces)
		exit
}
' "${source_file}")
case "${detach_block}" in
	*'"ACTIVE", 0'*|*'"MODE_ID", 0'*)
		printf '%s\n' 'test-tdvp-display-smoke-cleanup: detach incorrectly disables the CRTC' >&2
		exit 1
		;;
esac

# In the only normal cleanup path, the event-gated plane detach must precede
# DRM FB/dumb-buffer destruction. This avoids relying on master-drop cleanup
# to race an active test plane.
run_smoke_start=$(grep -n '^static int tdvp_run_smoke' "${source_file}" |
	head -n 1 | cut -d: -f1)
disable_call=$(awk -v start="${run_smoke_start}" '
	NR > start && /tdvp_disable_test_scanout\(display,/ { print NR; exit }
' "${source_file}")
destroy_call=$(awk -v start="${disable_call}" '
	NR > start && /tdvp_destroy_buffer\(display, &buffers\[1\]\)/ { print NR; exit }
' "${source_file}")

case "${run_smoke_start}:${disable_call}:${destroy_call}" in
	*[!0-9:]*|*::*)
		printf '%s\n' 'test-tdvp-display-smoke-cleanup: cannot locate cleanup ordering' >&2
		exit 1
		;;
esac
[ "${disable_call}" -lt "${destroy_call}" ] || {
	printf '%s\n' 'test-tdvp-display-smoke-cleanup: buffers can be destroyed before scanout disable' >&2
	exit 1
}

detach_start=$(grep -n '^static int tdvp_disable_test_scanout' "${source_file}" |
	head -n 1 | cut -d: -f1)
page_flip_wait=$(awk -v start="${detach_start}" '
	NR > start && /tdvp_wait_for_page_flip\(display, &page_flip,/ { print NR; exit }
' "${source_file}")
guard_call=$(awk -v start="${detach_start}" '
	NR > start && /tdvp_wait_for_post_detach_vblank\(display,/ { print NR; exit }
' "${source_file}")
case "${detach_start}:${page_flip_wait}:${guard_call}:${disable_call}:${destroy_call}" in
	*[!0-9:]*|*::* )
		printf '%s\n' 'test-tdvp-display-smoke-cleanup: cannot locate guard-vblank ordering' >&2
		exit 1
		;;
esac
[ "${page_flip_wait}" -lt "${guard_call}" ] || {
	printf '%s\n' 'test-tdvp-display-smoke-cleanup: guard vblank is not after the detach event' >&2
	exit 1
}
sed -n "${guard_call},$((guard_call + 1))p" "${source_file}" | \
	grep -Fq 'page_flip_timeout_ms, page_flip.sequence)' || {
		printf '%s\n' 'test-tdvp-display-smoke-cleanup: guard vblank is not bound to the detach sequence' >&2
		exit 1
	}

printf '%s\n' 'test-tdvp-display-smoke-cleanup: PASS'

#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  verify-k230-sdk-pageflip-contract.sh <sdk-worktree> <buildroot-output>

Constructively verify the K230 Canaan page-flip patch against the kernel commit
selected by an already configured SDK Buildroot output. The check uses an
alternate Git index only; it does not modify the SDK kernel cache or build tree.

Set TDVP_KERNEL_SOURCE to override the SDK's dl/linux/git source cache (used by
the host-side regression test).
EOF
}

if [ "$#" -ne 2 ]; then
	usage
	exit 2
fi

WORKTREE="$(cd "$1" && pwd)"
OUTPUT_DIR="$(cd "$2" && pwd)"
CONFIG="${OUTPUT_DIR}/.config"
PATCH_NAME='0053-tdvp-drm-canaan-page-flip-lifecycle.patch'
STAGED_PATCH="${WORKTREE}/output/buildroot-2025.02.1/linux/${PATCH_NAME}"
OVERLAY_PATCH="${WORKTREE}/buildroot-overlay/linux/${PATCH_NAME}"
KERNEL_SOURCE="${TDVP_KERNEL_SOURCE:-${WORKTREE}/dl/linux/git}"

fail() {
	printf 'TDVP K230 page-flip contract: %s\n' "$*" >&2
	exit 1
}

require_content() {
	grep -aFq -- "$2" "$1" || fail "missing ${2@Q} in $1"
}

require_absent_code_call() {
	! grep -aEq -- "^[[:space:]]*${2}[[:space:]]*\\(" "$1" ||
		fail "unexpected executable ${2} call in $1"
}

first_line() {
	local file="$1"
	local needle="$2"
	local line

	line="$(grep -aFn -- "$needle" "$file" | sed -n '1s/:.*//p')"
	[ -n "$line" ] || fail "missing ${needle@Q} in $file"
	printf '%s\n' "$line"
}

first_line_after() {
	local file="$1"
	local after_line="$2"
	local needle="$3"
	local line

	line="$(grep -aFn -- "$needle" "$file" | awk -F: -v after="$after_line" '$1 > after { print $1; exit }')"
	[ -n "$line" ] || fail "missing ${needle@Q} after line ${after_line} in $file"
	printf '%s\n' "$line"
}

require_after() {
	local file="$1"
	local before="$2"
	local after="$3"
	local before_line
	local after_line

	before_line="$(first_line "$file" "$before")"
	after_line="$(first_line_after "$file" "$before_line" "$after")"
	[ "$after_line" -gt "$before_line" ] ||
		fail "${after@Q} is not after ${before@Q} in $file"
}

[ -f "$CONFIG" ] || fail "missing configured Buildroot output: $CONFIG"
[ -f "$STAGED_PATCH" ] || fail "missing staged page-flip patch: $STAGED_PATCH"
[ -f "$OVERLAY_PATCH" ] || fail "missing overlay page-flip patch: $OVERLAY_PATCH"
cmp -s "$OVERLAY_PATCH" "$STAGED_PATCH" ||
	fail 'staged page-flip patch differs from the reviewed overlay copy'

expected_commit="$(sed -n 's/^BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="\(.*\)"$/\1/p' "$CONFIG" | head -n 1)"
[ -n "$expected_commit" ] ||
	fail "missing BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION in $CONFIG"
[ -d "$KERNEL_SOURCE" ] || fail "kernel source is unavailable: $KERNEL_SOURCE"
git -C "$KERNEL_SOURCE" rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
	fail "kernel source is not a Git worktree: $KERNEL_SOURCE"
actual_commit="$(git -C "$KERNEL_SOURCE" rev-parse HEAD)"
[ "$actual_commit" = "$expected_commit" ] ||
	fail "kernel source commit differs from Buildroot config: expected=$expected_commit actual=$actual_commit"

temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/tdvp-pageflip-contract.XXXXXX")"
cleanup() {
	rm -rf "$temporary_dir"
}
trap cleanup EXIT HUP INT TERM
export GIT_INDEX_FILE="${temporary_dir}/index"

# Apply only to an alternate index. This validates exact patch applicability and
# lets the assertions inspect the resulting source without touching the SDK
# download cache which may be shared by parallel or later Buildroot builds.
# HEAD was compared with the fully pinned Buildroot commit above. Use HEAD here
# rather than repeating the 40-hex object name: some vendor SDK caches contain
# an accidental ref with that exact name, which makes Git emit an ambiguity
# warning despite the commit identity already being verified.
git -C "$KERNEL_SOURCE" read-tree HEAD
git -C "$KERNEL_SOURCE" apply --cached --check "$STAGED_PATCH"
git -C "$KERNEL_SOURCE" apply --cached "$STAGED_PATCH"

for source_file in \
	drivers/gpu/drm/canaan/canaan_crtc.h \
	drivers/gpu/drm/canaan/canaan_crtc.c \
	drivers/gpu/drm/canaan/canaan_drv.c \
	drivers/gpu/drm/canaan/canaan_vo.c \
	drivers/gpu/drm/canaan/canaan_vo_regs.h; do
	git -C "$KERNEL_SOURCE" show ":${source_file}" > "${temporary_dir}/$(basename "$source_file")"
done

crtc_header="${temporary_dir}/canaan_crtc.h"
crtc_source="${temporary_dir}/canaan_crtc.c"
driver_source="${temporary_dir}/canaan_drv.c"
vo_source="${temporary_dir}/canaan_vo.c"
vo_regs="${temporary_dir}/canaan_vo_regs.h"

# A single driver-owned pending event is safe only while the generic atomic
# helper retains its flip_done serialization. Treat a replacement commit path
# as an incompatible upstream change rather than silently accepting 0053.
require_content "$crtc_header" 'struct drm_pending_vblank_event *event;'
require_content "$driver_source" '.atomic_commit = drm_atomic_helper_commit,'
require_content "$driver_source" '.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,'

# The event and its vblank reference must be established before the VO GO write
# becomes visible. The IRQ then owns exactly one completion/release transition.
require_content "$crtc_source" 'vblank_ret = drm_crtc_vblank_get(crtc);'
require_content "$crtc_source" 'if (WARN_ON(canaan_crtc->event))'
require_content "$crtc_source" 'canaan_crtc->event = event;'
require_content "$crtc_source" 'drm_crtc_vblank_put(crtc);'
require_absent_code_call "$crtc_source" 'drm_crtc_arm_vblank_event'
require_after "$crtc_source" \
	'vblank_ret = drm_crtc_vblank_get(crtc);' \
	'canaan_crtc->event = event;'
require_after "$crtc_source" \
	'canaan_crtc->event = event;' \
	'canaan_vo_flush_config(vo);'
require_after "$crtc_source" \
	'canaan_vo_flush_config(vo);' \
	'spin_unlock_irq(&crtc->dev->event_lock);'

require_content "$vo_regs" '#define VO_DISP_IRQ1_STATUS BIT(1)'
require_content "$vo_regs" '#define VO_DISP_IRQ1_VTTH_ENABLE BIT(20)'
require_content "$vo_source" 'status = canaan_vo_read(vo, VO_DISP_IRQ_STATUS);'
require_content "$vo_source" 'canaan_vo_write(vo, VO_DISP_IRQ_STATUS, status);'
require_content "$vo_source" 'if (!(status & VO_DISP_IRQ1_STATUS)'
require_content "$vo_source" 'drm_crtc_handle_vblank(crtc);'
require_content "$vo_source" 'event = canaan_crtc->event;'
require_content "$vo_source" 'drm_crtc_send_vblank_event(crtc, event);'
require_after "$vo_source" \
	'status = canaan_vo_read(vo, VO_DISP_IRQ_STATUS);' \
	'canaan_vo_write(vo, VO_DISP_IRQ_STATUS, status);'
require_after "$vo_source" \
	'canaan_vo_write(vo, VO_DISP_IRQ_STATUS, status);' \
	'drm_crtc_handle_vblank(crtc);'
require_after "$vo_source" \
	'drm_crtc_handle_vblank(crtc);' \
	'event = canaan_crtc->event;'
require_after "$vo_source" \
	'event = canaan_crtc->event;' \
	'drm_crtc_send_vblank_event(crtc, event);'
require_after "$vo_source" \
	'drm_crtc_send_vblank_event(crtc, event);' \
	'drm_crtc_vblank_put(crtc);'

printf 'TDVP K230 page-flip contract: PASS kernel=%s patch=%s index=isolated\n' \
	"$actual_commit" "$PATCH_NAME"

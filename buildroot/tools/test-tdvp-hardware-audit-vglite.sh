#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUDIT="${SCRIPT_DIR}/tdvp-hardware-audit.sh"

fail() {
	printf '%s\n' "test-tdvp-hardware-audit-vglite: FAIL: $*" >&2
	exit 1
}

[ -f "${AUDIT}" ] || fail "missing hardware audit script"
bash -n "${AUDIT}"

# The recovery capture must distinguish a running 0059/0060 candidate from a
# pre-watchdog VGLite kernel, reconstruct the selected renderer/session, and
# preserve both userspace and kernel evidence after a frozen desktop returns.
for required in \
	"section 'VGLite / DRM / Labwc recovery evidence'" \
	'/proc/sys/kernel/random/boot_id' \
	'/sys/module/vglite/parameters/infinite_wait_watchdog_ms' \
	'/sys/module/vg_lite/parameters/infinite_wait_watchdog_ms' \
	'/usr/bin/tdvp-vglite-watchdog-observer' \
	'/usr/local/bin/tdvp-renderer-profile status' \
	"section 'Passive KMS format/modifier/fence capability evidence'" \
	'/usr/bin/tdvp-kms-capability-observer --device /dev/dri/card0' \
	'/proc/$pid/wchan' \
	'/proc/$pid/stack' \
	"'^(WLR_|TDVP_)'" \
	's/^XDG_RUNTIME_DIR=//p' \
	'Labwc XDG_RUNTIME_DIR=' \
	'discovered from Labwc' \
	'fallback /run Labwc session logs' \
	"section 'Live graphics/test process evidence'" \
	'report_graphics_process()' \
	'/proc/[0-9]*' \
	'/dev/dri/*|/dev/vg_lite|/dev/dma_heap/*' \
	'anon_inode:\[dmabuf\]*' \
	'graphics_or_test_processes=' \
	'/run/user/*/tdvp-labwc.log' \
	'journalctl -b --no-pager -o short-monotonic' \
	'-- kernel: VGLite/DRM recovery' \
	'/proc/interrupts'; do
	grep -Fq -- "${required}" "${AUDIT}" ||
		fail "missing VGLite recovery evidence: ${required}"
done

# This is a forensic entrypoint, not a recovery controller. Keep accidental
# operational actions out of it so collecting a failure report never erases
# the scene, restarts the compositor, or changes an already-faulted GPU state.
if grep -Eq 'systemctl[[:space:]]+(start|stop|restart|reboot)' "${AUDIT}"; then
	fail "audit must not control systemd units"
fi
if grep -Eq '(^|[[:space:];])(reboot|shutdown|poweroff|halt)([[:space:];]|$)' "${AUDIT}"; then
	fail "audit must not reboot or power off the device"
fi
if grep -Eq '(^|[[:space:];])(kill|pkill|killall)([[:space:];]|$)' "${AUDIT}"; then
	fail "audit must not signal processes"
fi
if grep -Eq '(^|[[:space:];])(echo|printf)[^#]*>[[:space:]]*/(sys|proc|dev)/' "${AUDIT}"; then
	fail "audit must not write kernel, procfs, or device state"
fi

printf '%s\n' 'test-tdvp-hardware-audit-vglite: PASS'

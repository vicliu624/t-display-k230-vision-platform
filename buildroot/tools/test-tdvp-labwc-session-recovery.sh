#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
project_root=$(CDPATH= cd -- "${script_dir}/../.." && pwd)
session_source="${project_root}/user-space/tdvp-labwc-desktop/src/tdvp-labwc-session"
environment_source="${project_root}/user-space/tdvp-labwc-desktop/src/environment"
labwc_recovery_patch="${project_root}/buildroot/k230-sdk-overlay/package/labwc/0004-tdvp-vglite-render-failure-recovery.patch"
fixture_dir="${script_dir}/fixtures"
bwrap_bin="${BWRAP:-bwrap}"

fail() {
	printf '%s\n' "test-tdvp-labwc-session-recovery: FAIL: $*" >&2
	exit 1
}

# A VGLite wait timeout first returns through wlroots as a failed scene/output
# commit.  The session wrapper can only select Pixman if the Labwc patch turns
# repeated failures into a non-zero process exit.  Keep this cross-layer
# contract explicit even on hosts without bubblewrap for the dynamic half.
[ -f "${labwc_recovery_patch}" ] || fail "missing Labwc VGLite recovery patch"
grep -Fq '#define TDVP_VGLITE_RENDER_FAILURE_LIMIT 3U' "${labwc_recovery_patch}" ||
	fail "Labwc recovery limit is missing or changed"
grep -Fq 'TDVP_LABWC_VGLITE_FAILURE_RECOVERY' "${labwc_recovery_patch}" ||
	fail "Labwc recovery is no longer gated to the VGLite session"
grep -Fq 'if (lab_wlr_scene_output_commit(scene_output, pending)) {' "${labwc_recovery_patch}" ||
	fail "Labwc no longer observes the normal scene commit result"
grep -Fq 'output->consecutive_render_failures = 0;' "${labwc_recovery_patch}" ||
	fail "a successful render no longer resets the failure counter"
grep -Fq 'output_handle_render_failure(output);' "${labwc_recovery_patch}" ||
	fail "a failed render no longer enters the recovery counter"
grep -Fq 'server->exit_status = EXIT_FAILURE;' "${labwc_recovery_patch}" ||
	fail "failure threshold no longer makes Labwc exit non-zero"
grep -Fq 'wl_display_terminate(server->wl_display);' "${labwc_recovery_patch}" ||
	fail "failure threshold no longer terminates Labwc's event loop"
grep -Fq 'return server.exit_status;' "${labwc_recovery_patch}" ||
	fail "Labwc no longer returns the recovery status to its session wrapper"
printf '%s\n' 'test-tdvp-labwc-session-recovery: PASS static-Labwc-failure-contract'

if ! command -v "${bwrap_bin}" >/dev/null 2>&1; then
	printf '%s\n' 'test-tdvp-labwc-session-recovery: SKIP bwrap is unavailable'
	exit 77
fi

test_root=$(mktemp -d "${TMPDIR:-/tmp}/tdvp-labwc-session-test.XXXXXX")
cleanup() {
	rm -rf "${test_root}"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "${test_root}/etc/tdvp/labwc" \
	"${test_root}/usr/local/bin" \
	"${test_root}/usr/bin" \
	"${test_root}/home/tdvp/runtime" \
	"${test_root}/home/tdvp/state"

install -m 0644 "${environment_source}" "${test_root}/etc/tdvp/labwc/environment"
install -m 0755 "${session_source}" "${test_root}/usr/local/bin/tdvp-labwc-session"
install -m 0755 "${fixture_dir}/tdvp-labwc-session-profile-helper" \
	"${test_root}/usr/local/bin/tdvp-renderer-profile"
install -m 0755 "${fixture_dir}/tdvp-labwc-session-fake-command" \
	"${test_root}/usr/bin/setsid"
ln -s setsid "${test_root}/usr/bin/dbus-run-session"
ln -s setsid "${test_root}/usr/bin/labwc"
mkdir -p "${test_root}/home/tdvp/state/tdvp-labwc"
: > "${test_root}/home/tdvp/state/tdvp-labwc/vglite-diagnostics-next-session"

run_session() {
	"${bwrap_bin}" --unshare-user --unshare-pid --new-session --die-with-parent \
		--bind / / \
		--dev /dev \
		--tmpfs /etc \
		--dir /etc/tdvp \
		--dir /etc/tdvp/labwc \
		--bind "${test_root}/etc/tdvp/labwc/environment" /etc/tdvp/labwc/environment \
		--tmpfs /usr/local \
		--dir /usr/local/bin \
		--bind "${test_root}/usr/local/bin/tdvp-labwc-session" /usr/local/bin/tdvp-labwc-session \
		--bind "${test_root}/usr/local/bin/tdvp-renderer-profile" /usr/local/bin/tdvp-renderer-profile \
		--tmpfs /usr/bin \
		--ro-bind /usr/bin/sh /usr/bin/sh \
		--ro-bind /usr/bin/basename /usr/bin/basename \
		--ro-bind /usr/bin/chmod /usr/bin/chmod \
		--ro-bind /usr/bin/mkdir /usr/bin/mkdir \
		--ro-bind /usr/bin/mv /usr/bin/mv \
		--ro-bind /usr/bin/rm /usr/bin/rm \
		--ro-bind /usr/bin/rmdir /usr/bin/rmdir \
		--bind "${test_root}/usr/bin/setsid" /usr/bin/setsid \
		--bind "${test_root}/usr/bin/dbus-run-session" /usr/bin/dbus-run-session \
		--bind "${test_root}/usr/bin/labwc" /usr/bin/labwc \
		--tmpfs /home \
		--dir /home/tdvp \
		--bind "${test_root}/home/tdvp" /home/tdvp \
		--setenv HOME /home/tdvp \
		--setenv XDG_RUNTIME_DIR /home/tdvp/runtime \
		--setenv XDG_STATE_HOME /home/tdvp/state \
		--setenv TDVP_TEST_LOG /home/tdvp/state/invocations.log \
		/usr/local/bin/tdvp-labwc-session
}

run_session

test -f "${test_root}/home/tdvp/state/tdvp-labwc/vglite.failed"
test ! -e "${test_root}/home/tdvp/state/tdvp-labwc/vglite-diagnostics-next-session"
test "$(wc -l < "${test_root}/home/tdvp/state/invocations.log")" -eq 2
grep -Fx 'renderer=vglite direct_scanout=1 failure_recovery=1 diagnostics=1 force_pixman=unset' \
	"${test_root}/home/tdvp/state/invocations.log"
grep -Fx 'renderer=pixman direct_scanout=unset failure_recovery=unset diagnostics=unset force_pixman=unset' \
	"${test_root}/home/tdvp/state/invocations.log"
grep -F 'VGLite circuit breaker recorded' \
	"${test_root}/home/tdvp/runtime/tdvp-labwc.log.previous"
grep -F 'restarting authenticated desktop with Pixman' \
	"${test_root}/home/tdvp/runtime/tdvp-labwc.log.previous"
grep -F 'renderer_profile=pixman auto-recovery=forced-pixman' \
	"${test_root}/home/tdvp/runtime/tdvp-labwc.log"
grep -F 'direct_scanout=restored-for-pixman-recovery' \
	"${test_root}/home/tdvp/runtime/tdvp-labwc.log"
grep -F 'vglite_failure_recovery=not-applicable' \
	"${test_root}/home/tdvp/runtime/tdvp-labwc.log"
grep -F 'vglite_diagnostics=enabled-once' \
	"${test_root}/home/tdvp/runtime/tdvp-labwc.log.previous"
grep -F 'vglite_diagnostics=off' \
	"${test_root}/home/tdvp/runtime/tdvp-labwc.log"

# A root operator may arm a capture while the breaker still forces Pixman.
# That marker must remain pending: consuming it here would let a later report
# mistake a non-VGLite Pixman measurement for the requested VGLite session.
: > "${test_root}/home/tdvp/state/tdvp-labwc/vglite-diagnostics-next-session"
run_session
test -f "${test_root}/home/tdvp/state/tdvp-labwc/vglite-diagnostics-next-session"
test "$(wc -l < "${test_root}/home/tdvp/state/invocations.log")" -eq 3
tail -n 1 "${test_root}/home/tdvp/state/invocations.log" | \
	grep -Fx 'renderer=pixman direct_scanout=unset failure_recovery=unset diagnostics=unset force_pixman=unset'

printf '%s\n' 'test-tdvp-labwc-session-recovery: PASS'

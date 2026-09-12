#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
project_root=$(CDPATH= cd -- "${script_dir}/../.." && pwd)
desktop_source="${project_root}/user-space/tdvp-labwc-desktop/src"
labwc_recovery_patch="${project_root}/buildroot/k230-sdk-overlay/package/labwc/0004-tdvp-vglite-render-failure-recovery.patch"

fail() {
	printf '%s\n' "test-tdvp-labwc-session-recovery: FAIL: $*" >&2
	exit 1
}

# Preserve the delivered GPU failure detector. Only the session's response
# changes: clean up and fail closed, never launch a software replacement.
grep -Fq '#define TDVP_VGLITE_RENDER_FAILURE_LIMIT 3U' "${labwc_recovery_patch}"
grep -Fq 'TDVP_LABWC_VGLITE_FAILURE_RECOVERY' "${labwc_recovery_patch}"
grep -Fq 'output->consecutive_render_failures = 0;' "${labwc_recovery_patch}"
grep -Fq 'output_handle_render_failure(output);' "${labwc_recovery_patch}"
grep -Fq 'server->exit_status = EXIT_FAILURE;' "${labwc_recovery_patch}"
grep -Fq 'wl_display_terminate(server->wl_display);' "${labwc_recovery_patch}"
grep -Fq 'return server.exit_status;' "${labwc_recovery_patch}"
if grep -Eiq 'pixman|exec /usr/local/bin/tdvp-labwc-session' "${desktop_source}/tdvp-labwc-session"; then
	fail 'desktop contains a software fallback or self-restart'
fi
[ "$(id -u)" = 0 ] || { echo 'test-tdvp-labwc-session-recovery: SKIP needs root for isolated chroot'; exit 77; }

test_root=$(mktemp -d "${TMPDIR:-/tmp}/tdvp-labwc-session-test.XXXXXX")
cleanup() {
	rm -rf "${test_root}"
}
trap cleanup EXIT HUP INT TERM

# Execute the real absolute-path launcher/profile in an isolated filesystem.
# Real setsid gives the fake compositor its own PGID: production cleanup
# cannot signal the test runner or another host session. No GPU/DRM is used.
for command in sh env chmod mkdir mv rm rmdir grep dirname date id setsid; do
	command_path=$(command -v "${command}")
	install -D -m 0755 "${command_path}" "${test_root}/usr/bin/${command}"
	ldd "${command_path}" | awk '/=> \// {print $3} /^[[:space:]]*\// {print $1}' |
		while IFS= read -r library; do
			install -D -m 0755 "${library}" "${test_root}${library}"
		done
done
ln -s usr/bin "${test_root}/bin"
mkdir -p "${test_root}/dev" "${test_root}/etc/tdvp/labwc" \
	"${test_root}/usr/local/bin" "${test_root}/home/tdvp/runtime" \
	"${test_root}/home/tdvp/state/tdvp-labwc"
mknod -m 0666 "${test_root}/dev/null" c 1 3
for file in environment renderer-profile vglite-enabled; do
	install -m 0644 "${desktop_source}/${file}" "${test_root}/etc/tdvp/labwc/${file}"
done
for file in tdvp-labwc-session tdvp-renderer-profile; do
	install -m 0755 "${desktop_source}/${file}" "${test_root}/usr/local/bin/${file}"
done
install -m 0755 "${script_dir}/fixtures/tdvp-labwc-session-fake-compositor" "${test_root}/usr/bin/labwc"
install -m 0755 "${script_dir}/fixtures/tdvp-labwc-session-fake-dbus" "${test_root}/usr/bin/dbus-run-session"
state="${test_root}/home/tdvp/state/tdvp-labwc"
calls="${test_root}/home/tdvp/state/invocations.log"
log="${test_root}/home/tdvp/runtime/tdvp-labwc.log"
: > "${state}/vglite-diagnostics-next-session"

run_session() {
	chroot "${test_root}" /usr/bin/env -i HOME=/home/tdvp \
		XDG_RUNTIME_DIR=/home/tdvp/runtime XDG_STATE_HOME=/home/tdvp/state \
		TDVP_TEST_LOG=/home/tdvp/state/invocations.log TDVP_TEST_EXIT="$1" \
		/usr/local/bin/tdvp-labwc-session
}

if run_session 1; then fail 'GPU failure returned success'; fi
test -f "${state}/vglite.failed"
test ! -e "${state}/vglite-diagnostics-next-session"
test "$(wc -l < "${calls}")" -eq 1
grep -Fx 'renderer=vglite direct_scanout=1 failure_recovery=1 diagnostics=1' "${calls}"
grep -F 'VGLite circuit breaker recorded' "${log}"

# A blocked next login must neither launch a compositor nor consume a newly
# armed diagnostic capture. The error remains visible to greetd.
: > "${state}/vglite-diagnostics-next-session"
if run_session 0; then fail 'tripped GPU failure allowed another desktop'; fi
test -f "${state}/vglite-diagnostics-next-session"
test "$(wc -l < "${calls}")" -eq 1
rm "${state}/vglite.failed"
run_session 0
test ! -e "${state}/vglite.failed"
test "$(wc -l < "${calls}")" -eq 2
mv "${test_root}/etc/tdvp/labwc/vglite-enabled" "${test_root}/saved-enable"
if run_session 0; then fail 'missing enable policy launched desktop'; fi
mv "${test_root}/saved-enable" "${test_root}/etc/tdvp/labwc/vglite-enabled"
chmod 0644 "${test_root}/usr/local/bin/tdvp-renderer-profile"
if run_session 0; then fail 'missing executable helper launched desktop'; fi
test "$(wc -l < "${calls}")" -eq 2
printf '%s\n' 'test-tdvp-labwc-session-recovery: PASS real launcher/profile refuse software fallback; failed and normal exits verified'

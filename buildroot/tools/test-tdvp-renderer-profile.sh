#!/bin/sh
set -eu

project_dir="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
profile_tool="${project_dir}/user-space/tdvp-labwc-desktop/src/tdvp-renderer-profile"
temporary_root="$(mktemp -d "${TMPDIR:-/tmp}/tdvp-renderer-profile.XXXXXX")"

cleanup() {
	rm -rf "${temporary_root}"
}
trap cleanup EXIT HUP INT TERM

profile_file="${temporary_root}/renderer-profile"
enabled_file="${temporary_root}/vglite-enabled"
failure_file="${temporary_root}/state/vglite.failed"
diagnostics_file="${temporary_root}/state/vglite-diagnostics-next-session"
diagnostics_owner="$(id -u):$(id -g)"

run_tool() {
	env \
		TDVP_RENDERER_PROFILE_FILE="${profile_file}" \
		TDVP_VGLITE_ENABLED_FILE="${enabled_file}" \
		TDVP_VGLITE_FAILURE_FILE="${failure_file}" \
		TDVP_VGLITE_DIAGNOSTICS_FILE="${diagnostics_file}" \
		TDVP_VGLITE_DIAGNOSTICS_OWNER="${diagnostics_owner}" \
		sh "${profile_tool}" "$@"
}

expect_equal() {
	expected="$1"
	actual="$2"
	name="$3"
	if [ "${actual}" != "${expected}" ]; then
		printf 'test-tdvp-renderer-profile: FAIL %s: expected=%s actual=%s\n' \
			"${name}" "${expected}" "${actual}" >&2
		exit 1
	fi
	printf 'test-tdvp-renderer-profile: PASS %s\n' "${name}"
}

[ -f "${profile_tool}" ] || {
	printf 'test-tdvp-renderer-profile: missing source %s\n' "${profile_tool}" >&2
	exit 1
}

expect_blocked() {
	if output="$(run_tool resolve 2>"${temporary_root}/error")"; then
		printf 'test-tdvp-renderer-profile: FAIL %s accepted blocked policy\n' "$1" >&2
		exit 1
	fi
	expect_equal '' "${output}" "$1-no-renderer-output"
	grep -Fq 'desktop blocked' "${temporary_root}/error"
}

expect_blocked missing-policy
expect_equal off "$(run_tool diagnostics-status)" diagnostics-default-off

printf '%s\n' 'TDVP_RENDERER_PROFILE=vglite' > "${profile_file}"
expect_blocked vglite-without-approval

printf '%s\n' 'tdvp_vglite_policy_version=1' > "${enabled_file}"
expect_equal vglite "$(run_tool resolve)" approved-vglite

run_tool trip 139
[ -f "${failure_file}" ] || {
	printf '%s\n' 'test-tdvp-renderer-profile: FAIL trip did not create marker' >&2
	exit 1
}
expect_blocked tripped-no-fallback
status="$(run_tool status)"
case "${status}" in
	*'configured=vglite effective=blocked vglite_enabled=yes breaker=tripped'*)
		printf '%s\n' 'test-tdvp-renderer-profile: PASS status-records-breaker'
		;;
	*)
		printf 'test-tdvp-renderer-profile: FAIL unexpected status: %s\n' "${status}" >&2
		exit 1
		;;
esac

rm -f "${failure_file}"
expect_equal vglite "$(run_tool resolve)" clear-breaker-restores-approved-profile

printf '%s\n' 'TDVP_RENDERER_PROFILE=unexpected' > "${profile_file}"
expect_blocked invalid-profile
printf '%s\n' 'TDVP_RENDERER_PROFILE=pixman' > "${profile_file}"
expect_blocked retired-software-profile
printf '%s\n' 'TDVP_RENDERER_PROFILE=vglite' 'TDVP_RENDERER_PROFILE=pixman' > "${profile_file}"
expect_blocked mixed-profile
printf '%s\n' 'TDVP_RENDERER_PROFILE=vglite' 'TDVP_RENDERER_PROFILE=vglite' > "${profile_file}"
expect_blocked duplicate-profile

# Bind the default to the actual files installed by the product recipe, not
# just a synthetic VGLite assignment. The image-source test checks that the
# recipe really delivers these files and the image guard requires them.
sed 's/\r$//' "${project_dir}/user-space/tdvp-labwc-desktop/src/renderer-profile" > "${profile_file}"
sed 's/\r$//' "${project_dir}/user-space/tdvp-labwc-desktop/src/vglite-enabled" > "${enabled_file}"
expect_equal vglite "$(run_tool resolve)" real-product-default-vglite
run_tool trip 1
expect_blocked real-product-failure
rm -f "${failure_file}" "${enabled_file}"
expect_blocked real-product-missing-enable-policy
: > "${enabled_file}"
expect_blocked empty-enable-policy

if [ "$(id -u)" = 0 ]; then
	rm -f "${enabled_file}"
	if run_tool select vglite >/dev/null 2>&1; then
		printf '%s\n' 'test-tdvp-renderer-profile: FAIL unapproved VGLite selection succeeded' >&2
		exit 1
	fi
	printf '%s\n' 'test-tdvp-renderer-profile: PASS unapproved-vglite-selection-refused'
	printf '%s\n' 'tdvp_vglite_policy_version=1' > "${enabled_file}"
	run_tool select vglite
	expect_equal vglite "$(run_tool resolve)" root-select-vglite
	run_tool clear-vglite-failure
	if run_tool select pixman >/dev/null 2>&1; then
		printf '%s\n' 'test-tdvp-renderer-profile: FAIL root selected retired software renderer' >&2
		exit 1
	fi
	expect_equal vglite "$(run_tool resolve)" root-cannot-select-software-renderer
	run_tool diagnostics-next-vglite-session
	[ -f "${diagnostics_file}" ] || {
		printf '%s\n' 'test-tdvp-renderer-profile: FAIL diagnostic marker was not created' >&2
		exit 1
	}
	expect_equal next-vglite-session "$(run_tool diagnostics-status)" root-arms-one-shot-diagnostics
	run_tool clear-diagnostics-next-vglite-session
	expect_equal off "$(run_tool diagnostics-status)" root-clears-one-shot-diagnostics
else
	printf '%s\n' 'test-tdvp-renderer-profile: SKIP root-management-actions (not root)'
fi

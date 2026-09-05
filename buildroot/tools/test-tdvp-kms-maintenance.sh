#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
SOURCE_DIR="$PROJECT_DIR/buildroot/k230-sdk-overlay/package/tdvp-display-smoke/src"
MAINTENANCE="$SOURCE_DIR/tdvp-kms-maintenance"
RECOVER="$SOURCE_DIR/tdvp-kms-maintenance-recover"
ACCEPTANCE="$SOURCE_DIR/tdvp-kms-acceptance"

for file in "$MAINTENANCE" "$RECOVER" "$ACCEPTANCE"; do
	[ -f "$file" ] || {
		printf 'test-tdvp-kms-maintenance: missing source file: %s\n' "$file" >&2
		exit 1
	}
done

test_root="$(mktemp -d)"
trap 'rm -rf "$test_root"' EXIT HUP INT TERM
fake_bin="$test_root/bin"
mkdir -p "$fake_bin"

write_fake() {
	path="$1"
	shift
	printf '%s\n' "$@" >"$path"
	chmod 0755 "$path"
}

write_fake "$fake_bin/systemctl" \
	'#!/bin/sh' \
	'set -eu' \
	'state_file="${TDVP_TEST_STATE_FILE:?}"' \
	'actions_file="${TDVP_TEST_ACTIONS_FILE:?}"' \
	'case "$1" in' \
	'  show)' \
	'    cat "$state_file"' \
	'    ;;' \
	'  stop)' \
	'    echo stop >>"$actions_file"' \
	'    if [ "${TDVP_TEST_STOP_BEHAVIOR:-inactive}" = inactive ]; then' \
	'      echo inactive >"$state_file"' \
	'    fi' \
	'    ;;' \
	'  start)' \
	'    echo start >>"$actions_file"' \
	'    echo active >"$state_file"' \
	'    ;;' \
	'  *)' \
	'    printf "unexpected fake systemctl command: %s\\n" "$1" >&2' \
	'    exit 64' \
	'    ;;' \
	'esac'
write_fake "$fake_bin/pgrep" \
	'#!/bin/sh' \
	'case "${TDVP_TEST_PGREP_MODE:-none}:${2:-}" in' \
	'  labwc:labwc|gtkgreet:gtkgreet) exit 0 ;;' \
	'  *) exit 1 ;;' \
	'esac'
write_fake "$fake_bin/logger" '#!/bin/sh' 'exit 0'
write_fake "$fake_bin/id" '#!/bin/sh' 'echo 0'
write_fake "$fake_bin/acceptance" \
	'#!/bin/sh' \
	'set -eu' \
	'echo acceptance >>"${TDVP_TEST_ACTIONS_FILE:?}"' \
	'exit "${TDVP_TEST_ACCEPTANCE_RC:-0}"'
write_fake "$fake_bin/display-smoke" \
	'#!/bin/sh' \
	'set -eu' \
	'echo display-smoke >>"${TDVP_TEST_ACTIONS_FILE:?}"' \
	'if [ "${TDVP_TEST_DISPLAY_SMOKE_NO_DETACH_MARKER:-0}" != 1 ]; then' \
	'  echo "tdvp-display-smoke: PASS test plane detached and guard vblank observed before buffer release"' \
	'fi' \
	'exit "${TDVP_TEST_DISPLAY_SMOKE_RC:-0}"'
write_fake "$fake_bin/vblank-observer" \
	'#!/bin/sh' \
	'set -eu' \
	'echo vblank-observer >>"${TDVP_TEST_ACTIONS_FILE:?}"' \
	'exit "${TDVP_TEST_VBLANK_OBSERVER_RC:-0}"'

assert_file_line() {
	file="$1"
	line="$2"
	grep -Fqx "$line" "$file" || {
		printf 'test-tdvp-kms-maintenance: expected %s in %s\n' "$line" "$file" >&2
		exit 1
	}
}

run_maintenance_case() {
	name="$1"
	initial_state="$2"
	stop_behavior="$3"
	pgrep_mode="$4"
	expected_status="$5"
	expected_rc="$6"
	case_root="$test_root/$name"
	state_dir="$case_root/state"
	state_file="$case_root/greetd-state"
	actions_file="$case_root/actions"
	mkdir -p "$state_dir"
	printf '%s\n' "$initial_state" >"$state_file"
	: >"$actions_file"

	set +e
	PATH="$fake_bin:$PATH" \
	TDVP_TEST_STATE_FILE="$state_file" \
	TDVP_TEST_ACTIONS_FILE="$actions_file" \
	TDVP_TEST_STOP_BEHAVIOR="$stop_behavior" \
	TDVP_TEST_PGREP_MODE="$pgrep_mode" \
	TDVP_KMS_STATE_DIR="$state_dir" \
	TDVP_KMS_ACCEPTANCE_PROGRAM="$fake_bin/acceptance" \
	TDVP_VBLANK_OBSERVER_PROGRAM="$fake_bin/vblank-observer" \
	TDVP_KMS_STOP_TIMEOUT_SECONDS=1 \
	TDVP_KMS_MAINTENANCE_SERVICE=1 \
	/bin/sh "$MAINTENANCE"
	rc=$?
	set -e
	[ "$rc" -eq "$expected_rc" ] || {
		printf 'test-tdvp-kms-maintenance: %s returned %s, expected %s\n' \
			"$name" "$rc" "$expected_rc" >&2
		exit 1
	}
	assert_file_line "$state_dir/kms-maintenance.status" "$expected_status"
	assert_file_line "$actions_file" stop
	if [ "$expected_status" = pass ]; then
		assert_file_line "$actions_file" acceptance
	fi
	test -e "$state_dir/kms-restore-greetd"

	PATH="$fake_bin:$PATH" \
	TDVP_TEST_STATE_FILE="$state_file" \
	TDVP_TEST_ACTIONS_FILE="$actions_file" \
	TDVP_KMS_STATE_DIR="$state_dir" \
	TDVP_KMS_MAINTENANCE_SERVICE=1 \
	/bin/sh "$RECOVER"
	assert_file_line "$actions_file" start
	assert_file_line "$state_file" active
	[ ! -e "$state_dir/kms-restore-greetd" ] || {
		printf 'test-tdvp-kms-maintenance: %s did not clear the restore marker\n' \
			"$name" >&2
		exit 1
	}
	printf 'test-tdvp-kms-maintenance: PASS %s\n' "$name"
}

run_maintenance_case active-pass active inactive none pass 0
run_maintenance_case deactivating-pass deactivating inactive none pass 0
run_maintenance_case stop-timeout active stuck none fail 1
run_maintenance_case lingering-gtkgreet active inactive gtkgreet fail 1

# All three helpers must treat help and malformed command lines as a pure CLI
# operation. In particular, help must never stop greetd or create a KMS run.
run_safe_cli_case() {
	name="$1"
	program="$2"
	argument="$3"
	expected_rc="$4"
	case_root="$test_root/cli-$name"
	state_dir="$case_root/state"
	state_file="$case_root/greetd-state"
	actions_file="$case_root/actions"
	mkdir -p "$state_dir"
	printf '%s\n' active >"$state_file"
	: >"$actions_file"

	set +e
	PATH="$fake_bin:$PATH" \
	TDVP_TEST_STATE_FILE="$state_file" \
	TDVP_TEST_ACTIONS_FILE="$actions_file" \
	TDVP_KMS_STATE_DIR="$state_dir" \
	/bin/sh "$program" "$argument" >"$case_root/stdout" 2>"$case_root/stderr"
	rc=$?
	set -e
	[ "$rc" -eq "$expected_rc" ] || {
		printf 'test-tdvp-kms-maintenance: CLI %s returned %s, expected %s\n' \
			"$name" "$rc" "$expected_rc" >&2
		exit 1
	}
	[ ! -s "$actions_file" ] || {
		printf 'test-tdvp-kms-maintenance: CLI %s performed a system action\n' \
			"$name" >&2
		exit 1
	}
	test ! -e "$state_dir/kms.status"
	test ! -e "$state_dir/kms-maintenance.status"
	printf 'test-tdvp-kms-maintenance: PASS cli-%s\n' "$name"
}

for helper in "$MAINTENANCE" "$ACCEPTANCE" "$RECOVER"; do
	base_name="$(basename "$helper")"
	run_safe_cli_case "${base_name}-help" "$helper" --help 0
	run_safe_cli_case "${base_name}-invalid" "$helper" --invalid-option 2
done

# A root shell must not be able to accidentally stop the desktop by executing
# the libexec helper directly. Only the systemd unit supplies this marker.
direct_maintenance_root="$test_root/direct-maintenance"
direct_maintenance_state="$direct_maintenance_root/state"
direct_maintenance_state_file="$direct_maintenance_root/greetd-state"
direct_maintenance_actions="$direct_maintenance_root/actions"
mkdir -p "$direct_maintenance_state"
printf '%s\n' active >"$direct_maintenance_state_file"
: >"$direct_maintenance_actions"
set +e
PATH="$fake_bin:$PATH" \
TDVP_TEST_STATE_FILE="$direct_maintenance_state_file" \
TDVP_TEST_ACTIONS_FILE="$direct_maintenance_actions" \
TDVP_KMS_STATE_DIR="$direct_maintenance_state" \
/bin/sh "$MAINTENANCE"
direct_maintenance_rc=$?
set -e
[ "$direct_maintenance_rc" -eq 1 ] || {
	printf 'test-tdvp-kms-maintenance: direct maintenance returned %s\n' \
		"$direct_maintenance_rc" >&2
	exit 1
}
[ ! -s "$direct_maintenance_actions" ]
test ! -e "$direct_maintenance_state/kms-maintenance.status"
printf '%s\n' 'test-tdvp-kms-maintenance: PASS direct-maintenance-guard'

guard_root="$test_root/direct-guard"
guard_state="$guard_root/state"
guard_state_file="$guard_root/greetd-state"
guard_actions="$guard_root/actions"
mkdir -p "$guard_state"
printf '%s\n' active >"$guard_state_file"
: >"$guard_actions"
set +e
PATH="$fake_bin:$PATH" \
TDVP_TEST_STATE_FILE="$guard_state_file" \
TDVP_TEST_ACTIONS_FILE="$guard_actions" \
TDVP_KMS_STATE_DIR="$guard_state" \
/bin/sh "$ACCEPTANCE"
guard_rc=$?
set -e
[ "$guard_rc" -eq 1 ] || {
	printf 'test-tdvp-kms-maintenance: direct acceptance guard returned %s\n' \
		"$guard_rc" >&2
	exit 1
}
assert_file_line "$guard_state/kms.status" blocked
grep -F 'refusing direct KMS acceptance outside tdvp-kms-maintenance' \
	"$guard_state/kms.log" >/dev/null
printf '%s\n' 'test-tdvp-kms-maintenance: PASS direct-acceptance-guard'

wrapped_guard_root="$test_root/wrapped-desktop-guard"
wrapped_guard_state="$wrapped_guard_root/state"
wrapped_guard_state_file="$wrapped_guard_root/greetd-state"
wrapped_guard_actions="$wrapped_guard_root/actions"
mkdir -p "$wrapped_guard_state"
printf '%s\n' active >"$wrapped_guard_state_file"
: >"$wrapped_guard_actions"
set +e
PATH="$fake_bin:$PATH" \
TDVP_TEST_STATE_FILE="$wrapped_guard_state_file" \
TDVP_TEST_ACTIONS_FILE="$wrapped_guard_actions" \
TDVP_KMS_STATE_DIR="$wrapped_guard_state" \
TDVP_KMS_MAINTENANCE_ACTIVE=1 \
/bin/sh "$ACCEPTANCE"
wrapped_guard_rc=$?
set -e
[ "$wrapped_guard_rc" -eq 1 ] || {
	printf 'test-tdvp-kms-maintenance: wrapped desktop guard returned %s\n' \
		"$wrapped_guard_rc" >&2
	exit 1
}
assert_file_line "$wrapped_guard_state/kms.status" blocked
grep -F 'refusing direct KMS acceptance while greetd is not fully stopped' \
	"$wrapped_guard_state/kms.log" >/dev/null
printf '%s\n' 'test-tdvp-kms-maintenance: PASS wrapped-desktop-guard'

pass_root="$test_root/direct-pass"
pass_state="$pass_root/state"
pass_state_file="$pass_root/greetd-state"
pass_actions="$pass_root/actions"
mkdir -p "$pass_state"
printf '%s\n' inactive >"$pass_state_file"
: >"$pass_actions"
PATH="$fake_bin:$PATH" \
TDVP_TEST_STATE_FILE="$pass_state_file" \
TDVP_TEST_ACTIONS_FILE="$pass_actions" \
TDVP_TEST_PGREP_MODE=none \
	TDVP_KMS_STATE_DIR="$pass_state" \
	TDVP_DISPLAY_SMOKE_PROGRAM="$fake_bin/display-smoke" \
	TDVP_VBLANK_OBSERVER_PROGRAM="$fake_bin/vblank-observer" \
	TDVP_KMS_MAINTENANCE_ACTIVE=1 \
	/bin/sh "$ACCEPTANCE"
assert_file_line "$pass_state/kms.status" pass
assert_file_line "$pass_actions" display-smoke
assert_file_line "$pass_actions" vblank-observer
printf '%s\n' 'test-tdvp-kms-maintenance: PASS direct-candidate-program'

detach_gate_root="$test_root/direct-detach-gate"
detach_gate_state="$detach_gate_root/state"
detach_gate_state_file="$detach_gate_root/greetd-state"
detach_gate_actions="$detach_gate_root/actions"
mkdir -p "$detach_gate_state"
printf '%s\n' inactive >"$detach_gate_state_file"
: >"$detach_gate_actions"
set +e
PATH="$fake_bin:$PATH" \
TDVP_TEST_STATE_FILE="$detach_gate_state_file" \
TDVP_TEST_ACTIONS_FILE="$detach_gate_actions" \
TDVP_TEST_PGREP_MODE=none \
TDVP_TEST_DISPLAY_SMOKE_NO_DETACH_MARKER=1 \
TDVP_KMS_STATE_DIR="$detach_gate_state" \
TDVP_DISPLAY_SMOKE_PROGRAM="$fake_bin/display-smoke" \
TDVP_VBLANK_OBSERVER_PROGRAM="$fake_bin/vblank-observer" \
TDVP_KMS_MAINTENANCE_ACTIVE=1 \
/bin/sh "$ACCEPTANCE"
detach_gate_rc=$?
set -e
[ "$detach_gate_rc" -eq 1 ] || {
	printf 'test-tdvp-kms-maintenance: missing detach marker returned %s\n' \
		"$detach_gate_rc" >&2
	exit 1
}
assert_file_line "$detach_gate_state/kms.status" fail
assert_file_line "$detach_gate_actions" display-smoke
if grep -Fqx vblank-observer "$detach_gate_actions"; then
	printf '%s\n' 'test-tdvp-kms-maintenance: vblank ran after a missing detach marker' >&2
	exit 1
fi
printf '%s\n' 'test-tdvp-kms-maintenance: PASS direct-detach-gate'

vblank_fail_root="$test_root/direct-vblank-fail"
vblank_fail_state="$vblank_fail_root/state"
vblank_fail_state_file="$vblank_fail_root/greetd-state"
vblank_fail_actions="$vblank_fail_root/actions"
mkdir -p "$vblank_fail_state"
printf '%s\n' inactive >"$vblank_fail_state_file"
: >"$vblank_fail_actions"
set +e
PATH="$fake_bin:$PATH" \
TDVP_TEST_STATE_FILE="$vblank_fail_state_file" \
TDVP_TEST_ACTIONS_FILE="$vblank_fail_actions" \
TDVP_TEST_PGREP_MODE=none \
TDVP_KMS_STATE_DIR="$vblank_fail_state" \
TDVP_DISPLAY_SMOKE_PROGRAM="$fake_bin/display-smoke" \
TDVP_VBLANK_OBSERVER_PROGRAM="$fake_bin/vblank-observer" \
TDVP_TEST_VBLANK_OBSERVER_RC=1 \
TDVP_KMS_MAINTENANCE_ACTIVE=1 \
/bin/sh "$ACCEPTANCE"
vblank_fail_rc=$?
set -e
[ "$vblank_fail_rc" -eq 1 ] || {
	printf 'test-tdvp-kms-maintenance: vblank failure returned %s\n' "$vblank_fail_rc" >&2
	exit 1
}
assert_file_line "$vblank_fail_state/kms.status" fail
assert_file_line "$vblank_fail_actions" display-smoke
assert_file_line "$vblank_fail_actions" vblank-observer
printf '%s\n' 'test-tdvp-kms-maintenance: PASS direct-vblank-failure'

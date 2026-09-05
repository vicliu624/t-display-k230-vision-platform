#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GATE="${PROJECT_DIR}/k230-sdk-overlay/package/tdvp-vglite-acceptance/src/tdvp-vglite-session-gate"
WATCHDOG_OBSERVER="${PROJECT_DIR}/k230-sdk-overlay/package/tdvp-vglite-acceptance/src/tdvp-vglite-watchdog-observer"
PACKAGE_MK="${PROJECT_DIR}/k230-sdk-overlay/package/tdvp-vglite-acceptance/tdvp-vglite-acceptance.mk"

fail() {
	printf '%s\n' "test-tdvp-vglite-session-gate: FAIL: $*" >&2
	exit 1
}

[ -f "${GATE}" ] || fail "missing session-gate wrapper"
[ -f "${WATCHDOG_OBSERVER}" ] || fail "missing VGLite watchdog observer"
bash -n "${GATE}"
bash -n "${WATCHDOG_OBSERVER}"
grep -Fqx 'PROFILE_TOOL="${TDVP_RENDERER_PROFILE_TOOL:-/usr/local/bin/tdvp-renderer-profile}"' "${GATE}"
grep -Fqx 'BENCH_SESSION="${TDVP_WAYLAND_SHM_BENCH_SESSION:-/usr/bin/tdvp-wayland-shm-bench-session}"' "${GATE}"
grep -Fqx 'WATCHDOG_OBSERVER="${TDVP_VGLITE_WATCHDOG_OBSERVER:-/usr/bin/tdvp-vglite-watchdog-observer}"' "${GATE}"
grep -Fqx 'DIAGNOSTICS_REPORT="${TDVP_VGLITE_DIAGNOSTICS_REPORT:-/usr/bin/tdvp-vglite-diagnostics-report}"' "${GATE}"
grep -Fq -- '--expect-vglite-compositor is required' "${GATE}"
grep -Fq 'run as the graphical login user, not root' "${GATE}"
grep -Fq "configured=vglite effective=vglite vglite_enabled=yes breaker=clear" "${GATE}"
grep -Fq 'WLR_RENDERER)" = vglite' "${GATE}"
grep -Fq 'TDVP_LABWC_VGLITE_FAILURE_RECOVERY)" = 1' "${GATE}"
grep -Fq 'WLR_SCENE_DISABLE_DIRECT_SCANOUT)" = 1' "${GATE}"
grep -Fq '"$WATCHDOG_OBSERVER" --expect-default-ms 5000' "${GATE}"
grep -Fq '"$BENCH_SESSION" --user "$session_user" --pid "$labwc_pid" --' "${GATE}"
grep -Fqx 'churn_iterations=0' "${GATE}"
grep -Fq -- '--churn-iterations 0' "${GATE}"
grep -Fq 'K230 VGLite is a single-context driver: do not run client churn while Labwc owns /dev/vg_lite' "${GATE}"
grep -Fq 'vglite_client_churn=not-run-single-context' "${GATE}"
grep -Fqx 'repeat=1' "${GATE}"
grep -Fq -- '--repeat N' "${GATE}"
grep -Fq 'require_positive_integer repeat "$repeat"' "${GATE}"
grep -Fq 'workload_round=$round/$repeat' "${GATE}"
grep -Fq 'while [ "$round" -le "$repeat" ]; do' "${GATE}"
grep -Fq 'read_labwc_accounting()' "${GATE}"
grep -Fq 'Labwc PID changed during Gate 1' "${GATE}"
grep -Fq 'labwc_cpu_user_ticks_delta=' "${GATE}"
grep -Fq 'labwc_cpu_system_ticks_delta=' "${GATE}"
grep -Fq 'labwc_rss_kb_before=' "${GATE}"
grep -Fq 'tdvp-vglite-session-gate: PASS' "${GATE}"
grep -Fq -- '--diagnostics-log PATH' "${GATE}"
grep -Fq '"$DIAGNOSTICS_REPORT" --log "$diagnostics_log" --skip-lines "$diagnostics_start_lines"' "${GATE}"
grep -Fq -- '--require-finish 1 --require-page-flip-transition 1' "${GATE}"
grep -Fqx 'shm_format=xr24' "${GATE}"
grep -Fq -- '--format xr24|ar24' "${GATE}"
grep -Fq 'require_shm_format "$shm_format"' "${GATE}"
grep -Fq -- '--format "$shm_format"' "${GATE}"

if grep -Eiq '(^|[^[:alnum:]_])(systemctl|service|reboot|shutdown|kill|pkill|modetest|drmMode|drmSetMaster|sudo)([^[:alnum:]_]|$)' "${GATE}"; then
	fail "session gate must not change a compositor, KMS state, or renderer policy"
fi
if grep -Eq 'tdvp-renderer-profile.*(select|trip|clear-vglite-failure)' "${GATE}"; then
	fail "session gate must not change the renderer-profile state"
fi

grep -Fq 'tdvp-vglite-session-gate' "${PACKAGE_MK}"
grep -Fq 'tdvp-vglite-watchdog-observer' "${PACKAGE_MK}"

# Exercise the repeat loop with a synthetic same-user Labwc process. The gate
# only receives fake helpers and a temporary proc tree: this proves that each
# selected workload is followed by a fresh process/profile check without
# starting a compositor, opening DRM, or touching a real user session.
TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/tdvp-vglite-gate.XXXXXX")"
cleanup() {
	rm -rf -- "${TEST_ROOT}"
}
trap cleanup EXIT

FAKE_BIN="${TEST_ROOT}/bin"
FAKE_PROC="${TEST_ROOT}/proc/4242"
FAKE_LOG="${TEST_ROOT}/calls.log"
FAKE_MODULE="${TEST_ROOT}/sys/module/vglite"
mkdir -p "${FAKE_BIN}" "${FAKE_PROC}" "${FAKE_MODULE}/parameters"

cat >"${FAKE_BIN}/id" <<'EOF'
#!/bin/sh
case "$1" in
-u) printf '%s\n' 1000 ;;
-un) printf '%s\n' tdvp ;;
*) exit 2 ;;
esac
EOF
cat >"${FAKE_BIN}/pgrep" <<'EOF'
#!/bin/sh
if [ "${FAKE_REPLACED_PID:-0}" = 1 ]; then
    state="${FAKE_PGREP_STATE:?}"
    if [ -e "$state" ]; then
        printf '%s\n' 4343
    else
        : >"$state"
        printf '%s\n' 4242
    fi
    exit 0
fi
printf '%s\n' 4242
EOF
cat >"${TEST_ROOT}/profile" <<'EOF'
#!/bin/sh
[ "$1" = status ] || exit 2
printf '%s\n' 'configured=vglite effective=vglite vglite_enabled=yes breaker=clear'
EOF
cat >"${TEST_ROOT}/bench" <<'EOF'
#!/bin/sh
printf 'bench %s\n' "$*" >>"${FAKE_LOG:?}"
if [ -n "${FAKE_DIAGNOSTICS_LOG:-}" ]; then
    printf '%s\n' 'TDVP_VGLITE_DIAG stage=texture_upload texture=0x1 damage_rects=1 copied_bytes=16 source_bytes=16 copy_calls=1 cache_flush_pending=1' >>"${FAKE_DIAGNOSTICS_LOG}"
    printf '%s\n' 'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x900 elapsed_ms=1.000 result=0 pass_ok=1 texture_clip_rects=1 texture_blit_attempts=1 texture_cache_flushes=1 solid_clip_rects=0 solid_blit_attempts=0 state_recovery_attempted=0 state_recovery_ok=1' >>"${FAKE_DIAGNOSTICS_LOG}"
	if [ "${FAKE_DIAGNOSTICS_READBACK_FAILURE:-0}" = 1 ]; then
		printf '%s\n' 'TDVP_VGLITE_DIAG stage=texture_readback_finish result=-5 renderer_quarantined=1' >>"${FAKE_DIAGNOSTICS_LOG}"
	fi
	printf '%s\n' 'TDVP_VGLITE_DIAG stage=queue commit_seq=7 submitted_fb=9 submitted_buffer=0x900 current_fb=8 current_buffer=0x800 queued_fb=9 queued_buffer=0x900' >>"${FAKE_DIAGNOSTICS_LOG}"
	printf '%s\n' 'TDVP_VGLITE_DIAG stage=drm_commit commit_seq=7 iface=atomic flags=0x601 result=success elapsed_ms=0.100 submitted_fb=9 submitted_buffer=0x900' >>"${FAKE_DIAGNOSTICS_LOG}"
	printf '%s\n' 'TDVP_VGLITE_DIAG stage=page_flip_event commit_seq=7 drm_sequence=1 drm_timestamp=1.000001' >>"${FAKE_DIAGNOSTICS_LOG}"
	printf '%s\n' 'TDVP_VGLITE_DIAG stage=page_flip_before commit_seq=7 submitted_fb=9 submitted_buffer=0x900 current_fb=8 current_buffer=0x800 queued_fb=9 queued_buffer=0x900' >>"${FAKE_DIAGNOSTICS_LOG}"
	printf '%s\n' 'TDVP_VGLITE_DIAG stage=page_flip_after commit_seq=7 submitted_fb=9 submitted_buffer=0x900 current_fb=9 current_buffer=0x900 queued_fb=0 queued_buffer=(nil)' >>"${FAKE_DIAGNOSTICS_LOG}"
fi
EOF
chmod 0755 "${FAKE_BIN}/id" "${FAKE_BIN}/pgrep" \
	"${TEST_ROOT}/profile" "${TEST_ROOT}/bench"

printf '%s\0' \
	'WLR_RENDERER=vglite' \
	'TDVP_LABWC_VGLITE_FAILURE_RECOVERY=1' \
	'WLR_SCENE_DISABLE_DIRECT_SCANOUT=1' >"${FAKE_PROC}/environ"
printf '%s\n' labwc >"${FAKE_PROC}/comm"
printf '%s\n' '4242 (labwc) S 0 0 0 0 0 0 0 0 0 0 10 20 0 0 0 0 0 0 0 0 30' >"${FAKE_PROC}/stat"
printf '%s\n' $'Name:\tlabwc\nUid:\t1000\t1000\t1000\t1000\nVmRSS:\t12000 kB' >"${FAKE_PROC}/status"
FAKE_PROC_REPLACED="${TEST_ROOT}/proc/4343"
cp -a "${FAKE_PROC}" "${FAKE_PROC_REPLACED}"
printf '%s\n' '4343 (labwc) S 0 0 0 0 0 0 0 0 0 0 10 20 0 0 0 0 0 0 0 0 30' >"${FAKE_PROC_REPLACED}/stat"
printf '%s\n' 5000 >"${FAKE_MODULE}/parameters/infinite_wait_watchdog_ms"

gate_output="$(PATH="${FAKE_BIN}:${PATH}" \
	FAKE_LOG="${FAKE_LOG}" \
	TDVP_PROC_ROOT="${TEST_ROOT}/proc" \
	TDVP_RENDERER_PROFILE_TOOL="${TEST_ROOT}/profile" \
	TDVP_WAYLAND_SHM_BENCH_SESSION="${TEST_ROOT}/bench" \
	TDVP_VGLITE_WATCHDOG_OBSERVER="${WATCHDOG_OBSERVER}" \
	TDVP_VGLITE_SYS_MODULE_ROOT="${FAKE_MODULE}" \
	sh "${GATE}" --expect-vglite-compositor --frames 2 --churn-iterations 0 \
	--format ar24 --repeat 3)"
grep -Fq 'workload_round=1/3' <<<"${gate_output}"
grep -Fq 'workload_round=2/3' <<<"${gate_output}"
grep -Fq 'workload_round=3/3' <<<"${gate_output}"
grep -Fq 'PASS operator_attestation=vglite-compositor' <<<"${gate_output}"
grep -Fq 'labwc_cpu_user_ticks_delta=0' <<<"${gate_output}"
grep -Fq 'labwc_cpu_system_ticks_delta=0' <<<"${gate_output}"
grep -Fq 'labwc_rss_kb_before=12000 labwc_rss_kb_after=12000' <<<"${gate_output}"
[ "$(grep -c '^bench ' "${FAKE_LOG}")" -eq 3 ] ||
	fail "repeat gate did not run the SHM workload three times"
grep -Fq -- '--format ar24' "${FAKE_LOG}" ||
	fail "session gate did not pass the selected AR24 format to the SHM benchmark"

FAKE_DIAGNOSTICS_LOG="${TEST_ROOT}/tdvp-labwc.log"
printf '%s\n' 'pre-gate non-diagnostic record' >"${FAKE_DIAGNOSTICS_LOG}"
diagnostics_output="$(PATH="${FAKE_BIN}:${PATH}" \
	FAKE_LOG="${FAKE_LOG}" \
	FAKE_DIAGNOSTICS_LOG="${FAKE_DIAGNOSTICS_LOG}" \
	TDVP_PROC_ROOT="${TEST_ROOT}/proc" \
	TDVP_RENDERER_PROFILE_TOOL="${TEST_ROOT}/profile" \
	TDVP_WAYLAND_SHM_BENCH_SESSION="${TEST_ROOT}/bench" \
	TDVP_VGLITE_WATCHDOG_OBSERVER="${WATCHDOG_OBSERVER}" \
	TDVP_VGLITE_DIAGNOSTICS_REPORT="${PROJECT_DIR}/k230-sdk-overlay/package/tdvp-vglite-acceptance/src/tdvp-vglite-diagnostics-report" \
	TDVP_VGLITE_SYS_MODULE_ROOT="${FAKE_MODULE}" \
	sh "${GATE}" --expect-vglite-compositor --frames 1 --repeat 1 \
	--diagnostics-log "${FAKE_DIAGNOSTICS_LOG}")"
grep -Fq 'diagnostics_log='"${FAKE_DIAGNOSTICS_LOG}"' diagnostics_skip_lines=1' <<<"${diagnostics_output}" ||
	fail "session gate did not record its diagnostics boundary"
grep -Fq 'tdvp-vglite-diagnostics-report: source='"${FAKE_DIAGNOSTICS_LOG}"' skip_lines=1 records texture_upload=1 vglite_finish=1 texture_readback_finish=0 queue=1 drm_commit=1 page_flip_event=1 page_flip_before=1 page_flip_after=1 page_flip_transitions=1 page_flip_vglite_transitions=1' <<<"${diagnostics_output}" ||
	fail "session gate did not isolate diagnostics appended during its workload"
grep -Fq 'tdvp-vglite-diagnostics-report: PASS' <<<"${diagnostics_output}" ||
	fail "session gate did not require a successful VGLite diagnostics report"

READBACK_FAILURE_LOG="${TEST_ROOT}/tdvp-labwc-readback-failure.log"
printf '%s\n' 'pre-gate non-diagnostic record' >"${READBACK_FAILURE_LOG}"
if readback_failure_output="$(PATH="${FAKE_BIN}:${PATH}" \
	FAKE_LOG="${FAKE_LOG}" \
	FAKE_DIAGNOSTICS_LOG="${READBACK_FAILURE_LOG}" \
	FAKE_DIAGNOSTICS_READBACK_FAILURE=1 \
	TDVP_PROC_ROOT="${TEST_ROOT}/proc" \
	TDVP_RENDERER_PROFILE_TOOL="${TEST_ROOT}/profile" \
	TDVP_WAYLAND_SHM_BENCH_SESSION="${TEST_ROOT}/bench" \
	TDVP_VGLITE_WATCHDOG_OBSERVER="${WATCHDOG_OBSERVER}" \
	TDVP_VGLITE_DIAGNOSTICS_REPORT="${PROJECT_DIR}/k230-sdk-overlay/package/tdvp-vglite-acceptance/src/tdvp-vglite-diagnostics-report" \
	TDVP_VGLITE_SYS_MODULE_ROOT="${FAKE_MODULE}" \
	sh "${GATE}" --expect-vglite-compositor --frames 1 --repeat 1 \
	--diagnostics-log "${READBACK_FAILURE_LOG}" 2>&1)"; then
	fail "session gate accepted a VGLite readback synchronization failure"
fi
grep -Fq 'texture_readback_finish_failure=1' <<<"${readback_failure_output}" ||
	fail "session gate did not expose the readback synchronization failure"
grep -Fq 'readback synchronization' <<<"${readback_failure_output}" ||
	fail "session gate did not reject the readback synchronization failure clearly"

if churn_output="$(PATH="${FAKE_BIN}:${PATH}" \
	FAKE_LOG="${FAKE_LOG}" \
	TDVP_PROC_ROOT="${TEST_ROOT}/proc" \
	TDVP_RENDERER_PROFILE_TOOL="${TEST_ROOT}/profile" \
	TDVP_WAYLAND_SHM_BENCH_SESSION="${TEST_ROOT}/bench" \
	TDVP_VGLITE_WATCHDOG_OBSERVER="${WATCHDOG_OBSERVER}" \
	TDVP_VGLITE_SYS_MODULE_ROOT="${FAKE_MODULE}" \
	sh "${GATE}" --expect-vglite-compositor --churn-iterations 1 2>&1)"; then
	fail "session gate accepted VGLite client churn while Labwc owns the sole context"
fi
grep -Fq 'K230 VGLite is a single-context driver: do not run client churn while Labwc owns /dev/vg_lite' \
	<<<"${churn_output}" || fail "client-churn rejection was not explained"
[ "$(grep -c '^bench ' "${FAKE_LOG}")" -eq 5 ] ||
	fail "rejected client churn started a Wayland workload"

if replacement_output="$(PATH="${FAKE_BIN}:${PATH}" \
	FAKE_LOG="${FAKE_LOG}" \
	FAKE_REPLACED_PID=1 \
	FAKE_PGREP_STATE="${TEST_ROOT}/pgrep-state" \
	TDVP_PROC_ROOT="${TEST_ROOT}/proc" \
	TDVP_RENDERER_PROFILE_TOOL="${TEST_ROOT}/profile" \
	TDVP_WAYLAND_SHM_BENCH_SESSION="${TEST_ROOT}/bench" \
	TDVP_VGLITE_WATCHDOG_OBSERVER="${WATCHDOG_OBSERVER}" \
	TDVP_VGLITE_SYS_MODULE_ROOT="${FAKE_MODULE}" \
	sh "${GATE}" --expect-vglite-compositor --frames 1 --churn-iterations 0 \
	--repeat 1 2>&1)"; then
	fail "session gate accepted a Labwc replacement during Gate 1"
fi
grep -Fq 'Labwc PID changed during Gate 1: expected=4242 actual=4343' <<<"${replacement_output}" ||
	fail "Labwc replacement did not produce a clear diagnostic"

printf '%s\n' 4000 >"${FAKE_MODULE}/parameters/infinite_wait_watchdog_ms"
if mismatch_output="$(PATH="${FAKE_BIN}:${PATH}" \
	TDVP_PROC_ROOT="${TEST_ROOT}/proc" \
	TDVP_RENDERER_PROFILE_TOOL="${TEST_ROOT}/profile" \
	TDVP_WAYLAND_SHM_BENCH_SESSION="${TEST_ROOT}/bench" \
	TDVP_VGLITE_WATCHDOG_OBSERVER="${WATCHDOG_OBSERVER}" \
	TDVP_VGLITE_SYS_MODULE_ROOT="${FAKE_MODULE}" \
	sh "${GATE}" --expect-vglite-compositor --frames 1 --churn-iterations 0 2>&1)"; then
	fail "session gate accepted a non-default watchdog value"
fi
grep -Fq 'watchdog parameter mismatch: expected=5000 actual=4000' <<<"${mismatch_output}" ||
	fail "watchdog mismatch did not produce a clear diagnostic"

if invalid_output="$(sh "${GATE}" --expect-vglite-compositor --format nv12 2>&1)"; then
	fail "session gate accepted an unsupported SHM format"
fi
grep -Fq -- '--format must be xr24 or ar24' <<<"${invalid_output}" ||
	fail "unsupported SHM format did not produce a clear diagnostic"

printf '%s\n' 'test-tdvp-vglite-session-gate: PASS'

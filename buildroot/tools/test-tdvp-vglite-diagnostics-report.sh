#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPORT="${PROJECT_DIR}/k230-sdk-overlay/package/tdvp-vglite-acceptance/src/tdvp-vglite-diagnostics-report"
PACKAGE_MK="${PROJECT_DIR}/k230-sdk-overlay/package/tdvp-vglite-acceptance/tdvp-vglite-acceptance.mk"

fail() {
	printf '%s\n' "test-tdvp-vglite-diagnostics-report: FAIL: $*" >&2
	exit 1
}

[ -f "${REPORT}" ] || fail "missing diagnostics report tool"
bash -n "${REPORT}"
grep -Fq 'TDVP_VGLITE_DIAG' "${REPORT}"
grep -Fq 'vglite_finish' "${REPORT}"
grep -Fq 'texture_upload' "${REPORT}"
grep -Fq 'texture_readback_finish' "${REPORT}"
grep -Fq 'page_flip_vglite_transition_records' "${REPORT}"
grep -Fq 'drm_commit' "${REPORT}"
grep -Fq 'page_flip_event' "${REPORT}"
grep -Fq 'result_nonzero' "${REPORT}"
grep -Fq 'pass_ok_false' "${REPORT}"
grep -Fq 'tdvp-vglite-diagnostics-report' "${PACKAGE_MK}"

TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/tdvp-vglite-diagnostics.XXXXXX")"
cleanup() {
	rm -rf -- "${TEST_ROOT}"
}
trap cleanup EXIT

GOOD_LOG="${TEST_ROOT}/good.log"
printf '%s\n' \
	'TDVP_VGLITE_DIAG stage=texture_upload texture=0x1 damage_rects=2 copied_bytes=100 source_bytes=120 copy_calls=2 cache_flush_pending=1' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x1000 elapsed_ms=1.500 result=0 pass_ok=1 texture_clip_rects=6 texture_blit_attempts=7 texture_cache_flushes=2 solid_clip_rects=8 solid_blit_attempts=9 state_recovery_attempted=0 state_recovery_ok=1' \
	'TDVP_VGLITE_DIAG stage=queue commit_seq=41 submitted_fb=17 submitted_buffer=0x1000 current_fb=16 current_buffer=0x0 queued_fb=17 queued_buffer=0x1000' \
	'TDVP_VGLITE_DIAG stage=drm_commit commit_seq=41 iface=atomic flags=0x601 result=success elapsed_ms=0.250 submitted_fb=17 submitted_buffer=0x1000' \
	'TDVP_VGLITE_DIAG stage=page_flip_event commit_seq=41 drm_sequence=123 drm_timestamp=77.000001' \
	'TDVP_VGLITE_DIAG stage=page_flip_before commit_seq=41 submitted_fb=17 submitted_buffer=0x1000 current_fb=16 current_buffer=0x0 queued_fb=17 queued_buffer=0x1000' \
	'TDVP_VGLITE_DIAG stage=page_flip_after commit_seq=41 submitted_fb=17 submitted_buffer=0x1000 current_fb=17 current_buffer=0x1000 queued_fb=0 queued_buffer=(nil)' \
	'TDVP_VGLITE_DIAG stage=texture_upload texture=0x2 damage_rects=4 copied_bytes=300 source_bytes=300 copy_calls=4 cache_flush_pending=1' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x2 elapsed_ms=0.500 result=0 pass_ok=1 texture_clip_rects=3 texture_blit_attempts=4 texture_cache_flushes=0 solid_clip_rects=5 solid_blit_attempts=6 state_recovery_attempted=0 state_recovery_ok=1' \
	>"${GOOD_LOG}"

good_output="$(sh "${REPORT}" --log "${GOOD_LOG}" --require-finish 2 --require-page-flip-transition 1)"
grep -Fq 'records texture_upload=2 vglite_finish=2 texture_readback_finish=0 queue=1 drm_commit=1 page_flip_event=1 page_flip_before=1 page_flip_after=1 page_flip_transitions=1 page_flip_vglite_transitions=1' <<<"${good_output}"
grep -Fq 'damage_rects_total=6 damage_rects_avg=3.000 copied_bytes_total=400 copied_bytes_avg=200.000 source_bytes_total=420 source_bytes_avg=210.000 copy_calls_total=6 copy_calls_avg=3.000 cache_flush_pending_total=2' <<<"${good_output}"
grep -Fq 'elapsed_ms_min=0.500 elapsed_ms_avg=1.000 elapsed_ms_max=1.500 texture_clip_rects_total=9 texture_clip_rects_avg=4.500 texture_blit_attempts_total=11 texture_blit_attempts_avg=5.500 texture_cache_flushes_total=2 texture_cache_flushes_avg=1.000 solid_clip_rects_total=13 solid_clip_rects_avg=6.500 solid_blit_attempts_total=15 solid_blit_attempts_avg=7.500 state_recovery_attempts=0 state_recovery_failures=0' <<<"${good_output}"
grep -Fq 'drm_commit elapsed_ms_min=0.250 elapsed_ms_avg=0.250 elapsed_ms_max=0.250 success=1 failure=0' <<<"${good_output}"
grep -Fq 'page_flip events=1 before_snapshots=1 after_snapshots=1 verified_transitions=1 verified_vglite_transitions=1' <<<"${good_output}"
grep -Fq 'failures result_nonzero=0 pass_ok_false=0 state_recovery_failure=0 texture_readback_finish_failure=0 drm_commit_failure=0' <<<"${good_output}"
grep -Fq 'tdvp-vglite-diagnostics-report: PASS' <<<"${good_output}"

new_records_output="$(sh "${REPORT}" --log "${GOOD_LOG}" --skip-lines 7 --require-finish 1)"
grep -Fq 'skip_lines=7 records texture_upload=1 vglite_finish=1 texture_readback_finish=0 queue=0 drm_commit=0 page_flip_event=0 page_flip_before=0 page_flip_after=0 page_flip_transitions=0 page_flip_vglite_transitions=0' <<<"${new_records_output}" ||
	fail "skip-lines did not isolate newly appended diagnostics"
grep -Fq 'elapsed_ms_min=0.500 elapsed_ms_avg=0.500 elapsed_ms_max=0.500' <<<"${new_records_output}" ||
	fail "skip-lines did not retain the expected finish record"

FAILURE_LOG="${TEST_ROOT}/failure.log"
printf '%s\n' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x3 elapsed_ms=3.000 result=-5 pass_ok=0 texture_clip_rects=0 texture_blit_attempts=0 texture_cache_flushes=0 solid_clip_rects=0 solid_blit_attempts=0 state_recovery_attempted=1 state_recovery_ok=0' \
	>"${FAILURE_LOG}"
if failure_output="$(sh "${REPORT}" --log "${FAILURE_LOG}" 2>&1)"; then
	fail "failed renderer pass was accepted"
fi
grep -Fq 'failures result_nonzero=1 pass_ok_false=1 state_recovery_failure=1 texture_readback_finish_failure=0 drm_commit_failure=0' <<<"${failure_output}" ||
	fail "failed renderer pass was not counted"
grep -Fq 'VGLite state recovery, result, renderer pass, readback synchronization, or DRM commit reported a failure' <<<"${failure_output}" ||
	fail "failed renderer pass had no clear diagnostic"

DRM_FAILURE_LOG="${TEST_ROOT}/drm-failure.log"
printf '%s\n' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x4 elapsed_ms=1.000 result=0 pass_ok=1 texture_clip_rects=0 texture_blit_attempts=0 texture_cache_flushes=0 solid_clip_rects=0 solid_blit_attempts=0 state_recovery_attempted=0 state_recovery_ok=1' \
	'TDVP_VGLITE_DIAG stage=drm_commit commit_seq=42 iface=atomic flags=0x601 result=failure elapsed_ms=0.750 submitted_fb=18 submitted_buffer=0x2000' \
	>"${DRM_FAILURE_LOG}"
if drm_failure_output="$(sh "${REPORT}" --log "${DRM_FAILURE_LOG}" 2>&1)"; then
	fail "failed DRM commit was accepted"
fi
grep -Fq 'failures result_nonzero=0 pass_ok_false=0 state_recovery_failure=0 texture_readback_finish_failure=0 drm_commit_failure=1' <<<"${drm_failure_output}" ||
	fail "failed DRM commit was not counted"
grep -Fq 'VGLite state recovery, result, renderer pass, readback synchronization, or DRM commit reported a failure' <<<"${drm_failure_output}" ||
	fail "failed DRM commit had no clear diagnostic"

READBACK_FAILURE_LOG="${TEST_ROOT}/readback-failure.log"
printf '%s\n' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x5 elapsed_ms=1.000 result=0 pass_ok=1 texture_clip_rects=0 texture_blit_attempts=0 texture_cache_flushes=0 solid_clip_rects=0 solid_blit_attempts=0 state_recovery_attempted=0 state_recovery_ok=1' \
	'TDVP_VGLITE_DIAG stage=texture_readback_finish result=-5 renderer_quarantined=1' \
	>"${READBACK_FAILURE_LOG}"
if readback_failure_output="$(sh "${REPORT}" --log "${READBACK_FAILURE_LOG}" 2>&1)"; then
	fail "failed VGLite readback synchronization was accepted"
fi
grep -Fq 'texture_readback_finish_failure=1' <<<"${readback_failure_output}" ||
	fail "failed VGLite readback synchronization was not counted"
grep -Fq 'VGLite state recovery, result, renderer pass, readback synchronization, or DRM commit reported a failure' <<<"${readback_failure_output}" ||
	fail "failed VGLite readback synchronization had no clear diagnostic"

INVALID_READBACK_LOG="${TEST_ROOT}/invalid-readback.log"
printf '%s\n' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x6 elapsed_ms=1.000 result=0 pass_ok=1 texture_clip_rects=0 texture_blit_attempts=0 texture_cache_flushes=0 solid_clip_rects=0 solid_blit_attempts=0 state_recovery_attempted=0 state_recovery_ok=1' \
	'TDVP_VGLITE_DIAG stage=texture_readback_finish result=0 renderer_quarantined=1' \
	>"${INVALID_READBACK_LOG}"
if invalid_readback_output="$(sh "${REPORT}" --log "${INVALID_READBACK_LOG}" 2>&1)"; then
	fail "successful readback result with renderer quarantine was accepted"
fi
grep -Fq 'texture_readback_finish record is missing a failed-quarantine metric' <<<"${invalid_readback_output}" ||
	fail "inconsistent readback quarantine record had no clear diagnostic"

INVALID_RECOVERY_LOG="${TEST_ROOT}/invalid-recovery.log"
printf '%s\n' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x5 elapsed_ms=1.000 result=0 pass_ok=1 texture_clip_rects=0 texture_blit_attempts=0 texture_cache_flushes=0 solid_clip_rects=0 solid_blit_attempts=0 state_recovery_attempted=0 state_recovery_ok=0' \
	>"${INVALID_RECOVERY_LOG}"
if invalid_recovery_output="$(sh "${REPORT}" --log "${INVALID_RECOVERY_LOG}" 2>&1)"; then
	fail "impossible state recovery result was accepted"
fi
grep -Fq 'state_recovery_ok must be 1 when state_recovery_attempted is 0' <<<"${invalid_recovery_output}" ||
	fail "impossible state recovery result had no clear diagnostic"

if transition_output="$(sh "${REPORT}" --log "${FAILURE_LOG}" --require-page-flip-transition 1 2>&1)"; then
	fail "missing page-flip transition was accepted"
fi
grep -Fq 'VGLite-linked page_flip transitions=0 required=1' <<<"${transition_output}" ||
	fail "missing page-flip transition had no clear diagnostic"

UNLINKED_PAGE_FLIP_LOG="${TEST_ROOT}/unlinked-page-flip.log"
printf '%s\n' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x7 elapsed_ms=1.000 result=0 pass_ok=1 texture_clip_rects=0 texture_blit_attempts=0 texture_cache_flushes=0 solid_clip_rects=0 solid_blit_attempts=0 state_recovery_attempted=0 state_recovery_ok=1' \
	'TDVP_VGLITE_DIAG stage=page_flip_before commit_seq=43 submitted_fb=19 submitted_buffer=0x1900 current_fb=18 current_buffer=0x1800 queued_fb=19 queued_buffer=0x1900' \
	'TDVP_VGLITE_DIAG stage=page_flip_after commit_seq=43 submitted_fb=19 submitted_buffer=0x1900 current_fb=19 current_buffer=0x1900 queued_fb=0 queued_buffer=(nil)' \
	>"${UNLINKED_PAGE_FLIP_LOG}"
if unlinked_page_flip_output="$(sh "${REPORT}" --log "${UNLINKED_PAGE_FLIP_LOG}" --require-page-flip-transition 1 2>&1)"; then
	fail "page-flip with an unrelated VGLite finish was accepted"
fi
grep -Fq 'page_flip submitted buffer has no unconsumed successful VGLite finish' <<<"${unlinked_page_flip_output}" ||
	fail "unlinked page-flip had no clear diagnostic"

INVALID_CACHE_FLUSH_LOG="${TEST_ROOT}/invalid-cache-flush.log"
printf '%s\n' \
	'TDVP_VGLITE_DIAG stage=vglite_finish buffer=0x8 elapsed_ms=1.000 result=0 pass_ok=1 texture_clip_rects=1 texture_blit_attempts=1 texture_cache_flushes=2 solid_clip_rects=0 solid_blit_attempts=0 state_recovery_attempted=0 state_recovery_ok=1' \
	>"${INVALID_CACHE_FLUSH_LOG}"
if invalid_cache_flush_output="$(sh "${REPORT}" --log "${INVALID_CACHE_FLUSH_LOG}" 2>&1)"; then
	fail "impossible texture cache flush count was accepted"
fi
grep -Fq 'vglite_finish texture cache flushes exceed texture blits' <<<"${invalid_cache_flush_output}" ||
	fail "impossible texture cache flush count had no clear diagnostic"

if short_output="$(sh "${REPORT}" --log "${GOOD_LOG}" --require-finish 3 2>&1)"; then
	fail "too few finish records were accepted"
fi
grep -Fq 'vglite_finish records=2 required=3' <<<"${short_output}" ||
	fail "finish-count requirement had no clear diagnostic"

INVALID_LOG="${TEST_ROOT}/invalid.log"
printf '%s\n' 'TDVP_VGLITE_DIAG stage=unknown' >"${INVALID_LOG}"
if invalid_output="$(sh "${REPORT}" --log "${INVALID_LOG}" 2>&1)"; then
	fail "unknown VGLite diagnostic stage was accepted"
fi
grep -Fq 'malformed diagnostic records=1 first_error=unrecognised TDVP_VGLITE_DIAG stage' <<<"${invalid_output}" ||
	fail "unknown diagnostic stage had no clear diagnostic"

EMPTY_LOG="${TEST_ROOT}/empty.log"
: >"${EMPTY_LOG}"
if empty_output="$(sh "${REPORT}" --log "${EMPTY_LOG}" 2>&1)"; then
	fail "empty diagnostic log was accepted"
fi
grep -Fq 'no TDVP_VGLITE_DIAG records found' <<<"${empty_output}" ||
	fail "empty diagnostic log had no clear diagnostic"

printf '%s\n' 'test-tdvp-vglite-diagnostics-report: PASS'

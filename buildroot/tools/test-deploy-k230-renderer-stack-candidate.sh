#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEPLOY="${SCRIPT_DIR}/deploy-k230-renderer-stack-candidate.sh"

# Gate 1 is meaningful only when the candidate carries both the SHM workload
# and its session wrapper.  The renderer deploy intentionally installs a
# bounded file set, so make omission of either program a local regression
# rather than a discovery made on the only hardware test device.
for path in \
	/usr/bin/labwc \
	/usr/lib/libwlroots-0.18.so \
	/usr/lib/libvg_lite.so \
	/usr/bin/tdvp-display-smoke \
	/usr/bin/tdvp-vblank-observer \
	/usr/bin/tdvp-kms-capability-observer \
	/usr/bin/tdvp-vglite-probe \
	/usr/bin/tdvp-vglite-client-churn \
	/usr/bin/tdvp-vglite-watchdog-observer \
	/usr/bin/tdvp-vglite-session-gate \
	/usr/bin/tdvp-vglite-diagnostics-report \
	/usr/bin/tdvp-vglite-inflight-close-gate \
	/usr/bin/tdvp-wayland-shm-bench \
	/usr/bin/tdvp-wayland-shm-bench-session \
	/usr/local/bin/tdvp-labwc-session \
	/usr/local/bin/tdvp-renderer-profile \
	/usr/libexec/tdvp/tdvp-kms-acceptance \
	/usr/libexec/tdvp/tdvp-kms-maintenance-recover \
	/usr/lib/systemd/system/tdvp-kms-acceptance.service \
	/usr/lib/systemd/system/tdvp-kms-maintenance.service \
	/usr/lib/udev/rules.d/60-tdvp-vg-lite.rules \
	/etc/tdvp/labwc/environment \
	; do
	grep -Fq "${path}" "${DEPLOY}"
done

# The target-side manifest parser must accept exactly the same Gate 1 paths;
# otherwise a locally complete candidate would fail only after it reaches the
# board and a backup has already been created.
grep -Fq '/usr/bin/tdvp-wayland-shm-bench|/usr/bin/tdvp-wayland-shm-bench-session' "${DEPLOY}"
grep -Fq 'renderer candidate is missing required file' "${DEPLOY}"
grep -Fq 'TDVP-renderer-stage-verified' "${DEPLOY}"
grep -Fq 'payload_archive="$(mktemp)"' "${DEPLOY}"
grep -Fq 'PAYLOAD.tar' "${DEPLOY}"
grep -Fq 'tar -xf ${REMOTE_STAGE}/PAYLOAD.tar -C ${REMOTE_STAGE}/root' "${DEPLOY}"
grep -Fq 'previous.manifest' "${DEPLOY}"
grep -Fq 'systemctl daemon-reload' "${DEPLOY}"
grep -Fq 'reboot separately before running Gate 0.5' "${DEPLOY}"

printf '%s\n' 'test-deploy-k230-renderer-stack-candidate: PASS'

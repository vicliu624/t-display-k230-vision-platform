#!/usr/bin/env bash
set -euo pipefail

TARGET_DIR="${1:?Buildroot did not provide the rootfs directory}"
TDVP_PASSWORD_HASH='$5$tdvp-repro-2026$oF1fY2harBx8EsEeKt.jshl8qujlwVIvSW8pUxsnZID'

# The vendor rootfs preset historically enables BusyBox telnetd. The staged
# BusyBox configuration disables the applet; remove every possible residual
# here as a final image-boundary safeguard.
rm -f \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/telnetd.service" \
	"${TARGET_DIR}/usr/lib/systemd/system/telnetd.service" \
	"${TARGET_DIR}/usr/sbin/telnetd"

grep -q '^tdvp:' "${TARGET_DIR}/etc/passwd" || {
    printf 'TDVP packaging error: graphical account is missing from /etc/passwd\n' >&2
    exit 1
}

grep -q '^tdvp:' "${TARGET_DIR}/etc/shadow" || {
    printf 'TDVP packaging error: graphical account is missing from /etc/shadow\n' >&2
    exit 1
}

sed -i "s|^tdvp:[^:]*:|tdvp:${TDVP_PASSWORD_HASH}:|" "${TARGET_DIR}/etc/shadow"

# Buildroot enables the package-provided generic wpa_supplicant.service during
# its systemd rootfs finalization, which happens after post-build.sh.  The
# platform owns one explicit wlan0 instance because its configuration lives at
# /etc/wpa_supplicant/wpa_supplicant-wlan0.conf.  Apply this at the final
# rootfs boundary so the image cannot accidentally boot with a service whose
# configuration path does not exist.
WANTS_DIR="${TARGET_DIR}/etc/systemd/system/multi-user.target.wants"
mkdir -p "${WANTS_DIR}"
rm -f "${WANTS_DIR}/wpa_supplicant.service"
ln -sfn ../../../../usr/lib/systemd/system/wpa_supplicant@.service \
    "${WANTS_DIR}/wpa_supplicant@wlan0.service"

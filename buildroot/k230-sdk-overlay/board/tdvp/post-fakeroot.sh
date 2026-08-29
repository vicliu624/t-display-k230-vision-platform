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

# Buildroot performs package service finalization after post-build.sh. Keep the
# final service graph exclusive: NetworkManager owns wpa_supplicant through
# D-Bus; PulseAudio is deliberately started in the authenticated Labwc user
# session so wfplug-volumepulse has the matching tdvp cookie.
WANTS_DIR="${TARGET_DIR}/etc/systemd/system/multi-user.target.wants"
mkdir -p "${WANTS_DIR}"
rm -f \
	"${TARGET_DIR}/etc/init.d/S40network" \
	"${WANTS_DIR}/systemd-networkd.service" \
	"${TARGET_DIR}/etc/systemd/system/sockets.target.wants/systemd-networkd.socket" \
	"${TARGET_DIR}/etc/systemd/system/network-online.target.wants/systemd-networkd-wait-online.service" \
	"${TARGET_DIR}/etc/systemd/system/dbus-org.freedesktop.network1.service" \
	"${WANTS_DIR}/wpa_supplicant.service" \
	"${WANTS_DIR}/wpa_supplicant@wlan0.service" \
	"${WANTS_DIR}/pulseaudio.service"
ln -sfn ../../../../usr/lib/systemd/system/NetworkManager.service \
	"${WANTS_DIR}/NetworkManager.service"
ln -sfn ../../../../usr/lib/systemd/system/tdvp-rootfs-expand.service \
	"${WANTS_DIR}/tdvp-rootfs-expand.service"

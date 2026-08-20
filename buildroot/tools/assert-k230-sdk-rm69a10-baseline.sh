#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  assert-k230-sdk-rm69a10-baseline.sh [--patch-only] <sdk-worktree>

Validate the T-Display K230 Labwc desktop profile and its SD-card image.
EOF
}

PATCH_ONLY=0
if [ "${1:-}" = "--patch-only" ]; then
	PATCH_ONLY=1
	shift
fi
if [ "$#" -ne 1 ]; then
	usage
	exit 2
fi

WORKTREE="$(cd "$1" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROFILE="k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
MANIFEST="${WORKTREE}/.tdvp/sdk-baseline-manifest"
BUILD="${WORKTREE}/output/${PROFILE}"
CONFIG="${BUILD}/.config"
IMAGES="${BUILD}/images"
ROOTFS="${IMAGES}/rootfs.ext2"
STAGED_OVERLAY="${WORKTREE}/buildroot-overlay"

fail() {
	printf 'TDVP desktop assertion: %s\n' "$*" >&2
	exit 1
}

require_file() {
	[ -f "$1" ] || fail "missing file: $1"
}

require_line() {
	grep -Fqx -- "$2" "$1" || fail "missing line in $1: $2"
}

require_content() {
	grep -aFq -- "$2" "$1" || fail "missing content in $1: $2"
}

packages=(
	gtk-layer-shell
	labwc
	sfwbar
	tdvp-greetd
	tdvp-gtkgreet
	tdvp-greeter
	tdvp-dejavu-fonts
	tdvp-display-smoke
	tdvp-keyboard-layout
	tdvp-kpu-acceptance
	tdvp-labwc-desktop
	tdvp-wayland-acceptance
	vicliu-pocket-linux-hardware
)

required_config=(
	BR2_INIT_SYSTEMD
	BR2_PACKAGE_SYSTEMD
	BR2_PACKAGE_SEATD
	BR2_PACKAGE_DBUS
	BR2_PACKAGE_LIBGTK3
	BR2_PACKAGE_LIBGTK3_WAYLAND
	BR2_PACKAGE_GTK_LAYER_SHELL
	BR2_PACKAGE_LABWC
	BR2_PACKAGE_SFWBAR
	BR2_PACKAGE_SWAYBG
	BR2_PACKAGE_FOOT
	BR2_PACKAGE_TDVP_LABWC_DESKTOP
	BR2_PACKAGE_TDVP_GREETD
	BR2_PACKAGE_TDVP_GTKGREET
	BR2_PACKAGE_TDVP_GREETER
	BR2_PACKAGE_TDVP_KPU_ACCEPTANCE
	BR2_PACKAGE_VICLIU_POCKET_LINUX_HARDWARE
	BR2_PACKAGE_TDVP_DISPLAY_SMOKE
	BR2_PACKAGE_TDVP_KEYBOARD_LAYOUT
	BR2_PACKAGE_TDVP_WAYLAND_ACCEPTANCE
)

require_file "${MANIFEST}"
require_line "${MANIFEST}" "profile=${PROFILE}"
require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/configs/${PROFILE}"

for package in "${packages[@]}"; do
	require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/${package}/Config.in"
	require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/${package}/${package}.mk"
done

for symbol in "${required_config[@]}"; do
	require_line "${PROJECT_DIR}/buildroot/k230-sdk-overlay/configs/${PROFILE}" "${symbol}=y"
done

DESKTOP_SOURCE="${PROJECT_DIR}/user-space/tdvp-labwc-desktop/src"
GREETER_SOURCE="${PROJECT_DIR}/user-space/tdvp-greeter/src"
GTK_GREET_PATCH="${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-gtkgreet/0001-tdvp-fixed-session-layout.patch"
require_content "${DESKTOP_SOURCE}/environment" 'WLR_DRM_DEVICES=/dev/dri/card0'
require_content "${DESKTOP_SOURCE}/environment" 'LIBSEAT_BACKEND=seatd'
require_content "${DESKTOP_SOURCE}/environment" 'TDVP_K230_OUTPUT=DSI-1'
require_content "${DESKTOP_SOURCE}/environment" 'TDVP_K230_OUTPUT_TRANSFORM=90'
require_content "${DESKTOP_SOURCE}/tdvp-labwc-session" 'set -a'
require_content "${DESKTOP_SOURCE}/tdvp-labwc-session" 'exec /usr/bin/dbus-run-session -- /usr/bin/labwc'
require_content "${DESKTOP_SOURCE}/autostart" '/usr/bin/swaybg -o "$TDVP_K230_OUTPUT"'
require_content "${DESKTOP_SOURCE}/autostart" '-i /usr/share/backgrounds/tdvp-pda-paper.svg -m fill'
require_file "${DESKTOP_SOURCE}/tdvp-sfwbar-session"
require_content "${DESKTOP_SOURCE}/tdvp-sfwbar-session" 'while :; do'
require_content "${DESKTOP_SOURCE}/tdvp-sfwbar-session" '/usr/bin/sfwbar -f /etc/sfwbar/sfwbar.config'
require_content "${DESKTOP_SOURCE}/autostart" '/usr/local/bin/tdvp-sfwbar-session &'
require_file "${DESKTOP_SOURCE}/tdvp-launcher.widget"
require_content "${DESKTOP_SOURCE}/sfwbar.config" 'widget "tdvp-launcher.widget"'
require_content "${DESKTOP_SOURCE}/tdvp-launcher.widget" 'action = Exec("/usr/local/bin/vpl-app-launcher")'
require_content "${DESKTOP_SOURCE}/sfwbar.config" 'taskbar {'
require_content "${DESKTOP_SOURCE}/sfwbar.config" 'Set ThicknessHint = "70px"'
require_content "${DESKTOP_SOURCE}/sfwbar.config" 'box-shadow: inset 0 -6px #e66a2c;'
require_content "${DESKTOP_SOURCE}/sfwbar.config" 'include "network.widget"'
require_content "${DESKTOP_SOURCE}/sfwbar.config" 'widget "network.widget"'
require_content "${DESKTOP_SOURCE}/rc.xml" '<default />'
require_content "${DESKTOP_SOURCE}/rc.xml" '<keybind key="Super_L" onRelease="yes">'
require_content "${DESKTOP_SOURCE}/rc.xml" 'command="/usr/local/bin/vpl-app-launcher"'
require_content "${DESKTOP_SOURCE}/rc.xml" '<mouseEmulation>yes</mouseEmulation>'
if grep -Fq '<mapToOutput>' "${DESKTOP_SOURCE}/rc.xml"; then
	fail 'desktop Labwc touch configuration must not remap the output'
fi
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher" '/usr/bin/pidof wofi'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher" '[ -x /usr/bin/wofi ] || exit 1'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher" 'exec /usr/bin/wofi --show drun'
if grep -Fq 'exec /usr/bin/foot' "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher"; then
	fail 'application launcher must not fall back to foot'
fi
require_file "${DESKTOP_SOURCE}/foot.ini"
require_content "${DESKTOP_SOURCE}/foot.ini" 'initial-window-mode=maximized'
require_file "${DESKTOP_SOURCE}/backgrounds/tdvp-pda-paper.svg"
require_content "${GREETER_SOURCE}/config.toml" 'command = "/usr/local/bin/tdvp-greeter-session"'
require_content "${GREETER_SOURCE}/config.toml" 'user = "greeter"'
require_content "${GREETER_SOURCE}/greetd" 'session    required   pam_unix.so'
require_content "${GREETER_SOURCE}/greetd-greeter" 'session    required   pam_unix.so'
require_content "${GREETER_SOURCE}/tdvp-greeter-session" '. /etc/tdvp/labwc/environment'
require_content "${GREETER_SOURCE}/tdvp-greeter-session" 'XDG_RUNTIME_DIR="${HOME}/.cache/wayland-runtime"'
require_content "${GREETER_SOURCE}/tdvp-greeter-labwc" '--transform "${TDVP_K230_OUTPUT_TRANSFORM}"'
require_content "${GREETER_SOURCE}/greetd.service" 'tdvp-keyboard-layout.service'
require_content "${GREETER_SOURCE}/gtkgreet.css" 'min-width: 740px;'
require_content "${GREETER_SOURCE}/gtkgreet.css" 'min-height: 62px;'
require_content "${GREETER_SOURCE}/gtkgreet.css" 'background-size: 14px 14px;'
require_content "${GTK_GREET_PATCH}" 'gtk_layer_set_keyboard_interactivity(GTK_WINDOW(ctx->window), TRUE);'

if [ "${PATCH_ONLY}" = "1" ]; then
	for package in "${packages[@]}"; do
		require_file "${STAGED_OVERLAY}/package/${package}/Config.in"
		require_file "${STAGED_OVERLAY}/package/${package}/${package}.mk"
	done
	require_file "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-launcher.widget"
	require_file "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-sfwbar-session"
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-sfwbar-session" 'while :; do'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-sfwbar-session" '/usr/bin/sfwbar -f /etc/sfwbar/sfwbar.config'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/autostart" '/usr/local/bin/tdvp-sfwbar-session &'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/sfwbar.config" 'widget "tdvp-launcher.widget"'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-launcher.widget" 'action = Exec("/usr/local/bin/vpl-app-launcher")'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/rc.xml" '<keybind key="Super_L" onRelease="yes">'
	if grep -Fq '<mapToOutput>' "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/rc.xml"; then
		fail 'staged desktop Labwc touch configuration must not remap the output'
	fi
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher" '/usr/bin/pidof wofi'
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher" '[ -x /usr/bin/wofi ] || exit 1'
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher" 'exec /usr/bin/wofi --show drun'
	if grep -Fq 'exec /usr/bin/foot' "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher"; then
		fail 'staged application launcher must not fall back to foot'
	fi
	printf '%s\n' 'TDVP desktop patch-input assertion: PASS'
	exit 0
fi

require_file "${CONFIG}"
require_file "${ROOTFS}"
require_file "${IMAGES}/sysimage-sdcard.img"
require_file "${IMAGES}/tdvp-image-manifest"
for symbol in "${required_config[@]}"; do
	require_line "${CONFIG}" "${symbol}=y"
done
bash "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/verify-sdcard-image.sh" "${IMAGES}"
require_content "${IMAGES}/tdvp-image-manifest" "profile=${PROFILE}"
require_content "${IMAGES}/tdvp-image-manifest" 'desktop=labwc'
require_content "${IMAGES}/tdvp-image-manifest" 'panel=sfwbar'
require_content "${IMAGES}/tdvp-image-manifest" 'background=swaybg'
require_content "${IMAGES}/tdvp-image-manifest" 'terminal=foot'
require_content "${IMAGES}/tdvp-image-manifest" 'display_manager=greetd'
require_content "${IMAGES}/tdvp-image-manifest" 'greeter=gtkgreet'
require_content "${IMAGES}/tdvp-image-manifest" 'session=tdvp-labwc-session'
printf '%s\n' 'TDVP desktop assertion: PASS'

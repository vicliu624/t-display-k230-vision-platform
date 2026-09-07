#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
	printf 'usage: %s <buildroot-output-directory>\n' "$0" >&2
	exit 2
fi

OUTPUT_DIR="$(cd "$1" && pwd)"
TARGET_DIR="$OUTPUT_DIR/target"
IMAGE_DIR="$OUTPUT_DIR/images"
REPORT="$IMAGE_DIR/k230-hardware-build-preflight.txt"

mkdir -p "$IMAGE_DIR"

require_file() {
	[ -f "$1" ] || {
		printf 'TDVP K230 preflight: required file is missing: %s\n' "$1" >&2
		exit 1
	}
}

require_buildroot_selection() {
	local symbol="$1"

	grep -Fqx "${symbol}=y" "$OUTPUT_DIR/.config" || {
		printf 'TDVP K230 preflight: required Buildroot selection is absent: %s\n' "$symbol" >&2
		exit 1
	}
}

reject_buildroot_selection() {
	local symbol="$1"

	if grep -Fqx "${symbol}=y" "$OUTPUT_DIR/.config"; then
		printf 'TDVP K230 preflight: retired Buildroot selection is still enabled: %s\n' "$symbol" >&2
		exit 1
	fi
}

require_file "$OUTPUT_DIR/.config"
require_buildroot_selection BR2_PACKAGE_TDVP_CPU1_VISION
for symbol in BR2_PACKAGE_TDVP_CAMERA_ISP BR2_PACKAGE_TDVP_CAMERA_ISP_RUNTIME \
	BR2_PACKAGE_TDVP_KPU_ACCEPTANCE BR2_PACKAGE_VVCAM BR2_PACKAGE_AI2D_KPU \
	BR2_PACKAGE_LIBMMZ BR2_PACKAGE_LIBNNCASE; do
	reject_buildroot_selection "$symbol"
done
bash "$(dirname "$0")/../k230-sdk-overlay/board/tdvp/cpu1/vision/verify-rootfs.sh" "$TARGET_DIR"
for symbol in \
	BR2_INIT_SYSTEMD \
	BR2_PACKAGE_SYSTEMD \
	BR2_PACKAGE_SEATD \
	BR2_PACKAGE_LIBGTK3 \
	BR2_PACKAGE_LIBGTK3_WAYLAND \
	BR2_PACKAGE_GTK_LAYER_SHELL \
	BR2_PACKAGE_LABWC \
	BR2_PACKAGE_GTKMM3 \
	BR2_PACKAGE_LIBFM_EXTRA \
	BR2_PACKAGE_LIBMENU_CACHE \
	BR2_PACKAGE_LIBFM \
	BR2_PACKAGE_PCMANFM \
	BR2_PACKAGE_OPKG \
	BR2_PACKAGE_OPKG_GPG_SIGN \
	BR2_PACKAGE_GNUPG2 \
	BR2_PACKAGE_GNUPG2_GPGV \
	BR2_PACKAGE_TDVP_OPKG_TRUST \
	BR2_PACKAGE_GLIB_NETWORKING \
	BR2_PACKAGE_NETWORK_MANAGER \
	BR2_PACKAGE_NETWORK_MANAGER_CLI \
	BR2_PACKAGE_GPTFDISK \
	BR2_PACKAGE_GPTFDISK_SGDISK \
	BR2_PACKAGE_E2FSPROGS \
	BR2_PACKAGE_E2FSPROGS_RESIZE2FS \
	BR2_PACKAGE_LIBCANBERRA \
	BR2_PACKAGE_SOUND_THEME_FREEDESKTOP \
	BR2_PACKAGE_LIBSECRET \
	BR2_PACKAGE_LIBNMA \
	BR2_PACKAGE_PULSEAUDIO \
	BR2_PACKAGE_PULSEAUDIO_DAEMON \
	BR2_PACKAGE_WF_PANEL_PI \
	BR2_PACKAGE_WFPLUG_BATT \
	BR2_PACKAGE_WFPLUG_MENU \
	BR2_PACKAGE_WFPLUG_CLOCK \
	BR2_PACKAGE_WFPLUG_NETMAN \
	BR2_PACKAGE_WFPLUG_POWER \
	BR2_PACKAGE_WFPLUG_VOLUMEPULSE \
	BR2_PACKAGE_WPA_SUPPLICANT_DBUS \
	BR2_PACKAGE_FOOT \
	BR2_PACKAGE_WVKBD \
	BR2_PACKAGE_TDVP_LABWC_DESKTOP \
	BR2_PACKAGE_KMOD_TOOLS \
	BR2_PACKAGE_VICLIU_POCKET_LINUX_HARDWARE \
	BR2_PACKAGE_TDVP_DISPLAY_SMOKE \
	BR2_PACKAGE_TDVP_KEYBOARD_LAYOUT \
	BR2_PACKAGE_TDVP_WAYLAND_ACCEPTANCE; do
	require_buildroot_selection "$symbol"
done
for symbol in BR2_PACKAGE_SFWBAR BR2_PACKAGE_SWAYBG; do
	reject_buildroot_selection "$symbol"
done

kernel_config="$(find "$OUTPUT_DIR/build" -maxdepth 2 -path '*/.config' -path '*linux*' -print -quit)"
[ -n "$kernel_config" ] || {
	printf 'TDVP K230 preflight: effective Linux configuration is missing\n' >&2
	exit 1
}

for option in CONFIG_K230_GNNE_DRIVER CONFIG_K230_AI2D_DRIVER; do
	if grep -Eq "^${option}=[ym]$" "$kernel_config"; then
		printf 'TDVP K230 preflight: competing Linux driver %s remains in %s\n' "$option" "$kernel_config" >&2
		exit 1
	fi
done

require_file "$TARGET_DIR/usr/local/bin/vpl-hwctl"
[ -x "$TARGET_DIR/usr/local/bin/vpl-hwctl" ] || {
	printf '%s\n' 'TDVP K230 preflight: vpl-hwctl is not executable' >&2
	exit 1
}
require_file "$TARGET_DIR/usr/bin/wvkbd-mobintl"
[ -x "$TARGET_DIR/usr/bin/wvkbd-mobintl" ] || {
	printf '%s\n' 'TDVP K230 preflight: wvkbd-mobintl is not executable' >&2
	exit 1
}
file "$TARGET_DIR/usr/bin/wvkbd-mobintl" | grep -Fq 'RISC-V' || {
	printf '%s\n' 'TDVP K230 preflight: wvkbd-mobintl is not a RISC-V target binary' >&2
	exit 1
}

for desktop_path in \
	/usr/bin/wf-panel-pi \
	/usr/bin/pcmanfm \
	/usr/local/bin/tdvp-wf-panel-session \
	/usr/local/bin/tdvp-pcmanfm-desktop-session \
	/etc/xdg/wf-panel-pi/wf-panel-pi.ini \
	/etc/xdg/pcmanfm/default/pcmanfm.conf; do
	require_file "$TARGET_DIR$desktop_path"
done
grep -Fqx 'wallpaper=/usr/share/backgrounds/tdvp-pda-paper.png' \
	"$TARGET_DIR/etc/xdg/pcmanfm/default/pcmanfm.conf" || {
	printf '%s\n' 'TDVP K230 preflight: PCManFM does not retain the TDVP wallpaper profile' >&2
	exit 1
}
grep -Fqx 'show_wm_menu=0' \
	"$TARGET_DIR/etc/xdg/pcmanfm/default/pcmanfm.conf" || {
	printf '%s\n' 'TDVP K230 preflight: PCManFM must own blank-desktop context menus' >&2
	exit 1
}
for retired_path in /usr/bin/sfwbar /usr/bin/swaybg /usr/local/bin/tdvp-sfwbar-session; do
	if [ -e "$TARGET_DIR$retired_path" ]; then
		printf 'TDVP K230 preflight: retired desktop artifact is still installed: %s\n' "$retired_path" >&2
		exit 1
	fi
done

dtb="$IMAGE_DIR/k230-canmv-rm69a10.dtb"
[ -f "$dtb" ] || {
	printf 'TDVP K230 preflight: RM69A10 board DTB is missing: %s\n' "$dtb" >&2
	exit 1
}
bash "$(dirname "$0")/../k230-sdk-overlay/board/tdvp/cpu1/vision/verify-pair.sh" \
	"$dtb" "$OUTPUT_DIR/host/bin/fdtget" "$IMAGE_DIR/tdvp-cpu1-rtsmart.manifest"

{
	printf 'TDVP K230 hardware build preflight\n'
	printf 'product_profile=systemd-seatd-labwc-standard-desktop\n'
	printf 'kernel_config=%s\n' "$kernel_config"
	printf 'dtb=%s\n' "$dtb"
	printf 'ai_owner=cpu1-rtsmart\n'
	printf 'camera_owner=cpu1-rtsmart\n'
	printf 'linux_bridge=/dev/tdvp-vision\n'
	printf 'hardware_status_tool=/usr/local/bin/vpl-hwctl\n'
	printf 'onscreen_keyboard=/usr/bin/wvkbd-mobintl\n'
	printf 'panel=wf-panel-pi\n'
	printf 'background=pcmanfm\n'
	printf 'runtime_acceptance=requires_paired_board_frames_and_CPU1_model_execution\n'
} > "$REPORT"

printf 'TDVP K230 preflight passed: %s\n' "$REPORT"

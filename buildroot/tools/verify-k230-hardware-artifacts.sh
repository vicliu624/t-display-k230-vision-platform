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
	BR2_PACKAGE_TDVP_KPU_ACCEPTANCE \
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
	grep -Fqx "${option}=y" "$kernel_config" || {
		printf 'TDVP K230 preflight: %s is not enabled in %s\n' "$option" "$kernel_config" >&2
		exit 1
	}
done

for asset in ai2d_kpu.elf test.kmodel ai2d_input.bin input.bin result.bin; do
	require_file "$TARGET_DIR/root/app/ai2d_kpu/$asset"
done
[ -x "$TARGET_DIR/root/app/ai2d_kpu/ai2d_kpu.elf" ] || {
	printf 'TDVP K230 preflight: ai2d_kpu.elf is not executable\n' >&2
	exit 1
}

require_file "$TARGET_DIR/usr/local/bin/tdvp-kpu-smoke"
[ -x "$TARGET_DIR/usr/local/bin/tdvp-kpu-smoke" ] || {
	printf '%s\n' 'TDVP K230 preflight: tdvp-kpu-smoke is not executable' >&2
	exit 1
}
require_file "$TARGET_DIR/usr/local/bin/vpl-hwctl"
[ -x "$TARGET_DIR/usr/local/bin/vpl-hwctl" ] || {
	printf '%s\n' 'TDVP K230 preflight: vpl-hwctl is not executable' >&2
	exit 1
}
require_file "$TARGET_DIR/usr/lib/systemd/system/tdvp-kpu-acceptance.service"
[ -L "$TARGET_DIR/etc/systemd/system/multi-user.target.wants/tdvp-kpu-acceptance.service" ] || {
	printf '%s\n' 'TDVP K230 preflight: KPU acceptance service is not enabled' >&2
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
for compatible in k230-gnne k230-ai2d; do
	strings "$dtb" | grep -Fxq "$compatible" || {
		printf 'TDVP K230 preflight: %s is missing from %s\n' "$compatible" "$dtb" >&2
		exit 1
	}
done

{
	printf 'TDVP K230 hardware build preflight\n'
	printf 'product_profile=systemd-seatd-labwc-standard-desktop\n'
	printf 'kernel_config=%s\n' "$kernel_config"
	printf 'dtb=%s\n' "$dtb"
	printf 'CONFIG_K230_GNNE_DRIVER=y\n'
	printf 'CONFIG_K230_AI2D_DRIVER=y\n'
	for asset in ai2d_kpu.elf test.kmodel ai2d_input.bin input.bin result.bin; do
		printf 'asset_sha256[%s]=' "$asset"
		sha256sum "$TARGET_DIR/root/app/ai2d_kpu/$asset" | awk '{print $1}'
	done
	printf 'kpu_smoke=/usr/local/bin/tdvp-kpu-smoke\n'
	printf 'kpu_service=tdvp-kpu-acceptance.service\n'
	printf 'hardware_status_tool=/usr/local/bin/vpl-hwctl\n'
	printf 'onscreen_keyboard=/usr/bin/wvkbd-mobintl\n'
	printf 'panel=wf-panel-pi\n'
	printf 'background=pcmanfm\n'
	printf 'runtime_acceptance=requires_target_device_nodes_and_kmodel_execution\n'
} > "$REPORT"

printf 'TDVP K230 preflight passed: %s\n' "$REPORT"

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

require_executable() {
	[ -f "$1" ] && [ -x "$1" ] || fail "missing executable bit: $1"
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
	swaylock
	tdvp-quick-settings
	libfm-extra
	libmenu-cache
	libfm
	pcmanfm
	libnma
	nm-connection-editor
	wf-panel-pi
	wfplug-batt
	wfplug-menu
	wfplug-clock
	wfplug-netman
	wfplug-power
	wfplug-volumepulse
	tdvp-greetd
	tdvp-gtkgreet
	tdvp-greeter
	tdvp-opkg-trust
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
	BR2_PACKAGE_SWAYLOCK
	BR2_PACKAGE_TDVP_QUICK_SETTINGS
	BR2_PACKAGE_GTKMM3
	BR2_PACKAGE_LIBFM_EXTRA
	BR2_PACKAGE_LIBMENU_CACHE
	BR2_PACKAGE_LIBFM
	BR2_PACKAGE_PCMANFM
	BR2_PACKAGE_OPKG
	BR2_PACKAGE_OPKG_GPG_SIGN
	BR2_PACKAGE_GNUPG2
	BR2_PACKAGE_GNUPG2_GPGV
	BR2_PACKAGE_TDVP_OPKG_TRUST
	BR2_PACKAGE_GLIB_NETWORKING
	BR2_PACKAGE_NETWORK_MANAGER
	BR2_PACKAGE_NETWORK_MANAGER_CLI
	BR2_PACKAGE_NM_CONNECTION_EDITOR
	BR2_PACKAGE_WIRELESS_REGDB
	BR2_PACKAGE_PROCPS_NG
	BR2_PACKAGE_UTIL_LINUX
	BR2_PACKAGE_UTIL_LINUX_BINARIES
	BR2_PACKAGE_UTIL_LINUX_RFKILL
	BR2_PACKAGE_GPTFDISK
	BR2_PACKAGE_GPTFDISK_SGDISK
	BR2_PACKAGE_E2FSPROGS
	BR2_PACKAGE_E2FSPROGS_RESIZE2FS
	BR2_PACKAGE_LIBSECRET
	BR2_PACKAGE_LIBNMA
	BR2_PACKAGE_PULSEAUDIO
	BR2_PACKAGE_PULSEAUDIO_DAEMON
	BR2_PACKAGE_ALSA_UTILS_SPEAKER_TEST
	BR2_PACKAGE_LIBCANBERRA
	BR2_PACKAGE_SOUND_THEME_FREEDESKTOP
	BR2_PACKAGE_WF_PANEL_PI
	BR2_PACKAGE_WFPLUG_BATT
	BR2_PACKAGE_WFPLUG_MENU
	BR2_PACKAGE_WFPLUG_CLOCK
	BR2_PACKAGE_WFPLUG_NETMAN
	BR2_PACKAGE_WFPLUG_POWER
	BR2_PACKAGE_WFPLUG_VOLUMEPULSE
	BR2_PACKAGE_WPA_SUPPLICANT_DBUS
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
require_line "${PROJECT_DIR}/buildroot/k230-sdk-overlay/configs/${PROFILE}" 'BR2_JLEVEL=4'
for hook in post-build.sh post-fakeroot.sh post-image.sh; do
	require_executable "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/${hook}"
done

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
LABWC_LONG_PRESS_PATCH="${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/labwc/0001-tdvp-touch-long-press-emulates-right-click.patch"
LABWC_MOUSE_EMULATION_POINTER_PATCH="${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/labwc/0002-tdvp-advertise-pointer-for-mouse-emulated-touch.patch"
LIBGTK3_WAYLAND_TRANSFORM_PATCH="${PROJECT_DIR}/buildroot/patches/buildroot/0006-libgtk3-wayland-honor-output-transform.patch"
NM_CONNECTION_EDITOR_WAYLAND_PATCH="${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/nm-connection-editor/0002-tdvp-wayland-drop-unused-gdkx-header.patch"
EXTERNAL_I2S_DTS_PATCH="${PROJECT_DIR}/buildroot/k230-sdk-overlay/linux/0051-tdvp-riscv-dts-canaan-add-external-i2s-amp.patch"
EXTERNAL_I2S_ASOC_PATCH="${PROJECT_DIR}/buildroot/k230-sdk-overlay/linux/0052-tdvp-asoc-canaan-add-external-i2s-output-switch.patch"
HARDWARE_SOURCE="${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src"
require_file "${EXTERNAL_I2S_DTS_PATCH}"
require_content "${EXTERNAL_I2S_DTS_PATCH}" 'canaan,external-i2s-output-default;'
require_content "${EXTERNAL_I2S_DTS_PATCH}" 'amp-shutdown-gpios = <&gpio1_ports 2 GPIO_ACTIVE_HIGH>;'
require_content "${EXTERNAL_I2S_DTS_PATCH}" 'function = K230_IO32_IIS_CLK;'
require_content "${EXTERNAL_I2S_DTS_PATCH}" 'function = K230_IO33_IIS_WS;'
require_content "${EXTERNAL_I2S_DTS_PATCH}" 'function = K230_IO35_IIS_D_OUT0_PDM_IN1;'
require_content "${EXTERNAL_I2S_DTS_PATCH}" 'function = K230_IO34_GPIO34;'
require_file "${EXTERNAL_I2S_ASOC_PATCH}"
require_content "${EXTERNAL_I2S_ASOC_PATCH}" 'External I2S Output Switch'
require_content "${EXTERNAL_I2S_ASOC_PATCH}" 'devm_gpiod_get_optional'
require_content "${EXTERNAL_I2S_ASOC_PATCH}" 'audio_i2s_enable_audio_codec(!priv->external_i2s_output);'
require_content "${EXTERNAL_I2S_ASOC_PATCH}" 'gpiod_set_value_cansleep'
require_file "${HARDWARE_SOURCE}/tdvp-audio-route"
require_file "${HARDWARE_SOURCE}/tdvp-speaker-acceptance"
require_content "${HARDWARE_SOURCE}/tdvp-audio-route" "readonly CONTROL='External I2S Output Switch'"
require_content "${HARDWARE_SOURCE}/tdvp-audio-route" 'never drives'
require_content "${HARDWARE_SOURCE}/tdvp-speaker-acceptance" 'confirm-audible'
require_content "${HARDWARE_SOURCE}/tdvp-expand-rootfs" 'readonly SFDISK=/usr/sbin/sfdisk'
require_content "${HARDWARE_SOURCE}/tdvp-expand-rootfs" 'PARTUUID=*'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-hardware/vicliu-pocket-linux-hardware.mk" 'tdvp-rootfs-expand.service'
if grep -Fq 'tdvp-external-audio.service' "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-hardware/vicliu-pocket-linux-hardware.mk"; then
	fail 'external audio route service must not be enabled at boot'
fi
require_content "${HARDWARE_SOURCE}/hardware/status.cpp" 'speaker_amplifier_owner'
require_content "${HARDWARE_SOURCE}/hardware/status.cpp" 'pcm_playback_state'
require_content "${HARDWARE_SOURCE}/hardware/quick_settings_service.cpp" 'speaker-volume'
require_content "${HARDWARE_SOURCE}/hardware/quick_settings_service.cpp" 'speaker-mute'
require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/swaylock/src/swaylock"
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/swaylock/swaylock.mk" '-Dpam=enabled'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/swaylock/src/swaylock" 'auth       required   pam_unix.so'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-quick-settings/tdvp-quick-settings.mk" 'TDVP_QUICK_SETTINGS_VERSION = 0.1.10'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-quick-settings/tdvp-quick-settings.mk" 'vicliu624,tdvp-quick-settings'
require_content "${DESKTOP_SOURCE}/tdvp-gdk-committed-compat.c" 'g_signal_handler_disconnect'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-labwc-desktop/tdvp-labwc-desktop.mk" '-lglib-2.0'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-labwc-desktop/tdvp-labwc-desktop.mk" '/usr/lib/tdvp-gdk-committed-compat.so'
require_content "${DESKTOP_SOURCE}/environment" 'WLR_DRM_DEVICES=/dev/dri/card0'
require_content "${DESKTOP_SOURCE}/environment" 'LIBSEAT_BACKEND=seatd'
require_content "${DESKTOP_SOURCE}/environment" 'TDVP_K230_OUTPUT=DSI-1'
require_content "${DESKTOP_SOURCE}/environment" 'TDVP_K230_OUTPUT_TRANSFORM=90'
require_content "${DESKTOP_SOURCE}/tdvp-labwc-session" 'set -a'
require_content "${DESKTOP_SOURCE}/tdvp-labwc-session" 'XDG_CACHE_HOME="${XDG_CACHE_HOME:-${HOME}/.cache}"'
require_content "${DESKTOP_SOURCE}/tdvp-labwc-session" 'XDG_DATA_HOME="${XDG_DATA_HOME:-${HOME}/.local/share}"'
require_content "${DESKTOP_SOURCE}/tdvp-labwc-session" 'exec /usr/bin/dbus-run-session -- /usr/bin/labwc'
require_file "${DESKTOP_SOURCE}/tdvp-wf-panel-session"
require_file "${DESKTOP_SOURCE}/tdvp-pcmanfm-desktop-session"
require_file "${DESKTOP_SOURCE}/tdvp-key-bridge.c"
require_file "${DESKTOP_SOURCE}/menus/lxde-applications.menu"
require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-keyboard-layout/src/tdvp-fn-yellow.xkb"
require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-keyboard-layout/src/tdvp-us-xkb-variant.xkb"
require_content "${DESKTOP_SOURCE}/tdvp-wf-panel-session" 'export GDK_BACKEND=wayland'
require_content "${DESKTOP_SOURCE}/tdvp-wf-panel-session" 'LD_PRELOAD="/usr/lib/tdvp-gdk-committed-compat.so'
require_content "${DESKTOP_SOURCE}/tdvp-wf-panel-session" 'PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin'
require_content "${DESKTOP_SOURCE}/tdvp-wf-panel-session" '/usr/bin/wf-panel-pi --config /etc/xdg/wf-panel-pi/wf-panel-pi.ini'
require_content "${DESKTOP_SOURCE}/tdvp-pcmanfm-desktop-session" 'export GDK_BACKEND=wayland'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig" 'BR2_PACKAGE_UTF8PROC=y'
require_content "${DESKTOP_SOURCE}/tdvp-pcmanfm-desktop-session" '/usr/bin/pcmanfm --desktop'
if grep -Eq -- '(^|[[:space:]])--one-screen([[:space:]]|$)' "${DESKTOP_SOURCE}/tdvp-pcmanfm-desktop-session"; then
	fail 'PCManFM desktop mode must not disable monitor 0 with --one-screen'
fi
require_content "${DESKTOP_SOURCE}/autostart" '/usr/local/bin/tdvp-pcmanfm-desktop-session &'
require_content "${DESKTOP_SOURCE}/autostart" '/usr/local/bin/tdvp-wf-panel-session &'
require_content "${DESKTOP_SOURCE}/autostart" '/usr/local/bin/tdvp-key-bridge &'
require_content "${DESKTOP_SOURCE}/autostart" '/usr/bin/tdvp-quick-settings &'
require_content "${DESKTOP_SOURCE}/tdvp-key-bridge.c" 'event.code == KEY_MENU && event.value == 1'
require_content "${DESKTOP_SOURCE}/tdvp-key-bridge.c" 'TDVP_KEYBOARD_NAME "tca8418"'
require_content "${DESKTOP_SOURCE}/tdvp-key-bridge.c" 'MENU_DEBOUNCE_MS 450'
require_content "${DESKTOP_SOURCE}/tdvp-key-bridge.c" 'toggle_panel_menu(void)'
require_content "${DESKTOP_SOURCE}/tdvp-key-bridge.c" 'execl("/usr/local/bin/tdvp-panel-menu", "tdvp-panel-menu", (char *)NULL);'
require_content "${DESKTOP_SOURCE}/wf-panel-pi.ini" 'widgets_left=smenu spacing8 window-list'
require_content "${DESKTOP_SOURCE}/wf-panel-pi.ini" 'widgets_right=netman spacing4 volumepulse spacing4 batt spacing4 clock'
require_content "${DESKTOP_SOURCE}/wf-panel-pi.ini" 'minimal_height=64'
require_content "${DESKTOP_SOURCE}/tdvp-wf-panel.css" 'box-shadow: inset 0 -6px #e66a2c;'
require_content "${DESKTOP_SOURCE}/tdvp-wf-panel.css" '#PanelToplevel #volumepulse,'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-netman/wfplug-netman.mk" 'https://github.com/raspberrypi-ui/pplug-netman.git'
require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-netman/0004-tdvp-select-the-wf-panel-code-path.patch"
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-netman/0004-tdvp-select-the-wf-panel-code-path.patch" '+#define LXPANEL_PLUGIN'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'lp-connection-editor'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'exec /usr/bin/nm-connection-editor'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/nm-connection-editor/nm-connection-editor.mk" 'network-manager-applet.git'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/nm-connection-editor/0001-editor-only-no-nm-applet-autostart.patch" 'nm-connection-editor'
require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/nm-connection-editor/src/org.gnome.nm-applet.gschema.xml"
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/nm-connection-editor/src/org.gnome.nm-applet.gschema.xml" 'id="org.gnome.nm-applet"'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-volumepulse/wfplug-volumepulse.mk" 'https://github.com/raspberrypi-ui/pplug-volumepulse.git'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-volumepulse/wfplug-volumepulse.mk" 'libcanberra'
require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-volumepulse/0003-tdvp-use-standard-libcanberra-volume-feedback.patch"
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-volumepulse/0003-tdvp-use-standard-libcanberra-volume-feedback.patch" 'audio-volume-change'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-batt/wfplug-batt.mk" 'https://github.com/raspberrypi-ui/pplug-batt.git'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/wfplug-power/wfplug-power.mk" 'https://github.com/raspberrypi-ui/pplug-power.git'
require_content "${DESKTOP_SOURCE}/pcmanfm.conf" 'wallpaper=/usr/share/backgrounds/tdvp-pda-paper.png'
require_content "${DESKTOP_SOURCE}/pcmanfm.conf" 'show_wm_menu=0'
require_content "${DESKTOP_SOURCE}/menus/lxde-applications.menu" '<Filename>tdvp-pcmanfm.desktop</Filename>'
require_content "${DESKTOP_SOURCE}/menus/lxde-applications.menu" '<Filename>foot.desktop</Filename>'
require_content "${DESKTOP_SOURCE}/menus/lxde-applications.menu" '<Filename>vpl-package-manager.desktop</Filename>'
require_content "${DESKTOP_SOURCE}/menus/lxde-applications.menu" '<Menuname>Accessories</Menuname>'
require_content "${DESKTOP_SOURCE}/menus/lxde-applications.menu" '<Menuname>Sound &amp; Video</Menuname>'
require_content "${DESKTOP_SOURCE}/menus/lxde-applications.menu" '<Name>Games</Name>'
require_content "${DESKTOP_SOURCE}/menus/lxde-applications.menu" '<Category>Game</Category>'
require_content "${DESKTOP_SOURCE}/menus/lxde-applications.menu" '<Menuname>Games</Menuname>'
if grep -Fq 'vpl-audio.desktop' "${DESKTOP_SOURCE}/menus/lxde-applications.menu" || \
	grep -Fq 'vpl-wifi.desktop' "${DESKTOP_SOURCE}/menus/lxde-applications.menu"; then
	fail 'XDG menu must not expose retired custom audio or Wi-Fi dialogs'
fi
require_content "${DESKTOP_SOURCE}/rc.xml" '<default />'
require_content "${DESKTOP_SOURCE}/tdvp-panel-menu" 'exec /bin/wfpanelctl smenu menu'
require_content "${DESKTOP_SOURCE}/tdvp-panel-menu" 'DBUS_SESSION_BUS_ADDRESS%%,guid=*'
require_content "${DESKTOP_SOURCE}/rc.xml" '<mouseEmulation>yes</mouseEmulation>'
require_content "${DESKTOP_SOURCE}/rc.xml" '<layout>icon:iconify,max,close</layout>'
require_content "${DESKTOP_SOURCE}/environment" 'XKB_DEFAULT_VARIANT=tdvp'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-keyboard-layout/src/tdvp-fn-yellow.xkb" 'key <I472>'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-keyboard-layout/src/tdvp-fn-yellow.xkb" 'ISO_Level3_Shift'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-keyboard-layout/src/tdvp-fn-yellow.xkb" 'modifier_map Mod5 { <I472> };'
require_file "${LABWC_LONG_PRESS_PATCH}"
require_content "${LABWC_LONG_PRESS_PATCH}" 'TOUCH_LONG_PRESS_TIMEOUT_MS 650'
require_content "${LABWC_LONG_PRESS_PATCH}" 'TOUCH_LONG_PRESS_SLOP_PX 18.0'
require_content "${LABWC_LONG_PRESS_PATCH}" 'cursor_emulate_button(touch_point->seat, BTN_RIGHT,'
require_file "${LABWC_MOUSE_EMULATION_POINTER_PATCH}"
require_content "${LABWC_MOUSE_EMULATION_POINTER_PATCH}" 'touch_uses_mouse_emulation(const struct input *input)'
require_content "${LABWC_MOUSE_EMULATION_POINTER_PATCH}" 'caps |= WL_SEAT_CAPABILITY_POINTER;'
require_file "${LIBGTK3_WAYLAND_TRANSFORM_PATCH}"
require_content "${LIBGTK3_WAYLAND_TRANSFORM_PATCH}" 'monitor->output_transform = transform;'
require_content "${LIBGTK3_WAYLAND_TRANSFORM_PATCH}" 'case WL_OUTPUT_TRANSFORM_FLIPPED_270:'
require_content "${LIBGTK3_WAYLAND_TRANSFORM_PATCH}" 'monitor->output_geometry.width = height;'
require_file "${NM_CONNECTION_EDITOR_WAYLAND_PATCH}"
require_content "${NM_CONNECTION_EDITOR_WAYLAND_PATCH}" 'gdk/gdkx.h'
require_content "${NM_CONNECTION_EDITOR_WAYLAND_PATCH}" 'Drop the unused include'
if grep -Fq '<mapToOutput>' "${DESKTOP_SOURCE}/rc.xml"; then
	fail 'desktop Labwc touch configuration must not remap the output'
fi
[ ! -e "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher" ] || fail 'obsolete vpl-app-launcher wrapper must not be present'
[ ! -e "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-audio-menu" ] || fail 'custom audio Wofi dialog must not be present'
[ ! -e "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-wifi" ] || fail 'custom Wi-Fi Wofi dialog must not be present'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/libexec/vpl-desktopctl" '/usr/bin/nmcli'
if grep -Eq '/etc/wpa_supplicant|systemd-networkd|wifi-connect|wifi-scan' "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/libexec/vpl-desktopctl"; then
	fail 'desktop utility must not retain direct legacy Wi-Fi ownership'
fi
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/wofi/config" 'location=top_left'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/wofi/config" 'width=440'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/vicliu-pocket-linux-desktop.mk" '$(TARGET_DIR)/etc/xdg/wofi/config'
require_file "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/tdvp-expand-rootfs"
require_file "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/tdvp-rootfs-expand.service"
require_content "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/tdvp-expand-rootfs" 'PARTUUID'
require_content "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/tdvp-expand-rootfs" 'rootfs-expand.pending'
require_content "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/tdvp-rootfs-expand.service" 'Before=greetd.service'
require_content "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/hardware/network.cpp" 'NetworkManager.service'
[ ! -e "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/tdvp-provision-data" ] || fail 'obsolete /data provisioner must not be present'
[ ! -e "${PROJECT_DIR}/user-space/vicliu-pocket-linux-hardware/src/tdvp-data-storage.service" ] || fail 'obsolete /data service must not be present'
	require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-package-manager"
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-package-manager" 'exec /usr/local/bin/tdvp-terminal'
	require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-opkg-console"
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-opkg-console" 'TDVP Software Manager (opkg)'
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-opkg-console" 'sudo tdvp-opkg update'
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/sudoers/vpl-desktop" '/usr/local/sbin/tdvp-opkg'
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/vicliu-pocket-linux-desktop/src/sudoers/vpl-desktop" 'secure_path=/usr/local/sbin:/usr/local/bin:'
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'option status_file /var/lib/opkg/status'
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'tdvp-platform-abi.list'
	require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-opkg-trust/src/tdvp-repo-public.asc"
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-opkg-trust/src/tdvp-opkg-bootstrap" '2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500'
	require_file "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-opkg-trust/src/tdvp-opkg"
	require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-opkg-trust/src/tdvp-opkg" '/usr/local/libexec/tdvp-opkg-bootstrap'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'option check_signature 1'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'option signature_type gpg-asc'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'option gpg_dir /etc/opkg/gpg'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'option gpg_trust_level TrustAny'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'src/gz tdvp_apps_r6 https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/r6/riscv64'
require_file "${PROJECT_DIR}/buildroot/tools/assert-tdvp-opkg-feed-release.sh"
require_content "${PROJECT_DIR}/buildroot/tools/assert-tdvp-opkg-feed-release.sh" "download 'Packages.asc' 'Packages.asc'"
require_content "${PROJECT_DIR}/buildroot/tools/assert-tdvp-opkg-feed-release.sh" 'gpgv --keyring'
require_content "${PROJECT_DIR}/buildroot/tools/assert-tdvp-opkg-feed-release.sh" 'tdvp-platform-abi (= '
require_file "${PROJECT_DIR}/buildroot/tools/collect-release-bundle.sh"
require_content "${PROJECT_DIR}/buildroot/tools/collect-release-bundle.sh" 'sysimage-sdcard.img.gz'
require_content "${PROJECT_DIR}/buildroot/tools/collect-release-bundle.sh" 'destination already exists'
if grep -Fq 'sysimage-sdcard.img" "${RELEASE_DIR}' \
	"${PROJECT_DIR}/buildroot/tools/collect-release-bundle.sh"; then
	fail 'release collector must not copy an uncompressed SD image into Windows output'
fi
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-fakeroot.sh" 'tdvp-rootfs-expand.service'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'network-wireless-connected-100=network-wireless-signal-excellent-symbolic'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'nm-signal-100=network-wireless-signal-excellent-symbolic'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'nm-device-wired=network-transmit-receive-symbolic'
require_content "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/post-build.sh" 'Package: tdvp-platform-abi'
require_file "${DESKTOP_SOURCE}/foot.ini"
require_content "${DESKTOP_SOURCE}/foot.ini" 'initial-window-mode=windowed'
require_content "${DESKTOP_SOURCE}/foot.ini" 'initial-window-size-pixels=900x460'
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
	for hook in post-build.sh post-fakeroot.sh post-image.sh; do
		require_executable "${STAGED_OVERLAY}/board/tdvp/${hook}"
	done
	for package in "${packages[@]}"; do
		require_file "${STAGED_OVERLAY}/package/${package}/Config.in"
		require_file "${STAGED_OVERLAY}/package/${package}/${package}.mk"
	done
	require_file "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-wf-panel-session"
	require_file "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-pcmanfm-desktop-session"
	require_file "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-key-bridge.c"
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-labwc-session" 'XDG_CACHE_HOME="${XDG_CACHE_HOME:-${HOME}/.cache}"'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-labwc-session" 'XDG_DATA_HOME="${XDG_DATA_HOME:-${HOME}/.local/share}"'
	require_file "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu"
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-wf-panel-session" '/usr/bin/wf-panel-pi --config /etc/xdg/wf-panel-pi/wf-panel-pi.ini'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-pcmanfm-desktop-session" '/usr/bin/pcmanfm --desktop'
	if grep -Eq -- '(^|[[:space:]])--one-screen([[:space:]]|$)' "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-pcmanfm-desktop-session"; then
		fail 'staged PCManFM desktop mode must not disable monitor 0 with --one-screen'
	fi
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/autostart" '/usr/local/bin/tdvp-wf-panel-session &'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/autostart" '/usr/local/bin/tdvp-pcmanfm-desktop-session &'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/autostart" '/usr/local/bin/tdvp-key-bridge &'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/autostart" '/usr/bin/tdvp-quick-settings &'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-key-bridge.c" 'event.code == KEY_MENU && event.value == 1'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-key-bridge.c" 'MENU_DEBOUNCE_MS 450'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-key-bridge.c" 'toggle_panel_menu(void)'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-key-bridge.c" 'execl("/usr/local/bin/tdvp-panel-menu", "tdvp-panel-menu", (char *)NULL);'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/wf-panel-pi.ini" 'widgets_left=smenu spacing8 window-list'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/wf-panel-pi.ini" 'widgets_right=netman spacing4 volumepulse spacing4 batt spacing4 clock'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu" '<Filename>tdvp-pcmanfm.desktop</Filename>'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu" '<Filename>foot.desktop</Filename>'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu" '<Filename>vpl-package-manager.desktop</Filename>'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu" '<Menuname>Accessories</Menuname>'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu" '<Name>Games</Name>'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu" '<Category>Game</Category>'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu" '<Menuname>Games</Menuname>'
	if grep -Fq 'vpl-audio.desktop' "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu" || \
		grep -Fq 'vpl-wifi.desktop' "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/menus/lxde-applications.menu"; then
		fail 'staged XDG menu must not expose retired custom audio or Wi-Fi dialogs'
	fi
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/pcmanfm.conf" 'show_wm_menu=0'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-panel-menu" 'exec /bin/wfpanelctl smenu menu'
	require_file "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/tdvp-terminal"
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/foot.desktop" 'Exec=/usr/local/bin/tdvp-terminal'
	require_content "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/rc.xml" '<layout>icon:iconify,max,close</layout>'
	require_file "${STAGED_OVERLAY}/package/labwc/0001-tdvp-touch-long-press-emulates-right-click.patch"
	require_content "${STAGED_OVERLAY}/package/labwc/0001-tdvp-touch-long-press-emulates-right-click.patch" 'TOUCH_LONG_PRESS_TIMEOUT_MS 650'
	require_content "${STAGED_OVERLAY}/package/labwc/0001-tdvp-touch-long-press-emulates-right-click.patch" 'cursor_emulate_button(touch_point->seat, BTN_RIGHT,'
	require_file "${STAGED_OVERLAY}/package/labwc/0002-tdvp-advertise-pointer-for-mouse-emulated-touch.patch"
	require_content "${STAGED_OVERLAY}/package/labwc/0002-tdvp-advertise-pointer-for-mouse-emulated-touch.patch" 'caps |= WL_SEAT_CAPABILITY_POINTER;'
	if grep -Fq '<mapToOutput>' "${STAGED_OVERLAY}/package/tdvp-labwc-desktop/src/rc.xml"; then
		fail 'staged desktop Labwc touch configuration must not remap the output'
	fi
	[ ! -e "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-app-launcher" ] || fail 'staged obsolete vpl-app-launcher wrapper must not be present'
	[ ! -e "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-audio-menu" ] || fail 'staged custom audio Wofi dialog must not be present'
	[ ! -e "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-wifi" ] || fail 'staged custom Wi-Fi Wofi dialog must not be present'
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/libexec/vpl-desktopctl" '/usr/bin/nmcli'
	if grep -Eq '/etc/wpa_supplicant|systemd-networkd|wifi-connect|wifi-scan' "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/libexec/vpl-desktopctl"; then
		fail 'staged desktop utility must not retain direct legacy Wi-Fi ownership'
	fi
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/wofi/config" 'location=top_left'
require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/wofi/config" 'width=440'
require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/vicliu-pocket-linux-desktop.mk" '$(TARGET_DIR)/etc/xdg/wofi/config'
	require_file "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-package-manager"
	require_file "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-opkg-console"
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-desktop/src/bin/vpl-opkg-console" 'sudo tdvp-opkg update'
	require_file "${STAGED_OVERLAY}/package/wfplug-volumepulse/0003-tdvp-use-standard-libcanberra-volume-feedback.patch"
	require_content "${STAGED_OVERLAY}/package/wfplug-volumepulse/0003-tdvp-use-standard-libcanberra-volume-feedback.patch" 'audio-volume-change'
	require_file "${STAGED_OVERLAY}/package/nm-connection-editor/0002-tdvp-wayland-drop-unused-gdkx-header.patch"
	require_content "${STAGED_OVERLAY}/package/nm-connection-editor/0002-tdvp-wayland-drop-unused-gdkx-header.patch" 'Drop the unused include'
	require_file "${STAGED_OVERLAY}/linux/0051-tdvp-riscv-dts-canaan-add-external-i2s-amp.patch"
	require_content "${STAGED_OVERLAY}/linux/0051-tdvp-riscv-dts-canaan-add-external-i2s-amp.patch" 'K230_IO35_IIS_D_OUT0_PDM_IN1'
	require_file "${STAGED_OVERLAY}/linux/0052-tdvp-asoc-canaan-add-external-i2s-output-switch.patch"
	require_content "${STAGED_OVERLAY}/linux/0052-tdvp-asoc-canaan-add-external-i2s-output-switch.patch" 'External I2S Output Switch'
	require_file "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/tdvp-expand-rootfs"
	require_file "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/tdvp-rootfs-expand.service"
	require_file "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/tdvp-audio-route"
	require_file "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/tdvp-speaker-acceptance"
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/tdvp-audio-route" 'amp-shutdown-gpios'
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/hardware/status.cpp" 'speaker_amplifier_owner'
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/tdvp-expand-rootfs" 'PARTUUID=*'
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/tdvp-expand-rootfs" 'readonly SFDISK=/usr/sbin/sfdisk'
	require_content "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/hardware/network.cpp" 'NetworkManager.service'
	[ ! -e "${STAGED_OVERLAY}/package/vicliu-pocket-linux-hardware/src/tdvp-provision-data" ] || fail 'staged obsolete /data provisioner must not be present'
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
require_line "${CONFIG}" 'BR2_JLEVEL=4'
bash "${PROJECT_DIR}/buildroot/k230-sdk-overlay/board/tdvp/verify-sdcard-image.sh" "${IMAGES}"
require_content "${IMAGES}/tdvp-image-manifest" "profile=${PROFILE}"
require_content "${IMAGES}/tdvp-image-manifest" 'desktop=labwc'
require_content "${IMAGES}/tdvp-image-manifest" 'panel=wf-panel-pi'
require_content "${IMAGES}/tdvp-image-manifest" 'background=pcmanfm'
require_content "${IMAGES}/tdvp-image-manifest" 'terminal=foot'
require_content "${IMAGES}/tdvp-image-manifest" 'display_manager=greetd'
require_content "${IMAGES}/tdvp-image-manifest" 'greeter=gtkgreet'
require_content "${IMAGES}/tdvp-image-manifest" 'session=tdvp-labwc-session'
printf '%s\n' 'TDVP desktop assertion: PASS'

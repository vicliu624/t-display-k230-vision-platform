#!/usr/bin/env bash
set -euo pipefail

# The SDK overlay is commonly checked out on Windows. Normalize only final
# runtime text configuration after the vendor post-build step, leaving binary
# firmware and all non-runtime build inputs untouched.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_POST_BUILD="${SCRIPT_DIR}/../canaan/k230-soc/post-build.sh"

"${SDK_POST_BUILD}" "$@"

: "${TARGET_DIR:?Buildroot did not provide TARGET_DIR}"

# PCManFM installs its upstream default desktop profile during its package
# target-install phase.  A preserved/incremental package output can rerun that
# phase without rerunning the TDVP desktop package, which would silently
# replace the product wallpaper and desktop-context-menu policy.  Reinstall
# the product-owned profile at the rootfs boundary, after every package has
# finished, so the resulting image is independent of package rebuild order.
TDVP_PCMANFM_PROFILE="${SCRIPT_DIR}/../../package/tdvp-labwc-desktop/src/pcmanfm.conf"
if [ ! -f "${TDVP_PCMANFM_PROFILE}" ]; then
    printf 'TDVP packaging error: PCManFM desktop profile is missing: %s\n' \
        "${TDVP_PCMANFM_PROFILE}" >&2
    exit 1
fi
install -D -m 0644 "${TDVP_PCMANFM_PROFILE}" \
    "${TARGET_DIR}/etc/xdg/pcmanfm/default/pcmanfm.conf"

# xkeyboard-config provides the regular US layout.  Extend it with a product
# variant rather than replacing its files: the TCA8418 reports both physical
# Fn keys as KEY_FN, and the TDVP variant exposes the printed yellow glyphs as
# the normal XKB third level in the graphical Labwc session.
TDVP_XKB_SYMBOLS="${TARGET_DIR}/etc/tdvp/keymaps/tdvp-fn-yellow.xkb"
TDVP_XKB_VARIANT="${TARGET_DIR}/etc/tdvp/keymaps/tdvp-us-xkb-variant.xkb"
XKB_SYMBOL_DIR="${TARGET_DIR}/usr/share/X11/xkb/symbols"
if [ ! -f "${TDVP_XKB_SYMBOLS}" ] || [ ! -f "${TDVP_XKB_VARIANT}" ] || \
	[ ! -f "${XKB_SYMBOL_DIR}/us" ]; then
	printf '%s\n' 'TDVP packaging error: Fn XKB layer or base US layout is missing' >&2
	exit 1
fi
install -D -m 0644 "${TDVP_XKB_SYMBOLS}" "${XKB_SYMBOL_DIR}/tdvp"
if ! grep -Fq 'xkb_symbols "tdvp"' "${XKB_SYMBOL_DIR}/us"; then
	printf '\n' >> "${XKB_SYMBOL_DIR}/us"
	cat "${TDVP_XKB_VARIANT}" >> "${XKB_SYMBOL_DIR}/us"
fi

# The packaged netman plugin is Raspberry Pi's upstream component.  Its
# settings schema is not installed by the stripped-down build, so compile an
# isolated cache at image-build time instead of contaminating the target's
# global GSettings cache or relying on a manual first-boot command.
TDVP_NETMAN_SCHEMA="${SCRIPT_DIR}/../../package/wfplug-netman/org.rpi.nm-applet.gschema.xml"
TDVP_GSETTINGS_DIR="${TARGET_DIR}/etc/tdvp/gsettings"
if [ ! -f "${TDVP_NETMAN_SCHEMA}" ] || [ ! -x "${HOST_DIR}/bin/glib-compile-schemas" ]; then
    printf '%s\n' 'TDVP packaging error: netman schema or host glib-compile-schemas is missing' >&2
    exit 1
fi
install -D -m 0644 "${TDVP_NETMAN_SCHEMA}" \
    "${TDVP_GSETTINGS_DIR}/org.rpi.nm-applet.gschema.xml"
"${HOST_DIR}/bin/glib-compile-schemas" "${TDVP_GSETTINGS_DIR}"

# nm-connection-editor is a normal GTK application, not a GNOME Shell
# component.  Its upstream build installs its schema under the standard GLib
# location; compile that cache while building the image because the target
# intentionally does not carry glib-compile-schemas just for first boot.
NM_EDITOR_GSETTINGS_DIR="${TARGET_DIR}/usr/share/glib-2.0/schemas"
TDVP_NM_EDITOR_SCHEMA="${SCRIPT_DIR}/../../package/nm-connection-editor/src/org.gnome.nm-applet.gschema.xml"
if [ ! -f "${TDVP_NM_EDITOR_SCHEMA}" ]; then
	printf 'TDVP packaging error: nm-connection-editor schema is missing: %s\n' \
		"${TDVP_NM_EDITOR_SCHEMA}" >&2
	exit 1
fi
install -D -m 0644 "${TDVP_NM_EDITOR_SCHEMA}" \
	"${NM_EDITOR_GSETTINGS_DIR}/org.gnome.nm-applet.gschema.xml"
"${HOST_DIR}/bin/glib-compile-schemas" "${NM_EDITOR_GSETTINGS_DIR}"

# pplug-netman is maintained by Raspberry Pi and deliberately shares its
# common implementation between the LXPanel and wf-panel-pi modules.  That
# implementation invokes lp-connection-editor.  This product ships the
# upstream NetworkManager editor binary (nm-connection-editor), not a second
# LXPanel-specific editor, so provide the expected executable contract as a
# small Wayland-aware compatibility launcher.  The panel process does not
# always export WAYLAND_DISPLAY explicitly; GTK's default is wayland-0 for
# this single-seat image.  Keep an existing caller-supplied schema directory
# intact, otherwise use the cache compiled above.
cat > "${TARGET_DIR}/usr/bin/lp-connection-editor" <<'EOF'
#!/bin/sh
set -eu

if [ -z "${WAYLAND_DISPLAY:-}" ]; then
    export WAYLAND_DISPLAY=wayland-0
fi
if [ -z "${GSETTINGS_SCHEMA_DIR:-}" ] && \
        [ -f /usr/share/glib-2.0/schemas/gschemas.compiled ]; then
    export GSETTINGS_SCHEMA_DIR=/usr/share/glib-2.0/schemas
fi
export GDK_BACKEND="${GDK_BACKEND:-wayland}"
exec /usr/bin/nm-connection-editor "$@"
EOF
chmod 0755 "${TARGET_DIR}/usr/bin/lp-connection-editor"

while IFS= read -r -d '' file; do
    if LC_ALL=C grep -Iq . "${file}"; then
        sed -i 's/\r$//' "${file}"
    fi
done < <(find "${TARGET_DIR}/etc" -type f -print0)

# Keep the vendor camera/ISP service in the image. Remove only the optional
# ADB/MTP and plaintext Telnet services from the target service inventory.
rm -f \
	"${TARGET_DIR}/etc/init.d/S40network" \
    "${TARGET_DIR}/etc/init.d/S41adb_mtp" \
    "${TARGET_DIR}/etc/init.d/S50telnet"

# The product image uses OpenSSH for recovery. The root password comes from
# BR2_TARGET_GENERIC_ROOT_PASSWD, and the final target writes the matching
# password-authenticated recovery policy.
SSHD_CONFIG="${TARGET_DIR}/etc/ssh/sshd_config"
if [ ! -f "${SSHD_CONFIG}" ]; then
    printf 'TDVP packaging error: OpenSSH server configuration is missing: %s\n' \
        "${SSHD_CONFIG}" >&2
    exit 1
fi
sed -i -E '/^[[:space:]]*#?[[:space:]]*PermitRootLogin[[:space:]]+/d' \
    "${SSHD_CONFIG}"
printf '%s\n' 'PermitRootLogin yes' >> "${SSHD_CONFIG}"

# Enable SSH and the standard Raspberry Pi panel backends. NetworkManager owns
# wired and Wi-Fi profiles; wfplug-netman talks to it through its upstream
# D-Bus API, so the product must not leave a competing networkd/wpa instance
# active in the same image. PulseAudio supplies the standard protocol consumed
# by Raspberry Pi's upstream wfplug-volumepulse plugin.  The plugin needs the
# authenticated desktop-user daemon, which Labwc starts after login; do not
# enable PulseAudio's incompatible system-wide service here.
mkdir -p \
	"${TARGET_DIR}/etc/NetworkManager" \
	"${TARGET_DIR}/etc/pulse/default.pa.d" \
	"${TARGET_DIR}/etc/systemd/network" \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants" \
	"${TARGET_DIR}/usr/lib/systemd/system"
rm -f \
	"${TARGET_DIR}/etc/systemd/network/20-wired.network" \
	"${TARGET_DIR}/etc/systemd/network/30-wifi.network" \
	"${TARGET_DIR}/etc/network/interfaces" \
	"${TARGET_DIR}/etc/wpa_supplicant/wpa_supplicant-wlan0.conf" \
	"${TARGET_DIR}/usr/lib/systemd/system/wpa_supplicant@.service" \
	"${TARGET_DIR}/usr/lib/wf-panel-pi/libtdvp-network.so" \
	"${TARGET_DIR}/usr/lib/wf-panel-pi/libtdvp-volume.so" \
	"${TARGET_DIR}/usr/lib/wf-panel-pi/libtdvp-power.so" \
	"${TARGET_DIR}/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-provision-data" \
	"${TARGET_DIR}/usr/lib/systemd/system/tdvp-data-storage.service" \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/tdvp-data-storage.service" \
	"${TARGET_DIR}/usr/local/bin/vpl-audio-menu" \
	"${TARGET_DIR}/usr/local/bin/vpl-wifi" \
	"${TARGET_DIR}/usr/local/bin/vpl-files" \
	"${TARGET_DIR}/usr/share/applications/pcmanfm.desktop" \
	"${TARGET_DIR}/usr/share/applications/vpl-audio.desktop" \
	"${TARGET_DIR}/usr/share/applications/vpl-wifi.desktop" \
	"${TARGET_DIR}/usr/share/applications/vpl-files.desktop" \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/systemd-networkd.service" \
	"${TARGET_DIR}/etc/systemd/system/sockets.target.wants/systemd-networkd.socket" \
	"${TARGET_DIR}/etc/systemd/system/network-online.target.wants/systemd-networkd-wait-online.service" \
	"${TARGET_DIR}/etc/systemd/system/dbus-org.freedesktop.network1.service" \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/wpa_supplicant.service" \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service" \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/pulseaudio.service"
# Removing the wants links is insufficient: a vendor preset or a socket
# activation path can still start networkd after an SDK update.  NetworkManager
# is the product's sole connection owner, so use systemd's standard persistent
# mask for the competing service and its activation units.  Keep the
# wpa_supplicant executable itself; NetworkManager starts and owns it through
# D-Bus for Wi-Fi rather than through a separately enabled service.
for unit in \
	"systemd-networkd.service" \
	"systemd-networkd.socket" \
	"systemd-networkd-wait-online.service"; do
	ln -sfn /dev/null "${TARGET_DIR}/etc/systemd/system/${unit}"
done
# The integrated RTL8152 can report an invalid factory address.  Ask systemd
# to assign a stable per-machine address before NetworkManager starts rather than
# retaining a freshly generated address after each cold boot.
cat > "${TARGET_DIR}/etc/systemd/network/10-r8152.link" <<'EOF'
[Match]
Driver=r8152

[Link]
MACAddressPolicy=persistent
EOF
cat > "${TARGET_DIR}/etc/NetworkManager/NetworkManager.conf" <<'EOF'
[main]
plugins=keyfile

[device]
wifi.scan-rand-mac-address=no
EOF
if [ ! -f "${TARGET_DIR}/usr/lib/systemd/system/NetworkManager.service" ] || \
	[ ! -x "${TARGET_DIR}/usr/bin/pulseaudio" ]; then
	printf '%s\n' 'TDVP packaging error: NetworkManager service or PulseAudio client/server is missing' >&2
	exit 1
fi
ln -sf ../../../../usr/lib/systemd/system/sshd.service \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/sshd.service"
ln -sf ../../../../usr/lib/systemd/system/NetworkManager.service \
	"${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/NetworkManager.service"

# pplug-netman is the upstream Raspberry Pi panel implementation. Its status
# icon names predate the current Adwaita naming convention, so map every
# requested signal level to the equivalent *existing* standard theme asset.
# This is an alias-only compatibility layer, not a TDVP-specific icon set.
ADWAITA_ICON_ROOT="${TARGET_DIR}/usr/share/icons/Adwaita"
for REQUIRED_STATUS_ICON in \
	network-wireless-offline-symbolic.svg \
	network-wireless-signal-none-symbolic.svg \
	network-wireless-signal-weak-symbolic.svg \
	network-wireless-signal-ok-symbolic.svg \
	network-wireless-signal-good-symbolic.svg \
	network-wireless-signal-excellent-symbolic.svg \
	network-transmit-receive-symbolic.svg; do
	if [ ! -f "${ADWAITA_ICON_ROOT}/scalable/status/${REQUIRED_STATUS_ICON}" ]; then
		printf 'TDVP packaging error: required Adwaita status icon is missing: %s\n' \
			"${REQUIRED_STATUS_ICON}" >&2
		exit 1
	fi
done
for STATUS_DIR in "${ADWAITA_ICON_ROOT}"/*/status; do
	[ -d "${STATUS_DIR}" ] || continue
	for STATUS_ALIAS in \
		'nm-no-connection=network-wireless-offline-symbolic' \
		'network-wireless-connected-00=network-wireless-signal-none-symbolic' \
		'network-wireless-connected-25=network-wireless-signal-weak-symbolic' \
		'network-wireless-connected-50=network-wireless-signal-ok-symbolic' \
		'network-wireless-connected-75=network-wireless-signal-good-symbolic' \
		'network-wireless-connected-100=network-wireless-signal-excellent-symbolic' \
		'nm-signal-00=network-wireless-signal-none-symbolic' \
		'nm-signal-25=network-wireless-signal-weak-symbolic' \
		'nm-signal-50=network-wireless-signal-ok-symbolic' \
		'nm-signal-75=network-wireless-signal-good-symbolic' \
		'nm-signal-100=network-wireless-signal-excellent-symbolic' \
		'nm-device-wired=network-transmit-receive-symbolic'; do
		STATUS_NAME="${STATUS_ALIAS%%=*}"
		THEME_NAME="${STATUS_ALIAS#*=}"
		if [ -f "${STATUS_DIR}/${THEME_NAME}.symbolic.png" ]; then
			ln -sfn "${THEME_NAME}.symbolic.png" "${STATUS_DIR}/${STATUS_NAME}.png"
		fi
	done
done
for STATUS_ALIAS in \
	'nm-no-connection=network-wireless-offline-symbolic' \
	'network-wireless-connected-00=network-wireless-signal-none-symbolic' \
	'network-wireless-connected-25=network-wireless-signal-weak-symbolic' \
	'network-wireless-connected-50=network-wireless-signal-ok-symbolic' \
	'network-wireless-connected-75=network-wireless-signal-good-symbolic' \
	'network-wireless-connected-100=network-wireless-signal-excellent-symbolic' \
	'nm-signal-00=network-wireless-signal-none-symbolic' \
	'nm-signal-25=network-wireless-signal-weak-symbolic' \
	'nm-signal-50=network-wireless-signal-ok-symbolic' \
	'nm-signal-75=network-wireless-signal-good-symbolic' \
	'nm-signal-100=network-wireless-signal-excellent-symbolic' \
	'nm-device-wired=network-transmit-receive-symbolic'; do
	STATUS_NAME="${STATUS_ALIAS%%=*}"
	THEME_NAME="${STATUS_ALIAS#*=}"
	ln -sfn "${THEME_NAME}.svg" \
		"${ADWAITA_ICON_ROOT}/scalable/status/${STATUS_NAME}.svg"
done
# No host-native gtk-update-icon-cache is assumed during cross builds.  GTK
# safely scans the theme directories on first use; removing an old cache makes
# the aliases above visible on the very first graphical login.
rm -f "${ADWAITA_ICON_ROOT}/icon-theme.cache"

# The product's default locale is China Standard Time.  Use the same timezone
# for NTP, the RTC presentation and the existing Raspberry Pi clock widget.
if [ ! -f "${TARGET_DIR}/usr/share/zoneinfo/Asia/Shanghai" ]; then
	printf '%s\n' 'TDVP packaging error: Asia/Shanghai timezone data is missing' >&2
	exit 1
fi
ln -sfn /usr/share/zoneinfo/Asia/Shanghai "${TARGET_DIR}/etc/localtime"
printf '%s\n' 'Asia/Shanghai' > "${TARGET_DIR}/etc/timezone"

# Buildroot produces the immutable system baseline. opkg manages only
# ABI-matched applications and keeps its state separate from the image.  The
# source is fixed to this base-image ABI. Signature verification is mandatory:
# the privileged tdvp-opkg wrapper imports the single embedded release public
# key immediately before an operator asks opkg to use the feed.  This must not
# be a boot-time prerequisite: a desktop must remain usable while offline or
# if an operator-visible package verification error needs investigation.
mkdir -p \
    "${TARGET_DIR}/etc/opkg" \
	"${TARGET_DIR}/etc/opkg/gpg" \
    "${TARGET_DIR}/usr/lib/opkg" \
    "${TARGET_DIR}/var/lib/opkg/lists" \
    "${TARGET_DIR}/var/lib/opkg/info"
cat > "${TARGET_DIR}/etc/opkg/opkg.conf" <<'EOF'
dest root /
option lists_dir /var/lib/opkg/lists
option info_dir /var/lib/opkg/info
option status_file /var/lib/opkg/status
option tmp_dir /tmp
option check_signature 1
option signature_type gpg-asc
option gpg_dir /etc/opkg/gpg
# The dedicated keyring contains only the immutable TDVP release key.  TrustAny
# lets gpg accept that imported key without writable owner-trust state; it does
# not accept an unknown key or an invalid detached signature.
option gpg_trust_level TrustAny
arch all 1
arch noarch 1
arch riscv64 10
EOF
cat > "${TARGET_DIR}/etc/opkg/tdvp-feed.conf" <<'EOF'
# The sole package source is ABI-fixed for this base image.  Do not add a
# generic OpenWrt, Debian, or arbitrary riscv64 source.
src/gz tdvp_apps_r2 https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/r2/riscv64
EOF
cat > "${TARGET_DIR}/var/lib/opkg/status" <<'EOF'
Package: tdvp-platform-abi
Version: 2025.02.1-k230.6.6.36-glibc2.33-rv64-lp64d-r1
Architecture: riscv64
Status: install ok installed
Description: ABI identity for TDVP K230 firmware r1

Package: tdvp-base-runtime
Version: 2025.02.1-k230.6.6.36-glibc2.33-rv64-lp64d-r1
Architecture: riscv64
Status: install ok installed
Description: Image-owned glibc and C++ runtime seed for TDVP K230 r1

Package: tdvp-base-desktop
Version: 2025.02.1-k230.6.6.36-glibc2.33-rv64-lp64d-r1
Architecture: riscv64
Status: install ok installed
Description: Image-owned Wayland and GTK desktop runtime seed for TDVP K230 r1

Package: tdvp-base-network
Version: 2025.02.1-k230.6.6.36-glibc2.33-rv64-lp64d-r1
Architecture: riscv64
Status: install ok installed
Description: Image-owned TLS and NetworkManager runtime seed for TDVP K230 r1

Package: tdvp-base-audio
Version: 2025.02.1-k230.6.6.36-glibc2.33-rv64-lp64d-r1
Architecture: riscv64
Status: install ok installed
Description: Image-owned ALSA and PulseAudio runtime seed for TDVP K230 r1

EOF

# tdvp-platform-abi is an image-owned virtual package, but opkg still expects
# every installed package to have an info-dir manifest.  Without this empty
# file, otherwise valid download/install transactions emit a spurious missing
# ``.list`` error while scanning the installed package database.
: > "${TARGET_DIR}/var/lib/opkg/info/tdvp-platform-abi.list"
for tdvp_seed_package in \
	tdvp-base-runtime \
	tdvp-base-desktop \
	tdvp-base-network \
	tdvp-base-audio; do
	: > "${TARGET_DIR}/var/lib/opkg/info/${tdvp_seed_package}.list"
done

# The immutable image intentionally contains no user credentials.  The
# enabled NetworkManager instance owns all network state and waits for a Wi-Fi
# connection block to be added by its upstream panel UI or by an operator.

# The image starts its graphical session through greetd.  Do not retain a
# direct fixed-user compositor service from an incremental target directory.
rm -f \
    "${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/tdvp-labwc-desktop.service" \
    "${TARGET_DIR}/usr/lib/systemd/system/tdvp-labwc-desktop.service" \
    "${TARGET_DIR}/usr/local/bin/tdvp-labwc-desktop-session"

# Set the platform identity after the SDK hook so the generated image and the
# running system use the same device name in getty and SSH.
printf '%s\n' 'tdisplay-k230' > "${TARGET_DIR}/etc/hostname"
printf '%s\n' 'T-Display K230 Labwc Desktop' > "${TARGET_DIR}/etc/issue"

# Write fixed TDVP provenance into the user-visible version file. The image
# identity remains independent of host state and build invocation order.
mkdir -p "${TARGET_DIR}/etc/version"
cat > "${TARGET_DIR}/etc/version/release_version" <<'EOF'
T-Display K230 Labwc Desktop
profile: k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
sdk_commit: 5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7
linux_commit: 7d4e1f444f461dbe3833bd99a4640e7b6c2cd529
EOF

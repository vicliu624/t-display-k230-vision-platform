#!/usr/bin/env bash
set -euo pipefail

# The SDK overlay is commonly checked out on Windows. Normalize only final
# runtime text configuration after the vendor post-build step, leaving binary
# firmware and all non-runtime build inputs untouched.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_POST_BUILD="${SCRIPT_DIR}/../canaan/k230-soc/post-build.sh"

"${SDK_POST_BUILD}" "$@"

: "${TARGET_DIR:?Buildroot did not provide TARGET_DIR}"
while IFS= read -r -d '' file; do
    if LC_ALL=C grep -Iq . "${file}"; then
        sed -i 's/\r$//' "${file}"
    fi
done < <(find "${TARGET_DIR}/etc" -type f -print0)

# Keep the vendor camera/ISP service in the image. Remove only the optional
# ADB/MTP and plaintext Telnet services from the target service inventory.
rm -f \
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

# Enable SSH and DHCP on wired interfaces. The board can expose USB Ethernet
# as enu1, so the match expression intentionally covers all en* devices.
mkdir -p \
    "${TARGET_DIR}/etc/systemd/network" \
    "${TARGET_DIR}/etc/systemd/system/multi-user.target.wants" \
    "${TARGET_DIR}/etc/wpa_supplicant" \
    "${TARGET_DIR}/usr/lib/systemd/system"
cat > "${TARGET_DIR}/etc/systemd/network/20-wired.network" <<'EOF'
[Match]
Name=en* eth*

[Network]
DHCP=yes

[DHCPv4]
RouteMetric=600
EOF
cat > "${TARGET_DIR}/etc/systemd/network/30-wifi.network" <<'EOF'
[Match]
Name=wlan0

[Network]
DHCP=yes
IPv6AcceptRA=yes

[DHCPv4]
RouteMetric=100
EOF
# The integrated RTL8152 can report an invalid factory address.  Ask systemd
# to assign a stable per-machine address before DHCP starts rather than
# retaining a freshly generated address after each cold boot.
cat > "${TARGET_DIR}/etc/systemd/network/10-r8152.link" <<'EOF'
[Match]
Driver=r8152

[Link]
MACAddressPolicy=persistent
EOF
cat > "${TARGET_DIR}/etc/wpa_supplicant/wpa_supplicant-wlan0.conf" <<'EOF'
ctrl_interface=/run/wpa_supplicant
update_config=1
country=CN
EOF
chmod 0600 "${TARGET_DIR}/etc/wpa_supplicant/wpa_supplicant-wlan0.conf"
cat > "${TARGET_DIR}/usr/lib/systemd/system/wpa_supplicant@.service" <<'EOF'
[Unit]
Description=WPA supplicant for %i
Wants=network-pre.target
Before=network-pre.target
After=sys-subsystem-net-devices-%i.device
BindsTo=sys-subsystem-net-devices-%i.device

[Service]
Type=simple
RuntimeDirectory=wpa_supplicant
ExecStartPre=/usr/sbin/ip link set %i up
ExecStart=/usr/sbin/wpa_supplicant -i %i -c /etc/wpa_supplicant/wpa_supplicant-%i.conf
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF
rm -f "${TARGET_DIR}/etc/systemd/system/wpa_supplicant@.service"
ln -sf ../../../../usr/lib/systemd/system/sshd.service \
    "${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/sshd.service"
ln -sf ../../../../usr/lib/systemd/system/systemd-networkd.service \
    "${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/systemd-networkd.service"
rm -f "${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/wpa_supplicant.service"
ln -sf ../../../../usr/lib/systemd/system/wpa_supplicant@.service \
    "${TARGET_DIR}/etc/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service"

# Buildroot produces the immutable system baseline. opkg is available for
# field administration and keeps its package state separate from the image.
mkdir -p \
    "${TARGET_DIR}/etc/opkg" \
    "${TARGET_DIR}/usr/lib/opkg" \
    "${TARGET_DIR}/var/lib/opkg/lists"
cat > "${TARGET_DIR}/etc/opkg/opkg.conf" <<'EOF'
dest root /
lists_dir /var/lib/opkg/lists
option tmp_dir /tmp
arch all 1
arch noarch 1
arch riscv64 10
src/gz tdvp_apps_r1 https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/riscv64
EOF
: > "${TARGET_DIR}/usr/lib/opkg/status"

# The immutable image intentionally contains no user credentials.  The
# enabled instance stays alive and waits for a network block to be added by
# the Wi-Fi UI or by an operator.

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

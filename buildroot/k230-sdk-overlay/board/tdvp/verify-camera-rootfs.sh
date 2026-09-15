#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ] || { echo "usage: $0 <target-root>" >&2; exit 2; }
camera_target="$(cd "$1" && pwd)"
fail() { echo "TDVP camera rootfs: $*" >&2; exit 1; }
printf '%s  %s\n' \
    5d3e7bd8914cd2a3d9ca9da03a2743e7bd8ab24b9cb090929950b632ffb833ce \
    "$camera_target/usr/bin/isp_media_server" | sha256sum -c -
for file in usr/lib/libvvcam.so usr/libexec/tdvp-gc2093-chip-id \
    usr/libexec/tdvp-camera-device usr/libexec/tdvp-camera-capture-check \
    usr/libexec/tdvp-camera-modules usr/lib/udev/rules.d/70-tdvp-camera.rules \
    usr/lib/systemd/system/tdvp-camera-modules.service \
    usr/lib/systemd/system/tdvp-camera-isp.service; do
    [ -s "$camera_target/$file" ] || fail "missing $file"
done
for module in vvcam_mipi vvcam_vb vvcam_isp vvcam_isp_subdev vvcam_video; do
    mapfile -t matches < <(find "$camera_target/lib/modules" -type f -name "$module.ko*")
    [ "${#matches[@]}" -eq 1 ] || fail "expected one $module module, found ${#matches[@]}"
done
grep -aFq '/dev/i2c-4' "$camera_target/usr/lib/libvvcam.so" || fail 'plugin lacks verified I2C4 transport'
grep -aFq 'sensor clock mismatch before enable' \
    "$camera_target/lib/modules/6.6.36/updates/vvcam_mipi.ko" || fail 'MIPI module lacks managed-clock guard'
[ ! -e "$camera_target/etc/init.d/S31canaan_isp" ] || fail 'unmanaged vendor ISP startup remains'
[ "$(readlink "$camera_target/etc/systemd/system/multi-user.target.wants/tdvp-camera-isp.service")" = \
    '../../../../usr/lib/systemd/system/tdvp-camera-isp.service' ] || fail 'ISP service not enabled'
grep -Fxq 'DevicePolicy=closed' "$camera_target/usr/lib/systemd/system/tdvp-camera-isp.service" || fail 'ISP device policy missing'
if grep -Eq '^DeviceAllow=.*(/dev/mem|char-)' "$camera_target/usr/lib/systemd/system/tdvp-camera-isp.service"; then
    fail 'ISP must not bypass managed clocks or allow whole device classes'
fi
echo 'TDVP camera rootfs: PASS pinned scalar ISP, modules and supervised startup (not hardware acceptance)'

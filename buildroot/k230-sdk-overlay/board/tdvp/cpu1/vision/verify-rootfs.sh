#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ] || { echo "usage: $0 <target-root>" >&2; exit 2; }
target="$(cd "$1" && pwd)"
source_dir="$(cd "$(dirname "$0")" && pwd)"
fail() { echo "TDVP AI/vision rootfs: $*" >&2; exit 1; }
while IFS= read -r path; do
    [ ! -e "$target$path" ] && [ ! -L "$target$path" ] || fail "Linux owner remains: $path"
done < "$source_dir/retired-linux-paths.txt"
while IFS= read -r module; do
    matches="$(find "$target/lib/modules" -type f -name "$module.ko*")"
    [ -z "$matches" ] || fail "Linux owner module remains: $matches"
done < "$source_dir/retired-linux-modules.txt"
mapfile -t modules < <(find "$target/lib/modules" -type f -name tdvp_cpu1_vision.ko)
[ "${#modules[@]}" -eq 1 ] || fail 'expected one uncompressed CPU1 bridge module'
grep -aFq 'tdvp,cpu1-vision-v1' "${modules[0]}" || fail 'bridge lacks the paired DT alias'
grep -aFq 'tdvp-vision' "${modules[0]}" || fail 'bridge lacks the application endpoint'
cmp "$source_dir/linux/70-tdvp-cpu1-vision.rules" "$target/usr/lib/udev/rules.d/70-tdvp-cpu1-vision.rules"
cmp "$source_dir/linux/tdvp-cpu1-vision.conf" "$target/usr/lib/modules-load.d/tdvp-cpu1-vision.conf"
grep -Eq '^video:[^:]*:[0-9]+:' "$target/etc/group" || fail 'video group missing'
echo 'TDVP AI/vision rootfs: PASS bridge, boot loading and video access; no Linux ISP/KPU owner'

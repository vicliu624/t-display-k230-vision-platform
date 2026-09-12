#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 1 ] || { echo "usage: $0 <rootfs.ext2>" >&2; exit 2; }
rootfs="$1"
source_dir="$(cd "$(dirname "$0")" && pwd)"
while IFS= read -r path; do
    if debugfs -R "stat $path" "$rootfs" 2>/dev/null | grep -q '^Inode:'; then
        echo "TDVP AI/vision image: competing Linux artifact: $path" >&2
        exit 1
    fi
done < "$source_dir/retired-linux-paths.txt"
# Inspect all installed module paths, including old compressed/nested modules,
# not just a known updates/ filename or modules.dep which may itself be stale.
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
mkdir -p "$temporary/lib" "$temporary/etc" "$temporary/usr/lib/udev/rules.d" "$temporary/usr/lib/modules-load.d"
debugfs -R "rdump /lib/modules $temporary/lib" "$rootfs" >/dev/null 2>&1
for path in /etc/group /usr/lib/udev/rules.d/70-tdvp-cpu1-vision.rules \
    /usr/lib/modules-load.d/tdvp-cpu1-vision.conf; do
    debugfs -R "dump $path $temporary$path" "$rootfs" >/dev/null 2>&1
    test -s "$temporary$path"
done
bash "$source_dir/verify-rootfs.sh" "$temporary"

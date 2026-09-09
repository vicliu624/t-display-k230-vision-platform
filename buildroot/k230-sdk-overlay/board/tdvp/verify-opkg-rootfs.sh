#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo 'Usage: verify-opkg-rootfs.sh <rootfs.ext2> [export-directory]' >&2
    exit 2
fi
export PATH="$PATH:/usr/sbin:/sbin"
rootfs="$(realpath "$1")"
script_dir="$(cd "$(dirname "$0")" && pwd)"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
mkdir "$temporary/root"
# debugfs opens the filesystem read-only. Its rdump preserves link targets
# and modes, so this check sees the packaged files after fakeroot hooks.
debugfs -R "rdump / $temporary/root" "$rootfs" > "$temporary/debugfs.log" 2>&1
python3 "$script_dir/seed-opkg-image.py" --verify --require-buildroot --target-root "$temporary/root"
if [[ $# -eq 2 ]]; then
    mkdir -p "$2"
    cp "$temporary/root/usr/share/tdvp/opkg/image-base.json" "$2/tdvp-image-base.json"
    cp "$temporary/root/var/lib/opkg/status" "$2/tdvp-opkg-status"
    tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
        -C "$temporary/root/var/lib/opkg" -cf - info | gzip -n > "$2/tdvp-opkg-info.tar.gz"
fi

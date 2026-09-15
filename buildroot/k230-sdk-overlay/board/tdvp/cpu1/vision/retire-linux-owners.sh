#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 2 ] || { echo "usage: $0 <buildroot-target> <host-depmod>" >&2; exit 2; }
target="$(realpath -e "$1")"
depmod="$2"
source_dir="$(cd "$(dirname "$0")" && pwd)"
# Build-time only. Never permit a missing target argument or the host root.
[ "$target" != / ] && [ -d "$target/etc" ] && [ -d "$target/usr" ] && [ -x "$depmod" ]
remove_owned() {
    local path="$1" parent
    # Resolve parents, not the leaf: removing an old /dev/null service symlink
    # must unlink that symlink, but a parent pointing outside target is unsafe.
    parent="$(realpath -m "$(dirname "$path")")"
    case "$parent/" in "$target/"*) ;; *) echo "unsafe retired target: $path" >&2; exit 1;; esac
    rm -rf -- "$path"
}
while IFS= read -r path; do
    [[ "$path" == /* && "$path" != *..* && "$path" != / ]] || exit 1
    remove_owned "$target$path"
done < "$source_dir/retired-linux-paths.txt"
module_root="$(realpath -m "$target/lib/modules")"
case "$module_root/" in "$target/"*) ;; *) echo 'unsafe target module directory' >&2; exit 1;; esac
if [ -d "$module_root" ]; then
    while IFS= read -r module; do
        [[ "$module" =~ ^[a-z0-9_]+$ ]] || exit 1
        while IFS= read -r -d '' file; do remove_owned "$file"; done < <(
            find "$module_root" -type f \( -name "$module.ko" -o -name "$module.ko.xz" \
                -o -name "$module.ko.gz" -o -name "$module.ko.zst" \) -print0)
    done < "$source_dir/retired-linux-modules.txt"
    # Post-build follows module installation; update depmod after retirement
    # so no stale dependency/alias entry points back to an old owner.
    for version in "$module_root"/*; do
        [ -d "$version" ] || continue
        [ ! -L "$version" ] || { echo 'unsafe target module version symlink' >&2; exit 1; }
        "$depmod" -a -b "$target" "$(basename "$version")"
    done
fi
echo 'TDVP AI/vision: retired Linux ISP/KPU artifacts from the build target'

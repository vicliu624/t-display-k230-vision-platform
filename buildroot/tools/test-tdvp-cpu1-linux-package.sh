#!/usr/bin/env bash
set -euo pipefail
[ "$#" -eq 2 ] || { echo "usage: $0 <real-built-tdvp_cpu1_vision.ko> <buildroot-host-depmod>" >&2; exit 2; }
module="$(realpath "$1")"
# Keep the depmod symlink name: Buildroot uses the multicall kmod executable,
# whose operation is selected by argv[0], not just by its resolved inode.
depmod="$2"
[ -x "$depmod" ]
file "$module" | grep -Fq RISC-V
project="$(cd "$(dirname "$0")/../.." && pwd)"
overlay="$project/buildroot/k230-sdk-overlay"
vision="$overlay/board/tdvp/cpu1/vision"
temporary="$(mktemp -d)"
trap 'rm -rf -- "$temporary"' EXIT
target="$temporary/target"
mkdir -p "$target/etc" "$target/usr" "$target/lib/modules/6.6.36/updates" "$temporary/package"
printf 'root:x:0:\nvideo:x:44:tdvp\n' > "$target/etc/group"
cp "$module" "$target/lib/modules/6.6.36/updates/tdvp_cpu1_vision.ko"
cp -a "$vision/." "$temporary/package/"
# Execute the real package install recipe, not a second handwritten copy.
printf 'include %s\n.PHONY: %s/package/install\n%s/package/install:\n\t$(TDVP_CPU1_VISION_INSTALL_TARGET_CMDS)\n\t$(TDVP_CPU1_VISION_INSTALL_STAGING_CMDS)\n' \
    "$overlay/package/tdvp-cpu1-vision/tdvp-cpu1-vision.mk" "$temporary" "$temporary" > "$temporary/Makefile"
make --no-print-directory -f "$temporary/Makefile" INSTALL=install TARGET_DIR="$target" \
    STAGING_DIR="$temporary/staging" "$temporary/package/install"
cmp "$vision/tdvp_vision_abi.h" "$temporary/staging/usr/include/tdvp/tdvp_vision_abi.h"
cmp "$vision/tdvp_ai_abi.h" "$temporary/staging/usr/include/tdvp/tdvp_ai_abi.h"
bash "$vision/verify-rootfs.sh" "$target"
# An older bridge must not pass merely because it has the same ABI/telemetry.
# Mutate each resource label in a copy of the actual ELF; never alter the input.
installed="$target/lib/modules/6.6.36/updates/tdvp_cpu1_vision.ko"
for claim in tdvp-cpu1-kpu-sram tdvp-cpu1-shared-sram tdvp-cpu1-gnne-fft-ai2d; do
    LC_ALL=C sed "s/$claim/${claim/tdvp/xxxx}/g" "$module" > "$installed"
    if bash "$vision/verify-rootfs.sh" "$target" > "$temporary/rejected.log" 2>&1; then
        echo "missing AI resource claim accepted: $claim" >&2; exit 1
    fi
    grep -Fq "bridge lacks AI resource claim: $claim" "$temporary/rejected.log"
done
cp "$module" "$installed"
# A pre-service bridge must not pass the new paired userspace/CPU1 contract.
for claim in ai_abi=1 backend=cpu1-ai2d,fft,kpu-kws; do
    LC_ALL=C sed "s/$claim/xxxxxxxx/g" "$module" > "$installed"
    if bash "$vision/verify-rootfs.sh" "$target" > "$temporary/rejected.log" 2>&1; then
        echo "missing async AI service claim accepted: $claim" >&2; exit 1
    fi
    grep -Eq 'bridge lacks (AI job ABI|CPU1 AI job endpoint)' "$temporary/rejected.log"
done
cp "$module" "$installed"
# Plant every retired artifact and compressed/nested module. Check refusal
# first, then cleanup twice to cover reused output and idempotence.
while IFS= read -r path; do
    mkdir -p "$(dirname "$target$path")"
    printf 'retired\n' > "$target$path"
    if bash "$vision/verify-rootfs.sh" "$target" > "$temporary/rejected.log" 2>&1; then
        echo "retired path was accepted: $path" >&2; exit 1
    fi
    grep -Fq "$path" "$temporary/rejected.log"
    rm -- "$target$path"
done < "$vision/retired-linux-paths.txt"
while IFS= read -r path; do printf 'retired\n' > "$target$path"; done < "$vision/retired-linux-paths.txt"
mkdir -p "$target/lib/modules/6.6.36/updates/nested"
while IFS= read -r name; do
    printf 'retired\n' > "$target/lib/modules/6.6.36/updates/nested/$name.ko.xz"
done < "$vision/retired-linux-modules.txt"
for pass in 1 2; do
    bash "$vision/retire-linux-owners.sh" "$target" "$depmod"
    bash "$vision/verify-rootfs.sh" "$target"
done
# This is a small filesystem regression using the actual compiled bridge,
# not a bootable SD image or a substitute for the complete image verifier.
truncate -s 32M "$temporary/rootfs.ext2"
mke2fs -q -t ext4 -F -d "$target" "$temporary/rootfs.ext2"
bash "$vision/verify-rootfs-image.sh" "$temporary/rootfs.ext2"
printf 'retired\n' > "$temporary/retired"
# Add the real forbidden path, proving the image check reads ext4, not target.
debugfs -w -R 'mkdir /usr/bin' "$temporary/rootfs.ext2" >/dev/null 2>&1
debugfs -w -R "write $temporary/retired /usr/bin/isp_media_server" "$temporary/rootfs.ext2" >/dev/null 2>&1
if bash "$vision/verify-rootfs-image.sh" "$temporary/rootfs.ext2" > "$temporary/rejected.log" 2>&1; then
    echo 'retired ISP accepted in ext4 image' >&2; exit 1
fi
grep -Fq '/usr/bin/isp_media_server' "$temporary/rejected.log"
echo 'CPU1 Linux package: PASS actual install recipe, reused-target retirement twice, and real-module ext4 guards'

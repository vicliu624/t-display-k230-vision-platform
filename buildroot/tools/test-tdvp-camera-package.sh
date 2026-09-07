#!/usr/bin/env bash
set -euo pipefail
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_dir="$project/user-space/tdvp-camera-isp/src"
overlay="$project/buildroot/k230-sdk-overlay"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
expected_dts_hash="$(sed -n 's/^gc2093_dts_patch_sha256 = //p' "$project/buildroot/sdk-sources.lock" | tr -d '\r')"
actual_dts_hash="$(sha256sum "$overlay/linux/0066-tdvp-riscv-dts-enable-gc2093-managed-clock.patch" | awk '{print $1}')"
[ "$actual_dts_hash" = "$expected_dts_hash" ]
(
    cd "$source_dir/patches"
    sha256sum -c SHA256SUMS
)
profile="$overlay/configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
grep -Fxq 'BR2_PACKAGE_TDVP_CAMERA_ISP=y' "$profile"
grep -Fxq '# BR2_PACKAGE_VVCAM is not set' "$profile"
grep -Fxq 'BR2_STRIP_EXCLUDE_FILES="isp_media_server"' "$profile"
grep -Fxq 'DevicePolicy=closed' "$source_dir/tdvp-camera-isp.service"
! grep -Eq '^DeviceAllow=.*(/dev/mem|char-)' "$source_dir/tdvp-camera-isp.service"
grep -Fq '/etc/init.d/S31canaan_isp' "$overlay/board/tdvp/post-build.sh"
grep -Fq 'verify-camera-rootfs.sh' "$overlay/board/tdvp/post-build.sh"
grep -Fq 'verify-camera-dtb.sh' "$overlay/board/tdvp/verify-sdcard-image.sh"
grep -Fq -- '--vo=wlshm' "$overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-camera"
grep -Fq -- '--demuxer-thread=no --demuxer-readahead-secs=0' "$overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-camera"
grep -Fq -- '--demuxer-lavf-o-add=video_size=1920x1080' "$overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-camera"
grep -Fq -- '--demuxer-lavf-o-add=input_format=nv12' "$overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-camera"
! grep -Fq 'av://v4l2:/dev/video0' "$overlay/package/vicliu-pocket-linux-desktop/src/bin/vpl-camera"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 \
    "$source_dir/tdvp-camera-device.c" -o "$test_dir/tdvp-camera-device"
# Bad arguments must not enumerate/open hardware on the CI host.
status=0
"$test_dir/tdvp-camera-device" --invalid > /dev/null 2>&1 || status=$?
[ "$status" -eq 2 ]
sh -n "$source_dir/tdvp-camera-modules"

# Execute the production recipe's local-source preparation hook twice. The
# second run must restore pristine vendor files, not double-apply the patches.
mkdir -p "$test_dir/top/package" "$test_dir/build/patches"
ln -s "$project/vendor/k230_linux_sdk/buildroot-overlay/package/vvcam" "$test_dir/top/package/vvcam"
cp "$source_dir/patches/"* "$test_dir/build/patches/"
printf 'include %s\n.PHONY: %s/build/prepare\n%s/build/prepare:\n\t$(TDVP_CAMERA_ISP_PREPARE_VENDOR)\n' \
    "$overlay/package/tdvp-camera-isp/tdvp-camera-isp.mk" "$test_dir" "$test_dir" > "$test_dir/Makefile"
for cycle in 1 2; do
    make --no-print-directory -f "$test_dir/Makefile" TOPDIR="$test_dir/top" "$test_dir/build/prepare" >/dev/null
    grep -Fq 'tdvp_gc2093_open' "$test_dir/build/vvcam/src/gc2093.c"
done
echo 'TDVP camera package: PASS production prepare hook twice, source hashes and image/service contract'

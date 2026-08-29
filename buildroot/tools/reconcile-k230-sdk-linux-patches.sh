#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  reconcile-k230-sdk-linux-patches.sh <sdk-worktree>

Remove conflicting TDVP/Lewis queue filenames from a synchronised SDK
Buildroot tree. Run this after `make sync` and before every build target.
EOF
}

if [ "$#" -ne 1 ]; then
	usage
	exit 2
fi

WORKTREE="$(cd "$1" && pwd)"
PROFILE="k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
PATCH_DIR="$WORKTREE/output/buildroot-2025.02.1/linux"
OVERLAY_DIR="$WORKTREE/buildroot-overlay/linux"
VENDOR_PATCH_FIX_DIR="$OVERLAY_DIR/vendor"

fail() {
	printf 'TDVP SDK patch reconciliation: %s\n' "$*" >&2
	exit 1
}

[ -d "$PATCH_DIR" ] || fail "run SDK make sync before reconciliation: $PATCH_DIR"
[ -d "$OVERLAY_DIR" ] || fail "missing staged overlay patch directory: $OVERLAY_DIR"
[ -d "$VENDOR_PATCH_FIX_DIR" ] || fail "missing canonical vendor patch directory: $VENDOR_PATCH_FIX_DIR"

# The SDK sync rule uses additive copy semantics. Remove conflicting
# TDVP-imported names so the staged directory contains the active queue.
# Vendor patches are never removed here.
for conflicting_patch in \
	0019-dts-add-nonai2d.patch \
	0025-drm-canaan-fix-2lan-dsi-phy-with-timeout.patch \
	0026-drm-canaan-vo-add-xrgb8888-format.patch \
	0027-panel-canaan-universal-enable-reset-in-prepare.patch \
	0029-drm-canaan-fix-video-mode-to-burst.patch \
	0030-drm-canaan-fix-xrgb8888-opaque-and-rb-swap.patch \
	0031-riscv-dts-canaan-add-rm69a10-display.patch \
	0032-riscv-dts-canaan-add-k230-rm69a10-dts.patch \
	0033-drm-canaan-fix-rgb2yuv-color-swap.patch \
	0034-gt9895-touch.patch \
	0035-gdma-xrgb8888-rotation.patch \
	0036-add-gc2093-camera.patch \
	0035-tdvp-input-tca8418-reset-gpios.patch \
	0036-tdvp-riscv-dts-canaan-add-t-display-k230-keyboard.patch \
	0037-tdvp-input-tca8418-polling.patch \
	0038-tdvp-input-tca8418-hardware-debounce.patch \
	0039-tdvp-pinctrl-k230-support-standard-schmitt-enable.patch; do
	rm -f "$PATCH_DIR/$conflicting_patch"
done

# The SDK's 0019 patch has a malformed unified-diff context line. Keep the
# vendor implementation but stage the format-correct canonical copy so that
# every Buildroot invocation starts from the same valid queue.
install -m 0644 "$VENDOR_PATCH_FIX_DIR/0019-dts-add-nonai2d.patch" \
	"$PATCH_DIR/0019-dts-add-nonai2d.patch"

for expected_patch in \
	0025-tdvp-drm-canaan-standard-gem-dma.patch \
	0026-lewis-drm-canaan-fix-2lan-dsi-phy-with-timeout.patch \
	0027-lewis-drm-canaan-vo-add-xrgb8888-format.patch \
	0028-lewis-panel-canaan-universal-enable-reset-in-prepare.patch \
	0030-lewis-drm-canaan-fix-video-mode-to-burst.patch \
	0031-lewis-drm-canaan-fix-xrgb8888-opaque-and-rb-swap.patch \
	0032-lewis-riscv-dts-canaan-add-rm69a10-display.patch \
	0033-lewis-riscv-dts-canaan-add-k230-rm69a10-dts.patch \
	0034-lewis-drm-canaan-fix-rgb2yuv-color-swap.patch \
	0035-lewis-gdma-add-xrgb8888-rotation.patch \
	0036-tdvp-input-tca8418-reset-gpios.patch \
	0037-tdvp-riscv-dts-canaan-add-t-display-k230-keyboard.patch \
	0038-tdvp-input-tca8418-polling.patch \
	0039-tdvp-input-tca8418-hardware-debounce.patch \
	0040-tdvp-pinctrl-k230-support-standard-schmitt-enable.patch \
	0041-tdvp-input-add-gt9895-touchscreen.patch \
	0042-tdvp-riscv-dts-canaan-enable-k230-platform-services.patch \
	0043-tdvp-panel-canaan-universal-add-standard-backlight.patch \
	0044-tdvp-riscv-dts-canaan-add-keyboard-backlight.patch \
	0045-tdvp-power-bq27xxx-add-bq27220.patch \
	0046-tdvp-riscv-dts-canaan-add-dock-power-devices.patch \
	0047-tdvp-misc-add-radio-profile-selector.patch \
	0048-tdvp-radio-lr2021-spi-transport.patch \
	0049-tdvp-k230-spi-bound-irq-enumeration.patch \
	0050-tdvp-hwmon-aht20-standard-binding.patch \
	0051-tdvp-riscv-dts-canaan-add-external-i2s-amp.patch \
	0052-tdvp-asoc-canaan-add-external-i2s-output-switch.patch; do
	[ -f "$OVERLAY_DIR/$expected_patch" ] || fail "overlay is missing $expected_patch"
	[ -f "$PATCH_DIR/$expected_patch" ] || fail "SDK sync did not install $expected_patch"
done

bash "$(dirname "$0")/validate-k230-sdk-linux-patches.sh" "$PATCH_DIR"

printf 'TDVP SDK patch reconciliation: %s queue is clean\n' "$PROFILE"

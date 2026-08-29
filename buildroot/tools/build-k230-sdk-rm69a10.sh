#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  build-k230-sdk-rm69a10.sh <sdk-worktree> [make-target ...]

Build the staged K230 Linux SDK RM69A10 product profile with a Linux-only PATH.
When no make target is supplied, this builds the complete SDK image.

Set TDVP_INCREMENTAL=1 only after prepare-k230-sdk-worktree.sh has staged the
current project sources into an existing worktree. Incremental mode reuses the
configured Buildroot output and builds the requested target without running
the SDK sync or Buildroot reconfiguration steps.
EOF
}

if [ "$#" -lt 1 ]; then
	usage
	exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
WORKTREE="$(cd "$1" && pwd)"
shift

# The script receives the SDK worktree root, not Buildroot's generated output
# directory.  Catch the common ``.../output/<profile>`` mistake before looking
# for a manifest in the wrong place; it otherwise produces a misleading
# "manifest missing" error and can lead to a nested output tree.
case "$WORKTREE" in
	*/output/*)
		WORKTREE_CANDIDATE="${WORKTREE%%/output/*}"
		if [ -f "$WORKTREE_CANDIDATE/.tdvp/sdk-baseline-manifest" ]; then
			printf '%s\n' 'TDVP SDK build: pass the SDK worktree root, not output/<profile>.' >&2
			printf 'TDVP SDK build: expected argument: %s\n' "$WORKTREE_CANDIDATE" >&2
			exit 2
		fi
		;;
esac

MANIFEST="$WORKTREE/.tdvp/sdk-baseline-manifest"
OVERLAY_DIR="$PROJECT_DIR/buildroot/k230-sdk-overlay"
USERSPACE_DIR="$PROJECT_DIR/user-space"
CORE_PATCH_DIR="$PROJECT_DIR/buildroot/patches/buildroot"
USERSPACE_COMPONENTS=(
	tdvp-greeter
	tdvp-kpu-acceptance
	tdvp-labwc-desktop
	vicliu-pocket-linux-hardware
)

[ -f "$MANIFEST" ] || {
	printf 'TDVP SDK build: staged manifest missing: %s\n' "$MANIFEST" >&2
	exit 1
}

grep -Fqx 'sdk_commit=5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7' "$MANIFEST" || {
	printf '%s\n' 'TDVP SDK build: worktree does not match the pinned SDK commit' >&2
	exit 1
}

PROFILE="$(sed -n 's/^profile=//p' "$MANIFEST")"
case "$PROFILE" in
	k230_canmv_t_display_rm69a10_labwc_desktop_defconfig)
		;;
	*)
		printf 'TDVP SDK build: unsupported staged profile: %s\n' "$PROFILE" >&2
		exit 1
		;;
esac

write_tree_manifest() {
	local source_dir="$1"
	local source_prefix="$2"

	(
		cd "$source_dir"
		find . -type f -print0 | sort -z |
			while IFS= read -r -d '' file; do
				if LC_ALL=C grep -Iq . "$file"; then
					hash="$(sed 's/\r$//' "$file" | sha256sum | awk '{print $1}')"
				else
					hash="$(sha256sum "$file" | awk '{print $1}')"
				fi
				printf '%s  %s/%s\n' "$hash" "$source_prefix" "${file#./}"
			done
	)
}

write_platform_source_manifest() {
	local component

	{
		write_tree_manifest "$OVERLAY_DIR" "buildroot/k230-sdk-overlay"
		for component in "${USERSPACE_COMPONENTS[@]}"; do
			write_tree_manifest "$USERSPACE_DIR/$component" "user-space/$component"
		done
	} | LC_ALL=C sort
}

write_component_source_manifest() {
	local source_root="$1"
	local component

	for component in "${USERSPACE_COMPONENTS[@]}"; do
		write_tree_manifest "$source_root/$component/src" \
			"user-space/$component/src"
	done | LC_ALL=C sort
}

write_core_patch_manifest() {
	local patch_dir="$1"

	(
		cd "$patch_dir"
		find . -type f -name '*.patch' -print0 | sort -z |
			while IFS= read -r -d '' file; do
				sed 's/\r$//' "$file" | sha256sum | sed "s|  -$|  $file|"
			done
	)
}

current_source_manifest="$(mktemp)"
staged_source_manifest="$(mktemp)"
trap 'rm -f "$current_source_manifest" "$staged_source_manifest"' EXIT

write_platform_source_manifest > "$current_source_manifest"
grep -E '^[0-9a-f]{64}  ' "$MANIFEST" > "$staged_source_manifest" || true

cmp -s "$current_source_manifest" "$staged_source_manifest" || {
	printf '%s\n' 'TDVP SDK build: staged platform sources differ from the current project sources' >&2
	printf '%s\n' 'TDVP SDK build: rerun prepare-k230-sdk-worktree.sh with TDVP_ALLOW_OVERWRITE=1' >&2
	exit 1
}

staged_core_patch_digest="$(sed -n 's/^core_patch_sha256=//p' "$MANIFEST" | head -n 1)"
current_core_patch_digest="$(write_core_patch_manifest "$CORE_PATCH_DIR" | sha256sum | awk '{print $1}')"
[ -n "$staged_core_patch_digest" ] && \
	[ "$staged_core_patch_digest" = "$current_core_patch_digest" ] || {
		printf '%s\n' 'TDVP SDK build: staged Buildroot core patch set differs from the current project' >&2
		printf '%s\n' 'TDVP SDK build: rerun prepare-k230-sdk-worktree.sh with TDVP_ALLOW_OVERWRITE=1' >&2
		exit 1
	}

# A synchronized vendor Buildroot tree still keeps independent local-package
# build directories.  Track the complete TDVP input set separately so an
# incremental build cannot reuse a package configured from an older staged
# source tree.
platform_source_digest="$(sha256sum "$current_source_manifest" | awk '{print $1}')"
build_input_digest="$(printf 'profile=%s\nplatform_source=%s\ncore_patches=%s\n' \
	"$PROFILE" "$platform_source_digest" "$current_core_patch_digest" | \
	sha256sum | awk '{print $1}')"

if [ "$#" -eq 0 ]; then
	set -- all
fi

# Buildroot has no standalone `images` target: its default `all` target builds
# the complete root filesystem and all configured image artifacts. Keep the
# product command line ergonomic while always using Buildroot's real target.
normalized_targets=()
for target in "$@"; do
	case "$target" in
		image|images)
			normalized_targets+=(all)
			;;
		*)
			normalized_targets+=("$target")
			;;
	esac
done
set -- "${normalized_targets[@]}"

build_jobs="${TDVP_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}"
case "${build_jobs}" in
	''|*[!0-9]*|0)
		printf 'TDVP SDK build: TDVP_BUILD_JOBS must be a positive integer, got: %s\n' \
			"${build_jobs}" >&2
		exit 2
		;;
esac
if [ "${build_jobs}" -gt 16 ]; then
	build_jobs=16
fi

incremental_mode="${TDVP_INCREMENTAL:-0}"
case "${incremental_mode}" in
	0|1)
		;;
	*)
		printf 'TDVP SDK build: TDVP_INCREMENTAL must be 0 or 1, got: %s\n' \
			"${incremental_mode}" >&2
		exit 2
		;;
esac

# Reassemble only the filesystem and SD-card artifacts from an already built,
# configured output tree. This is deliberately opt-in: it is for recovering
# from an image packaging failure caught by the raw-image audit, not for
# compiling changed packages. Keeping it explicit prevents a costly full SDK
# rebuild when package binaries are already known-good.
image_rebuild_mode="${TDVP_IMAGE_REBUILD:-0}"
case "${image_rebuild_mode}" in
	0|1)
		;;
	*)
		printf 'TDVP SDK build: TDVP_IMAGE_REBUILD must be 0 or 1, got: %s\n' \
			"${image_rebuild_mode}" >&2
		exit 2
		;;
esac

compiler_major_version() {
	"$1" -dumpfullversion -dumpversion 2>/dev/null |
		sed -n 's/^\([0-9][0-9]*\).*/\1/p' | head -n 1
}

select_host_compiler() {
	local requested="$1"
	local preferred="$2"
	local fallback="$3"
	local compiler=""
	local major=""

	if [ -n "$requested" ]; then
		compiler="$requested"
	elif command -v "$preferred" >/dev/null 2>&1; then
		compiler="$preferred"
	else
		compiler="$fallback"
	fi

	command -v "$compiler" >/dev/null 2>&1 || {
		printf 'TDVP SDK build: required host compiler is unavailable: %s\n' "$compiler" >&2
		exit 1
	}
	major="$(compiler_major_version "$compiler")"
	case "$major" in
		''|*[!0-9]*)
			printf 'TDVP SDK build: cannot determine host compiler version: %s\n' "$compiler" >&2
			exit 1
			;;
	esac
	if [ "$major" -lt 8 ]; then
		printf 'TDVP SDK build: %s is GCC %s; this SDK requires host GCC 8 or newer\n' \
			"$compiler" "$major" >&2
		exit 1
	fi

	printf '%s\n' "$compiler"
}

# Buildroot's systemd Kconfig gate is evaluated by the host compiler. WSL's
# default gcc is 7.5 on the supported build host, which silently deselects
# systemd and every product component depending on it. Do not inherit HOSTCC
# from the invoking shell; TDVP_HOSTCC/TDVP_HOSTCXX are explicit overrides.
default_hostcc="$(select_host_compiler "${TDVP_HOSTCC:-}" gcc-10 gcc)"
default_hostcxx="$(select_host_compiler "${TDVP_HOSTCXX:-}" g++-10 g++)"

# WSL inherits the Windows PATH by default. Buildroot rejects whitespace in
# PATH, so deliberately pass only the standard Linux host-tool directories.
BUILD_ENV=(
	env -i
	"HOME=${HOME:-/tmp}"
	"USER=${USER:-$(id -un)}"
	"LOGNAME=${LOGNAME:-$(id -un)}"
	SHELL=/bin/bash
	"TERM=${TERM:-dumb}"
	"LANG=${LANG:-C.UTF-8}"
	"LC_ALL=${LC_ALL:-C.UTF-8}"
	"HOSTCC=${default_hostcc}"
	"HOSTCXX=${default_hostcxx}"
	# The vendor Makefile probes a mirror with curl but does not set a curl
	# timeout.  Select the official artifact source explicitly so a build cannot
	# hang during that probe; a controlled mirror can still override this.
	"BR2_PRIMARY_SITE=${TDVP_BR2_PRIMARY_SITE:-https://download.kendryte.com/k230/downloads/dl}"
	PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
	FORCE_UNSAFE_CONFIGURE=1
)

run_make() {
	"${BUILD_ENV[@]}" make -C "$WORKTREE" "CONF=$PROFILE" "$@"
}

configure_buildroot_output() {
	"${BUILD_ENV[@]}" make -C "$WORKTREE/output/buildroot-2025.02.1" \
		"$PROFILE" "O=$WORKTREE/output/$PROFILE"
}

run_buildroot_target() {
	"${BUILD_ENV[@]}" make -C "$OUTPUT_DIR" "$@"
}

verify_synchronized_overlay() {
	local source_file
	local relative_path
	local destination_file
	local mismatch=0

	# The vendor SDK builds from output/buildroot-*, not directly from the
	# staging overlay. Check every copied platform file before any Buildroot
	# target runs so a stale package recipe cannot silently survive a stage.
	while IFS= read -r -d '' source_file; do
		relative_path="${source_file#${WORKTREE}/buildroot-overlay/}"
		destination_file="$WORKTREE/output/buildroot-2025.02.1/$relative_path"
		if [ ! -f "$destination_file" ] || ! cmp -s "$source_file" "$destination_file"; then
			printf 'TDVP SDK build: staged overlay file was not synchronized: %s\n' \
			"$relative_path" >&2
			mismatch=1
		fi
	done < <(
		find "$WORKTREE/buildroot-overlay" -type f \
			-not -path '*/boot/*-overlay/*' \
			-not -path '*/linux-6.6.22/*' \
			-not -name .overlay_sync \
			-print0
	)

	[ "$mismatch" -eq 0 ] || {
		printf '%s\n' 'TDVP SDK build: refusing to build from a partially synchronized overlay' >&2
		exit 1
	}
}

synchronize_stage_overlay() {
	# Buildroot executes the synchronized SDK copy below output/buildroot-*.  A
	# staged overlay is not enough on its own: in incremental mode an unchanged
	# vendor sync stamp would otherwise leave post-build hooks, package recipes
	# and board files from an earlier stage in the image.
	rm -f "$WORKTREE/output/buildroot-2025.02.1/.overlay_sync"
	# The vendor sync Makefile unconditionally scans these two overlay roots.
	# This platform currently has no U-Boot or OpenSBI file overrides, but the
	# directories must exist for a clean, deterministic vendor sync.
	mkdir -p \
		"$WORKTREE/buildroot-overlay/boot/uboot/u-boot-2022.10-overlay" \
		"$WORKTREE/buildroot-overlay/boot/opensbi/opensbi-1.4-overlay"
	install -D -m 0644 "$MANIFEST" \
		"$WORKTREE/output/.tdvp/sdk-baseline-manifest"
	run_make sync
	verify_synchronized_overlay
	# The SDK synchronizer merges the platform overlay without --delete so it
	# cannot remove vendor package files that the platform does not own. This
	# product replaces PCManFM's source with the Raspberry Pi Wayland branch;
	# the vendor's old gettext-tiny patch targets a different translation file
	# and would otherwise survive an incremental sync and fail before our
	# PCManFM patch can be applied. Remove precisely that retired patch only
	# when it is absent from the staged platform package.
	if [ ! -e "$WORKTREE/buildroot-overlay/package/pcmanfm/0001-po-de-po-fix-build-with-gettext-tiny.patch" ]; then
		rm -f "$WORKTREE/output/buildroot-2025.02.1/package/pcmanfm/0001-po-de-po-fix-build-with-gettext-tiny.patch"
	fi
	bash "$SCRIPT_DIR/apply-buildroot-core-patches.sh" "$WORKTREE"
	bash "$SCRIPT_DIR/reconcile-k230-sdk-linux-patches.sh" "$WORKTREE"
	bash "$SCRIPT_DIR/register-k230-sdk-tdvp-packages.sh" "$WORKTREE"
}

verify_staged_component_sources() {
	local staged_component_root="$WORKTREE/buildroot-overlay/package"

	if ! cmp -s \
		<(write_component_source_manifest "$USERSPACE_DIR") \
		<(write_component_source_manifest "$staged_component_root"); then
		printf '%s\n' 'TDVP SDK build: staged user-space package sources differ from the project' >&2
		printf '%s\n' 'TDVP SDK build: rerun prepare-k230-sdk-worktree.sh before building' >&2
		exit 1
	fi
}

OUTPUT_DIR="$WORKTREE/output/$PROFILE"
BUILD_INPUT_STAMP="$OUTPUT_DIR/.tdvp-product-input.sha256"

required_product_config=(
	BR2_INIT_SYSTEMD
	BR2_PACKAGE_SYSTEMD
	BR2_PACKAGE_SEATD
	BR2_PACKAGE_LABWC
	BR2_PACKAGE_LIBGTK3
	BR2_PACKAGE_LIBGTK3_WAYLAND
	BR2_PACKAGE_GTK_LAYER_SHELL
	BR2_PACKAGE_GTKMM3
	BR2_PACKAGE_LIBFM_EXTRA
	BR2_PACKAGE_LIBMENU_CACHE
	BR2_PACKAGE_LIBFM
	BR2_PACKAGE_PCMANFM
	BR2_PACKAGE_OPKG
	BR2_PACKAGE_OPKG_GPG_SIGN
	BR2_PACKAGE_GNUPG2
	BR2_PACKAGE_GNUPG2_GPGV
	BR2_PACKAGE_TDVP_OPKG_TRUST
	BR2_PACKAGE_GLIB_NETWORKING
	BR2_PACKAGE_NETWORK_MANAGER
	BR2_PACKAGE_NETWORK_MANAGER_CLI
	BR2_PACKAGE_NM_CONNECTION_EDITOR
	BR2_PACKAGE_PROCPS_NG
	BR2_PACKAGE_UTIL_LINUX
	BR2_PACKAGE_UTIL_LINUX_BINARIES
	BR2_PACKAGE_UTIL_LINUX_RFKILL
	BR2_PACKAGE_GPTFDISK
	BR2_PACKAGE_GPTFDISK_SGDISK
	BR2_PACKAGE_E2FSPROGS
	BR2_PACKAGE_E2FSPROGS_RESIZE2FS
	BR2_PACKAGE_LIBCANBERRA
	BR2_PACKAGE_SOUND_THEME_FREEDESKTOP
	BR2_PACKAGE_LIBSECRET
	BR2_PACKAGE_LIBNMA
	BR2_PACKAGE_PULSEAUDIO
	BR2_PACKAGE_PULSEAUDIO_DAEMON
	BR2_PACKAGE_ALSA_UTILS_SPEAKER_TEST
	BR2_PACKAGE_WF_PANEL_PI
	BR2_PACKAGE_WFPLUG_BATT
	BR2_PACKAGE_WFPLUG_MENU
	BR2_PACKAGE_WFPLUG_CLOCK
	BR2_PACKAGE_WFPLUG_NETMAN
	BR2_PACKAGE_WFPLUG_POWER
	BR2_PACKAGE_WFPLUG_VOLUMEPULSE
	BR2_PACKAGE_WPA_SUPPLICANT_DBUS
	BR2_PACKAGE_FOOT
	BR2_PACKAGE_WOFI
	BR2_PACKAGE_WVKBD
	BR2_PACKAGE_MC
	BR2_PACKAGE_MPV
	BR2_PACKAGE_V4L2GRAB
	BR2_PACKAGE_YAVTA
	BR2_PACKAGE_SUDO
	BR2_PACKAGE_TDVP_LABWC_DESKTOP
	BR2_PACKAGE_VICLIU_POCKET_LINUX_DESKTOP
	BR2_PACKAGE_TDVP_GREETD
	BR2_PACKAGE_TDVP_GTKGREET
	BR2_PACKAGE_TDVP_GREETER
	BR2_PACKAGE_TDVP_KPU_ACCEPTANCE
	BR2_PACKAGE_VICLIU_POCKET_LINUX_HARDWARE
	BR2_PACKAGE_TDVP_DISPLAY_SMOKE
	BR2_PACKAGE_TDVP_KEYBOARD_LAYOUT
	BR2_PACKAGE_TDVP_WAYLAND_ACCEPTANCE
)

product_config_matches_contract() {
	local config="$1"
	local symbol

	[ -f "$config" ] || return 1
	for symbol in "${required_product_config[@]}"; do
		grep -Fqx "${symbol}=y" "$config" || return 1
	done
}

verify_product_config() {
	local config="$OUTPUT_DIR/.config"
	local symbol
	local missing=0

	for symbol in "${required_product_config[@]}"; do
		if ! grep -Fqx "${symbol}=y" "$config"; then
			printf 'TDVP SDK build: required Buildroot selection is absent: %s\n' "$symbol" >&2
			missing=1
		fi
	done
	[ "$missing" -eq 0 ] || {
		printf '%s\n' 'TDVP SDK build: refusing to build an incomplete product profile' >&2
		exit 1
	}
}

reset_incompatible_output() {
	if product_config_matches_contract "$OUTPUT_DIR/.config"; then
		return
	fi

	# An empty worktree is the normal first-build state. Keep this branch
	# successful so `set -e` does not treat it as a failed reset.
	[ -e "$OUTPUT_DIR" ] || return 0
	case "$OUTPUT_DIR" in
		"$WORKTREE"/output/*)
			;;
		*)
			printf 'TDVP SDK build: refusing to reset unexpected output path: %s\n' "$OUTPUT_DIR" >&2
			exit 1
			;;
	esac
	printf 'TDVP SDK build: resetting incomplete profile output: %s\n' "$OUTPUT_DIR"
	rm -rf "$OUTPUT_DIR"
}

product_inputs_changed=1
if [ -r "$BUILD_INPUT_STAMP" ] && \
	[ "$(tr -d '\r\n' < "$BUILD_INPUT_STAMP")" = "$build_input_digest" ]; then
	product_inputs_changed=0
fi

verify_staged_component_sources

if [ "$incremental_mode" = "1" ]; then
	case "$1" in
		sync|*defconfig|savedefconfig)
			printf '%s\n' 'TDVP SDK build: TDVP_INCREMENTAL does not support sync or defconfig targets' >&2
			exit 2
			;;
	esac
	[ -f "$OUTPUT_DIR/Makefile" ] || {
		printf 'TDVP SDK build: incremental output is not configured: %s\n' "$OUTPUT_DIR" >&2
		printf '%s\n' 'TDVP SDK build: run without TDVP_INCREMENTAL first' >&2
		exit 1
	}
	synchronize_stage_overlay
	verify_product_config
	printf 'TDVP SDK build: incremental target build using %s\n' "$OUTPUT_DIR"
else
	reset_incompatible_output
	synchronize_stage_overlay

case "$1" in
	sync)
		exit 0
		;;
	*defconfig|savedefconfig)
		printf '%s\n' 'TDVP SDK build: profile configuration is performed internally; invoke this script without a defconfig target' >&2
		exit 2
		;;
esac

	configure_buildroot_output
	verify_product_config
fi
if [ "$image_rebuild_mode" = "1" ]; then
	[ "$incremental_mode" = "1" ] || {
		printf '%s\n' 'TDVP SDK build: TDVP_IMAGE_REBUILD=1 requires TDVP_INCREMENTAL=1' >&2
		exit 2
	}
	for target in "$@"; do
		case "$target" in
			all)
				;;
			*)
				printf '%s\n' 'TDVP SDK build: TDVP_IMAGE_REBUILD=1 only supports the complete image target' >&2
				exit 2
				;;
		esac
	done
	# These are generated files under the declared output directory. Removing
	# them forces Buildroot to serialize the current target tree again, while
	# retaining the already built package and toolchain artifacts.
	rm -f \
		"$OUTPUT_DIR/images/rootfs.ext2" \
		"$OUTPUT_DIR/images/rootfs.ext2.gz" \
		"$OUTPUT_DIR/images/rootfs.ext4" \
		"$OUTPUT_DIR/images/sysimage-sdcard.img" \
		"$OUTPUT_DIR/images/sysimage-sdcard.img.gz" \
		"$OUTPUT_DIR/images/sysimage-sdcard.img.sha256" \
		"$OUTPUT_DIR/images/sysimage-sdcard.img.gz.sha256"
	printf '%s\n' 'TDVP SDK build: rebuilding rootfs and SD image from the staged target tree'
fi

if [ "$product_inputs_changed" = "1" ] && [ "$image_rebuild_mode" != "1" ]; then
	# Local-site packages are copied into Buildroot's package build directory.
	# Force a focused resync/reconfigure whenever the staged TDVP input digest
	# changes. This keeps incremental builds reproducible without rebuilding the
	# vendor toolchain or unrelated SDK packages.
	product_packages=(
		gtk-layer-shell
		labwc
		libfm-extra
		libmenu-cache
	libfm
	pcmanfm
		libcanberra
		sound-theme-freedesktop
		alsa-utils
		opkg
	gnupg2
	nm-connection-editor
	tdvp-opkg-trust
	wf-panel-pi
		libnma
		wfplug-batt
		wfplug-menu
		wfplug-clock
		wfplug-netman
		wfplug-power
		wfplug-volumepulse
		wofi
		wvkbd
		mc
		mpv
		v4l2grab
		yavta
		sudo
		tdvp-greetd
		tdvp-gtkgreet
		tdvp-greeter
		tdvp-kpu-acceptance
		tdvp-labwc-desktop
		vicliu-pocket-linux-desktop
		vicliu-pocket-linux-hardware
		tdvp-display-smoke
		tdvp-keyboard-layout
		tdvp-wayland-acceptance
	)
	packages_to_clean=()
	clean_all_product_packages=0

	for target in "$@"; do
		case "$target" in
			all|image|images)
				clean_all_product_packages=1
				;;
			*)
				matched_package=0
				for package in "${product_packages[@]}"; do
					case "$target" in
						"$package"|"$package"-*)
							packages_to_clean+=("$package")
							matched_package=1
							;;
					esac
				done
				# A kernel, toolchain or other vendor target has no local product
				# package to clean. Leave existing product package outputs intact.
				;;
		esac
	done

	if [ "$clean_all_product_packages" -eq 1 ]; then
		packages_to_clean=("${product_packages[@]}")
	fi

	for package in "${packages_to_clean[@]}"; do
		run_buildroot_target "${package}-dirclean"
	done
elif [ "$product_inputs_changed" = "1" ]; then
	printf '%s\n' 'TDVP SDK build: preserving existing package outputs for explicit image-only rebuild'
fi
# The vendor vvcam recipe also emits a Debian-format camera bundle beside the
# SD-card artifacts, but does not create its output directory. Keep this
# vendor side artifact directory available for every configured build.
mkdir -p "$OUTPUT_DIR/images/deb"

"${BUILD_ENV[@]}" make -j"${build_jobs}" -C "$OUTPUT_DIR" "$@"

for target in "$@"; do
	case "$target" in
		all|image|images)
			verify_product_config
			bash "$SCRIPT_DIR/verify-k230-hardware-artifacts.sh" "$OUTPUT_DIR"
			# The package-level hardware audit is insufficient on its own: local
			# package sources are copied into the target filesystem before the
			# final SD-card image is assembled. Verify the resulting raw image and
			# both ext4 payloads before marking this input set reusable.
			bash "$OVERLAY_DIR/board/tdvp/verify-sdcard-image.sh" "$OUTPUT_DIR/images"
			break
			;;
	esac
done

# A failed image audit must never make a future incremental invocation believe
# that the current product inputs were successfully built.  In particular, a
# local package can have been staged correctly while an older target-tree copy
# still made it into the image.  Only record the input digest after all image
# validation has succeeded.
for target in "$@"; do
	case "$target" in
		all|image|images)
			printf '%s\n' "$build_input_digest" > "$BUILD_INPUT_STAMP"
			break
			;;
	esac
done

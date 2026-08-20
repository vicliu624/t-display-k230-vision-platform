#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  verify-sdcard-image.sh <images-directory>

Verify the active K230 Linux SDK TDVP image contract. This guard checks the
SDK's fixed raw-image offsets, both ext4 filesystems, the RM69A10 boot payload,
the exact kernel and DTB bytes copied into the boot filesystem, the systemd
service graph, graphical-session components, OpenSSH, and Wi-Fi tooling. The
physical LCD cold-boot gate follows this host-side release check.
EOF
}

if [ "$#" -ne 1 ]; then
	usage
	exit 2
fi

BINARIES_DIR="$(cd "$1" && pwd)"
SYSIMAGE="${BINARIES_DIR}/sysimage-sdcard.img"
SPL="${BINARIES_DIR}/uboot/fn_u-boot-spl.bin"
UBOOT_ENV="${BINARIES_DIR}/uboot/env.env"
UBOOT="${BINARIES_DIR}/uboot/fn_ug_u-boot.bin"
BOOTFS="${BINARIES_DIR}/boot.ext4"
# BR2_TARGET_ROOTFS_EXT2_4 creates an ext4 filesystem. The SDK artifact is
# named rootfs.ext2, and the packaging flow uses an ext4 filesystem image.
ROOTFS="${BINARIES_DIR}/rootfs.ext2"
SELECTED_DTB="${BINARIES_DIR}/k230-canmv-rm69a10.dtb"
BUILDROOT_HOST_DIR="${HOST_DIR:-$(dirname "${BINARIES_DIR}")/host}"
if [ -x "${BUILDROOT_HOST_DIR}/sbin/e2fsck" ]; then
	E2FSCK="${BUILDROOT_HOST_DIR}/sbin/e2fsck"
else
	E2FSCK="$(command -v fsck.ext4)"
fi
if [ -x "${BUILDROOT_HOST_DIR}/sbin/dumpe2fs" ]; then
	DUMPE2FS="${BUILDROOT_HOST_DIR}/sbin/dumpe2fs"
else
	DUMPE2FS="$(command -v dumpe2fs)"
fi
SGDISK="$(command -v sgdisk || true)"

ROOTFS_UUID="6ed17b77-cd22-52f2-ae36-1fdbe5d476b7"
ROOTFS_HASH_SEED="4cf1e2c3-fd06-5a49-a511-89041a8a1b98"
BOOTFS_UUID="22c75a54-84db-52be-a5f1-69bf90788d29"
BOOTFS_HASH_SEED="5305a011-7207-56c7-b4bf-ae5a1fc2f135"
GPT_DISK_UUID="FBB0B6A4-C36F-5F5C-8C42-077FFBA1377E"
GPT_BOOT_UUID="900E1751-E943-5DAA-A4EF-68831D3ED855"
GPT_ROOTFS_UUID="C9CC7F55-7FD2-5D64-AE97-0715ADF47FDE"
for file in "${SYSIMAGE}" "${SPL}" "${UBOOT_ENV}" "${UBOOT}" "${BOOTFS}" "${ROOTFS}" "${SELECTED_DTB}"; do
	if [ ! -s "${file}" ]; then
		printf 'TDVP image guard: missing required artifact: %s\n' "${file}" >&2
		exit 1
	fi
done

compare_at() {
	local label="$1"
	local offset="$(( $2 ))"
	local expected="$3"
	local size

	size="$(stat -c '%s' "${expected}")"
	if [ "$((offset + size))" -gt "$(stat -c '%s' "${SYSIMAGE}")" ]; then
		printf 'TDVP image guard: %s exceeds image bounds\n' "${label}" >&2
		exit 1
	fi

	if ! dd if="${SYSIMAGE}" bs=4M iflag=skip_bytes,count_bytes \
		skip="${offset}" count="${size}" status=none | cmp -s - "${expected}"; then
		printf 'TDVP image guard: byte mismatch for %s at 0x%x\n' \
			"${label}" "${offset}" >&2
		exit 1
	fi
}

compare_range_at() {
	local label="$1"
	local image_offset="$(( $2 ))"
	local expected="$3"
	local expected_offset="$(( $4 ))"
	local size="$(( $5 ))"

	if [ "$((image_offset + size))" -gt "$(stat -c '%s' "${SYSIMAGE}")" ] || \
		[ "$((expected_offset + size))" -gt "$(stat -c '%s' "${expected}")" ]; then
		printf 'TDVP image guard: %s sample exceeds image bounds\n' "${label}" >&2
		exit 1
	fi

	if ! cmp -s \
		<(dd if="${SYSIMAGE}" bs=4M iflag=skip_bytes,count_bytes \
			skip="${image_offset}" count="${size}" status=none) \
		<(dd if="${expected}" bs=4M iflag=skip_bytes,count_bytes \
			skip="${expected_offset}" count="${size}" status=none); then
		printf 'TDVP image guard: byte mismatch for %s\n' "${label}" >&2
		exit 1
	fi
}

compare_large_partition_at() {
	local label="$1"
	local partition_offset="$(( $2 ))"
	local expected="$3"
	local size
	local sample_size=$((4 * 1024 * 1024))
	local position
	local -a positions

	size="$(stat -c '%s' "${expected}")"
	if [ "${TDVP_FULL_ROOTFS_IMAGE_COMPARE:-0}" = "1" ]; then
		printf 'TDVP image guard: full raw comparison for %s\n' "${label}"
		compare_at "${label}" "${partition_offset}" "${expected}"
		return
	fi

	# The ext4 image itself is checked in full below.  A complete byte-for-byte
	# comparison of its 1.5 GiB copy inside sysimage takes more than an hour
	# through WSL's NTFS bridge, making a release guard impractical. Verify the
	# raw placement through the ext4 header, tail, and stable distributed samples
	# instead. Set TDVP_FULL_ROOTFS_IMAGE_COMPARE=1 for an exhaustive CI audit.
	if [ "${size}" -lt "${sample_size}" ]; then
		compare_at "${label}" "${partition_offset}" "${expected}"
		return
	fi

	positions=(0 1024 $((size / 4)) $((size / 2)) $((size * 3 / 4)) $((size - sample_size)))
	printf 'TDVP image guard: sampled raw comparison for %s (%s MiB samples)\n' \
		"${label}" "$((sample_size / 1024 / 1024))"
	for position in "${positions[@]}"; do
		if [ "${position}" -gt "$((size - sample_size))" ]; then
			position="$((size - sample_size))"
		fi
		compare_range_at "${label} at +${position}" \
			"$((partition_offset + position))" "${expected}" "${position}" "${sample_size}"
	done
}

require_fs_path() {
	local filesystem="$1"
	local path="$2"

	if ! debugfs -R "stat ${path}" "${filesystem}" 2>/dev/null | grep -q '^Inode:'; then
		printf 'TDVP image guard: filesystem is missing: %s\n' "${path}" >&2
		exit 1
	fi
}

require_fs_symlink_target() {
	local filesystem="$1"
	local path="$2"
	local target="$3"
	local inode
	local link_target

	inode="$(debugfs -R "stat ${path}" "${filesystem}" 2>/dev/null || true)"
	# ext4 stores short links inline as "Fast link dest". Longer links use a
	# data block, which debugfs exposes through cat. Read both representations
	# so the guard validates the link target, not inode storage details.
	link_target="$(sed -n 's/^Fast link dest: "\(.*\)"$/\1/p' <<<"${inode}")"
	if [ -z "${link_target}" ]; then
		link_target="$(debugfs -R "cat ${path}" "${filesystem}" 2>/dev/null || true)"
	fi
	if ! grep -q 'Type: symlink' <<<"${inode}" || \
		[ "${link_target}" != "${target}" ]; then
		printf 'TDVP image guard: %s is not the expected symlink to %s\n' \
			"${path}" "${target}" >&2
		exit 1
	fi
}

reject_fs_path() {
	local filesystem="$1"
	local path="$2"

	if debugfs -R "stat ${path}" "${filesystem}" 2>/dev/null | grep -q '^Inode:'; then
		printf 'TDVP image guard: filesystem unexpectedly contains: %s\n' "${path}" >&2
		exit 1
	fi
}

require_rootfs_line() {
	local path="$1"
	local pattern="$2"

	if ! debugfs -R "cat ${path}" "${ROOTFS}" 2>/dev/null | grep -Eq -- "${pattern}"; then
		printf 'TDVP image guard: rootfs %s does not contain: %s\n' \
			"${path}" "${pattern}" >&2
		exit 1
	fi
}

require_rootfs_fixed_line() {
	local path="$1"
	local text="$2"

	if ! debugfs -R "cat ${path}" "${ROOTFS}" 2>/dev/null | grep -Fqx -- "${text}"; then
		printf 'TDVP image guard: rootfs %s does not contain the required line\n' \
			"${path}" >&2
		exit 1
	fi
}

require_rootfs_content() {
	local path="$1"
	local text="$2"

	if ! debugfs -R "cat ${path}" "${ROOTFS}" 2>/dev/null | grep -aFq -- "${text}"; then
		printf 'TDVP image guard: rootfs %s does not contain required content: %s\n' \
			"${path}" "${text}" >&2
		exit 1
	fi
}

compare_bootfs_payload() {
	local path="$1"
	local expected="$2"
	local label="$3"
	local temporary_dir
	local temporary_file

	temporary_dir="$(mktemp -d)"
	temporary_file="${temporary_dir}/payload"
	if ! debugfs -R "dump ${path} ${temporary_file}" "${BOOTFS}" \
		>/dev/null 2>&1 || ! cmp -s "${temporary_file}" "${expected}"; then
		rm -f "${temporary_file}"
		rmdir "${temporary_dir}" 2>/dev/null || true
		printf 'TDVP image guard: boot filesystem payload mismatch: %s\n' \
			"${label}" >&2
		exit 1
	fi
	rm -f "${temporary_file}"
	rmdir "${temporary_dir}"
}

check_filesystem() {
	local label="$1"
	local filesystem="$2"

	if ! "${E2FSCK}" -fn "${filesystem}" >/dev/null; then
		printf 'TDVP image guard: %s filesystem check failed\n' "${label}" >&2
		exit 1
	fi
}

check_filesystem_identity() {
	local label="$1"
	local filesystem="$2"
	local uuid="$3"
	local hash_seed="$4"
	local header

	header="$("${DUMPE2FS}" -h "${filesystem}" 2>/dev/null)"
	if ! grep -Eq "^Filesystem UUID:[[:space:]]+${uuid}$" <<<"${header}"; then
		printf 'TDVP image guard: %s UUID is not deterministic: expected %s\n' \
			"${label}" "${uuid}" >&2
		exit 1
	fi
	if ! grep -Eq "^Directory Hash Seed:[[:space:]]+${hash_seed}$" <<<"${header}"; then
		printf 'TDVP image guard: %s directory hash seed is not deterministic: expected %s\n' \
			"${label}" "${hash_seed}" >&2
		exit 1
	fi
}

check_gpt_identity() {
	local disk_info
	local boot_info
	local rootfs_info

	if [ -z "${SGDISK}" ]; then
		printf '%s\n' 'TDVP image guard: host sgdisk is required to verify GPT identities' >&2
		exit 1
	fi
	disk_info="$("${SGDISK}" -p "${SYSIMAGE}")"
	boot_info="$("${SGDISK}" -i 1 "${SYSIMAGE}")"
	rootfs_info="$("${SGDISK}" -i 2 "${SYSIMAGE}")"
	if ! grep -Fq "Disk identifier (GUID): ${GPT_DISK_UUID}" <<<"${disk_info}" || \
		! grep -Fq "Partition unique GUID: ${GPT_BOOT_UUID}" <<<"${boot_info}" || \
		! grep -Fq "Partition unique GUID: ${GPT_ROOTFS_UUID}" <<<"${rootfs_info}"; then
		printf '%s\n' 'TDVP image guard: GPT UUID contract does not match' >&2
		exit 1
	fi
}

if ! dd if="${SYSIMAGE}" bs=1 skip=512 count=8 status=none | cmp -s - <(printf 'EFI PART'); then
	echo 'TDVP image guard: primary GPT signature is missing' >&2
	exit 1
fi

# These offsets are fixed by the pinned K230 Linux SDK genimage.cfg.
compare_at 'SPL copy 1' 0x100000 "${SPL}"
compare_at 'SPL copy 2' 0x180000 "${SPL}"
compare_at 'U-Boot environment copy 1' 0x1e0000 "${UBOOT_ENV}"
compare_at 'U-Boot image' 0x200000 "${UBOOT}"
compare_at 'U-Boot environment copy 2' 0x380000 "${UBOOT_ENV}"
compare_at 'boot ext4 partition' $((30 * 1024 * 1024)) "${BOOTFS}"
compare_large_partition_at 'rootfs filesystem partition' $((128 * 1024 * 1024)) "${ROOTFS}"

require_fs_path "${BOOTFS}" '/fw_jump_add_uboot_head.bin'
require_fs_path "${BOOTFS}" '/Image'
require_fs_path "${BOOTFS}" '/k.dtb'
require_fs_path "${BOOTFS}" '/k230-canmv-rm69a10.dtb'
compare_bootfs_payload '/Image' "${BINARIES_DIR}/Image" 'Linux Image'
compare_bootfs_payload '/k230-canmv-rm69a10.dtb' "${SELECTED_DTB}" 'RM69A10 DTB'

require_fs_path "${ROOTFS}" '/usr/lib/systemd/systemd'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/sshd.service'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/systemd-networkd.service'
require_fs_path "${ROOTFS}" '/usr/bin/seatd'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/seatd.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/sshd.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/systemd-networkd.service'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/wpa_supplicant@.service'
require_rootfs_fixed_line '/usr/lib/systemd/system/wpa_supplicant@.service' 'ExecStart=/usr/sbin/wpa_supplicant -i %i -c /etc/wpa_supplicant/wpa_supplicant-%i.conf'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/sshd.service' '../../../../usr/lib/systemd/system/sshd.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/systemd-networkd.service' '../../../../usr/lib/systemd/system/systemd-networkd.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service' '../../../../usr/lib/systemd/system/wpa_supplicant@.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/wpa_supplicant.service'
require_fs_path "${ROOTFS}" '/etc/systemd/network/20-wired.network'
require_fs_path "${ROOTFS}" '/etc/systemd/network/30-wifi.network'
require_rootfs_fixed_line '/etc/systemd/network/30-wifi.network' 'Name=wlan0'
require_rootfs_fixed_line '/etc/systemd/network/30-wifi.network' 'DHCP=yes'
require_rootfs_fixed_line '/etc/systemd/network/30-wifi.network' 'RouteMetric=100'
require_rootfs_fixed_line '/etc/systemd/network/20-wired.network' 'RouteMetric=600'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/seatd.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/greetd.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/seatd.service' '../../../../usr/lib/systemd/system/seatd.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/greetd.service' '../../../../usr/lib/systemd/system/greetd.service'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/tdvp-kpu-acceptance.service'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-kpu-smoke'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-kpu-acceptance.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-kpu-acceptance.service' '../../../../usr/lib/systemd/system/tdvp-kpu-acceptance.service'
reject_fs_path "${ROOTFS}" '/etc/tdvp/kpu/acceptance.enabled'
require_rootfs_content '/usr/local/bin/tdvp-kpu-smoke' 'TDVP KPU acceptance: skipped'
require_rootfs_content '/usr/local/bin/tdvp-kpu-smoke' 'acceptance.enabled'
require_rootfs_fixed_line '/usr/lib/systemd/system/tdvp-rtc-restore.service' 'ConditionPathExists=/dev/rtc0'
if debugfs -R 'cat /usr/lib/systemd/system/tdvp-rtc-restore.service' "${ROOTFS}" 2>/dev/null | \
	grep -Eq '^(Wants|Requires|After)=dev-rtc0\.device'; then
	printf '%s\n' 'TDVP image guard: optional RTC restore service waits for dev-rtc0.device' >&2
	exit 1
fi
require_rootfs_fixed_line '/usr/lib/systemd/system/tdvp-rtc-restore.service' 'SuccessExitStatus=1'
require_rootfs_fixed_line '/usr/lib/systemd/system/tdvp-rtc-writeback.service' 'SuccessExitStatus=1'
# Time is refreshed by a timer after network synchronization.  A device path
# watcher caused a second boot-time dependency path on boards with an RTC that
# initially reports no valid calendar value.
reject_fs_path "${ROOTFS}" '/usr/lib/systemd/system/tdvp-rtc-writeback.path'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-rtc-writeback.path'
# The graphical login and authenticated desktop session each own their process
# lifecycle.  The rootfs must contain only the current session entry points.
for retired_desktop_path in \
	'/usr/local/bin/vpl-shell-session' \
	'/usr/local/bin/vpl-os-labwc-session' \
	'/usr/local/bin/vpl-os-greeter-session' \
	'/usr/local/bin/vpl-shell' \
	'/etc/init.d/S40k230_canmv_t_display_rm69a10_shell_defconfig'; do
	reject_fs_path "${ROOTFS}" "${retired_desktop_path}"
done
require_fs_path "${ROOTFS}" '/usr/sbin/sshd'
require_fs_path "${ROOTFS}" '/usr/bin/ssh-keygen'
require_fs_path "${ROOTFS}" '/usr/sbin/wpa_supplicant'
require_fs_path "${ROOTFS}" '/etc/wpa_supplicant/wpa_supplicant-wlan0.conf'
require_rootfs_fixed_line '/etc/wpa_supplicant/wpa_supplicant-wlan0.conf' 'ctrl_interface=/run/wpa_supplicant'
require_rootfs_fixed_line '/etc/wpa_supplicant/wpa_supplicant-wlan0.conf' 'update_config=1'
require_rootfs_fixed_line '/etc/wpa_supplicant/wpa_supplicant-wlan0.conf' 'country=CN'
require_fs_path "${ROOTFS}" '/usr/bin/opkg'
require_fs_path "${ROOTFS}" '/usr/bin/curl'
require_fs_path "${ROOTFS}" '/usr/bin/openssl'
require_fs_path "${ROOTFS}" '/usr/bin/gpiodetect'
require_fs_path "${ROOTFS}" '/usr/bin/gpioinfo'
require_fs_path "${ROOTFS}" '/usr/bin/gpioget'
require_fs_path "${ROOTFS}" '/usr/bin/gpioset'
require_fs_path "${ROOTFS}" '/usr/bin/gpiomon'
require_fs_path "${ROOTFS}" '/usr/bin/evtest'
require_fs_path "${ROOTFS}" '/usr/sbin/i2cdetect'
require_fs_path "${ROOTFS}" '/usr/sbin/i2cget'
require_fs_path "${ROOTFS}" '/usr/sbin/i2cset'
require_fs_path "${ROOTFS}" '/usr/bin/arecord'
require_fs_path "${ROOTFS}" '/usr/sbin/ethtool'
require_fs_path "${ROOTFS}" '/usr/sbin/iw'
require_fs_path "${ROOTFS}" '/etc/opkg/opkg.conf'
require_fs_path "${ROOTFS}" '/usr/lib/opkg/status'
require_fs_path "${ROOTFS}" '/usr/bin/labwc'
require_fs_path "${ROOTFS}" '/usr/bin/sfwbar'
require_fs_path "${ROOTFS}" '/usr/bin/swaybg'
require_fs_path "${ROOTFS}" '/usr/bin/foot'
require_fs_path "${ROOTFS}" '/usr/bin/wofi'
require_fs_path "${ROOTFS}" '/usr/bin/wvkbd-mobintl'
require_fs_path "${ROOTFS}" '/usr/bin/wvkbd-mobintl'
require_fs_path "${ROOTFS}" '/usr/bin/mc'
require_fs_path "${ROOTFS}" '/usr/bin/mpv'
require_fs_path "${ROOTFS}" '/usr/bin/v4l2grab'
require_fs_path "${ROOTFS}" '/usr/bin/yavta'
require_fs_path "${ROOTFS}" '/usr/bin/sudo'
require_fs_path "${ROOTFS}" '/usr/bin/tdvp-wayland-acceptance'
require_fs_path "${ROOTFS}" '/usr/bin/wlr-randr'
require_fs_path "${ROOTFS}" '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
require_fs_path "${ROOTFS}" '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
require_fs_path "${ROOTFS}" '/usr/bin/greetd'
require_fs_path "${ROOTFS}" '/usr/bin/gtkgreet'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/greetd.service'
require_fs_path "${ROOTFS}" '/etc/greetd/config.toml'
require_fs_path "${ROOTFS}" '/etc/greetd/gtkgreet.css'
require_fs_path "${ROOTFS}" '/etc/pam.d/greetd'
require_fs_path "${ROOTFS}" '/etc/pam.d/greetd-greeter'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-greeter-session'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-greeter-labwc'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-labwc-session'
require_fs_path "${ROOTFS}" '/etc/tdvp/labwc/environment'
require_fs_path "${ROOTFS}" '/etc/xdg/labwc/autostart'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-sfwbar-session'
require_fs_path "${ROOTFS}" '/etc/xdg/labwc/rc.xml'
require_fs_path "${ROOTFS}" '/etc/sfwbar/sfwbar.config'
require_fs_path "${ROOTFS}" '/usr/share/sfwbar/tdvp-launcher.widget'
require_fs_path "${ROOTFS}" '/etc/xdg/foot/foot.ini'
require_fs_path "${ROOTFS}" '/usr/share/backgrounds/tdvp-pda-paper.svg'
require_fs_path "${ROOTFS}" '/usr/share/applications/foot.desktop'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-app-launcher'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-audio-menu'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-camera'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-display-menu'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-files'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-logs'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-osk'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-power-menu'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-wifi'
require_fs_path "${ROOTFS}" '/usr/local/libexec/vpl-desktopctl'
require_fs_path "${ROOTFS}" '/etc/sudoers.d/vpl-desktop'
require_fs_path "${ROOTFS}" '/etc/xdg/wofi/config'
require_fs_path "${ROOTFS}" '/etc/xdg/wofi/style.css'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-audio.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-camera.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-display.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-files.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-logs.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-osk.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-power.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-wifi.desktop'
require_fs_path "${ROOTFS}" '/etc/udev/rules.d/70-tdvp-touch.rules'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/tdvp-keyboard-layout.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-keyboard-layout.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-keyboard-layout.service' '../../../../usr/lib/systemd/system/tdvp-keyboard-layout.service'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/vicliu-pocket-linux-hardware.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/vicliu-pocket-linux-hardware.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/vicliu-pocket-linux-hardware.service' '../../../../usr/lib/systemd/system/vicliu-pocket-linux-hardware.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/sysinit.target.wants/tdvp-rtc-load.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/sysinit.target.wants/tdvp-rtc-restore.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/timers.target.wants/tdvp-rtc-writeback.timer'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/sysinit.target.wants/tdvp-rtc-load.service' '../../../../usr/lib/systemd/system/tdvp-rtc-load.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/sysinit.target.wants/tdvp-rtc-restore.service' '../../../../usr/lib/systemd/system/tdvp-rtc-restore.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/timers.target.wants/tdvp-rtc-writeback.timer' '../../../../usr/lib/systemd/system/tdvp-rtc-writeback.timer'
require_rootfs_content '/usr/lib/systemd/system/greetd.service' 'tdvp-keyboard-layout.service'
require_rootfs_line '/etc/ssh/sshd_config' '^PermitRootLogin[[:space:]]+yes$'
require_rootfs_line '/etc/opkg/opkg.conf' '^dest root /$'
require_rootfs_line '/etc/opkg/opkg.conf' '^lists_dir /var/lib/opkg/lists$'
require_rootfs_line '/etc/opkg/opkg.conf' '^arch all 1$'
require_rootfs_line '/etc/opkg/opkg.conf' '^arch noarch 1$'
require_rootfs_line '/etc/opkg/opkg.conf' '^arch riscv64 10$'
require_rootfs_line '/etc/opkg/opkg.conf' '^src/gz tdvp_apps_r1 https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/riscv64$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^WLR_BACKENDS=drm,libinput$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^WLR_DRM_DEVICES=/dev/dri/card0$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^WLR_RENDERER=pixman$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^WLR_DRM_NO_MODIFIERS=1$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^WLR_RENDERER_ALLOW_SOFTWARE=1$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^LIBSEAT_BACKEND=seatd$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^TDVP_K230_OUTPUT=DSI-1$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^TDVP_K230_OUTPUT_TRANSFORM=90$'
reject_fs_path "${ROOTFS}" '/usr/lib/systemd/system/tdvp-labwc-desktop.service'
reject_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-labwc-desktop-session'
require_rootfs_line '/etc/greetd/config.toml' '^command = "/usr/local/bin/tdvp-greeter-session"$'
require_rootfs_line '/etc/greetd/config.toml' '^user = "greeter"$'
require_rootfs_content '/etc/pam.d/greetd' 'session    required   pam_unix.so'
require_rootfs_content '/etc/pam.d/greetd-greeter' 'session    required   pam_unix.so'
require_rootfs_content '/usr/local/bin/tdvp-greeter-session' 'set -a'
require_rootfs_content '/usr/local/bin/tdvp-greeter-session' 'XDG_RUNTIME_DIR="${HOME}/.cache/wayland-runtime"'
require_rootfs_content '/usr/local/bin/tdvp-greeter-labwc' '--transform "${TDVP_K230_OUTPUT_TRANSFORM}"'
  require_rootfs_content '/usr/local/bin/tdvp-labwc-session' 'exec /usr/bin/dbus-run-session -- /usr/bin/labwc'
  require_rootfs_content '/etc/xdg/labwc/autostart' 'tdvp-pda-paper.svg -m fill'
  require_rootfs_content '/etc/xdg/labwc/autostart' '/usr/local/bin/tdvp-sfwbar-session &'
  require_rootfs_content '/usr/local/bin/tdvp-sfwbar-session' 'while :; do'
  require_rootfs_content '/usr/local/bin/tdvp-sfwbar-session' '/usr/bin/sfwbar -f /etc/sfwbar/sfwbar.config'
  require_rootfs_content '/etc/sfwbar/sfwbar.config' 'widget "tdvp-launcher.widget"'
require_rootfs_content '/usr/share/sfwbar/tdvp-launcher.widget' 'action = Exec("/usr/local/bin/vpl-app-launcher")'
require_rootfs_content '/etc/sfwbar/sfwbar.config' 'taskbar {'
require_rootfs_content '/etc/sfwbar/sfwbar.config' 'Set ThicknessHint = "70px"'
require_rootfs_content '/etc/sfwbar/sfwbar.config' 'box-shadow: inset 0 -6px #e66a2c;'
require_rootfs_content '/etc/sfwbar/sfwbar.config' 'include "network.widget"'
require_rootfs_content '/etc/sfwbar/sfwbar.config' 'widget "network.widget"'
require_rootfs_content '/etc/xdg/labwc/rc.xml' '<default />'
require_rootfs_content '/etc/xdg/labwc/rc.xml' '<keybind key="Super_L" onRelease="yes">'
require_rootfs_content '/etc/xdg/labwc/rc.xml' 'command="/usr/local/bin/vpl-app-launcher"'
require_rootfs_content '/etc/xdg/labwc/rc.xml' '<mouseEmulation>yes</mouseEmulation>'
if debugfs -R 'cat /etc/xdg/labwc/rc.xml' "${ROOTFS}" 2>/dev/null | grep -Fq '<mapToOutput>'; then
	printf '%s\n' 'TDVP image guard: desktop Labwc touch configuration must not remap the output' >&2
	exit 1
fi
require_rootfs_content '/usr/local/bin/vpl-app-launcher' '/usr/bin/pidof wofi'
require_rootfs_content '/usr/local/bin/vpl-app-launcher' '[ -x /usr/bin/wofi ] || exit 1'
require_rootfs_content '/usr/local/bin/vpl-app-launcher' 'exec /usr/bin/wofi --show drun'
if debugfs -R 'cat /usr/local/bin/vpl-app-launcher' "${ROOTFS}" 2>/dev/null | grep -Fq 'exec /usr/bin/foot'; then
	printf '%s\n' 'TDVP image guard: application launcher must not fall back to foot' >&2
	exit 1
fi
require_rootfs_content '/etc/tdvp/greetd/labwc/rc.xml' '<mouseEmulation>yes</mouseEmulation>'
require_rootfs_content '/etc/xdg/foot/foot.ini' 'initial-window-mode=maximized'
require_rootfs_content '/etc/greetd/gtkgreet.css' 'min-width: 740px;'
require_rootfs_content '/etc/greetd/gtkgreet.css' 'min-height: 62px;'
require_rootfs_content '/etc/greetd/gtkgreet.css' 'background-size: 14px 14px;'
require_rootfs_line '/etc/udev/rules.d/70-tdvp-touch.rules' 'ENV{LIBINPUT_CALIBRATION_MATRIX}="0 -1 1 1 0 0"'
require_rootfs_line '/etc/group' '^seat:x:'
require_rootfs_line '/etc/group' '^audio:.*:.*tdvp'
require_rootfs_line '/etc/tdvp/labwc/environment' '^XKB_DEFAULT_LAYOUT=us$'
require_rootfs_line '/etc/shadow' '^root:\$5\$tdvp-repro-2026\$oF1fY2harBx8EsEeKt\.jshl8qujlwVIvSW8pUxsnZID:'
require_rootfs_line '/etc/version/release_version' '^profile: k230_canmv_t_display_rm69a10_labwc_desktop_defconfig$'
require_rootfs_line '/etc/version/release_version' '^sdk_commit: 5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7$'
require_rootfs_line '/etc/version/release_version' '^linux_commit: 7d4e1f444f461dbe3833bd99a4640e7b6c2cd529$'
check_filesystem 'boot' "${BOOTFS}"
check_filesystem 'rootfs' "${ROOTFS}"
check_filesystem_identity 'boot' "${BOOTFS}" "${BOOTFS_UUID}" "${BOOTFS_HASH_SEED}"
check_filesystem_identity 'rootfs' "${ROOTFS}" "${ROOTFS_UUID}" "${ROOTFS_HASH_SEED}"
check_gpt_identity

echo 'TDVP SDK SD image contract: PASS'

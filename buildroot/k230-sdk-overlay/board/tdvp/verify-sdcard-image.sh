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
GPG="$(command -v gpg || true)"

ROOTFS_UUID="6ed17b77-cd22-52f2-ae36-1fdbe5d476b7"
ROOTFS_HASH_SEED="4cf1e2c3-fd06-5a49-a511-89041a8a1b98"
BOOTFS_UUID="22c75a54-84db-52be-a5f1-69bf90788d29"
BOOTFS_HASH_SEED="5305a011-7207-56c7-b4bf-ae5a1fc2f135"
GPT_DISK_UUID="FBB0B6A4-C36F-5F5C-8C42-077FFBA1377E"
GPT_BOOT_UUID="900E1751-E943-5DAA-A4EF-68831D3ED855"
GPT_ROOTFS_UUID="C9CC7F55-7FD2-5D64-AE97-0715ADF47FDE"
ROOTFS_BOOTARG="root=PARTUUID=${GPT_ROOTFS_UUID} loglevel=8 rw rootdelay=4 rootfstype=ext4 console=ttyS0,115200 earlycon=sbi"
for file in "${SYSIMAGE}" "${SPL}" "${UBOOT_ENV}" "${UBOOT}" "${BOOTFS}" "${ROOTFS}" "${SELECTED_DTB}"; do
	if [ ! -s "${file}" ]; then
		printf 'TDVP image guard: missing required artifact: %s\n' "${file}" >&2
		exit 1
	fi
done

# U-Boot must identify the root partition by its deterministic GPT UUID.  The
# Linux mmcblk index is not a stable board ABI: it changes when optional SDIO
# peripherals are present or probe in a different order.
if ! grep -aFq "${ROOTFS_BOOTARG}" "${UBOOT_ENV}"; then
	printf '%s\n' 'TDVP image guard: U-Boot environment does not use the rootfs PARTUUID boot argument' >&2
	exit 1
fi

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

require_fs_regular_file() {
	local filesystem="$1"
	local path="$2"

	if ! debugfs -R "stat ${path}" "${filesystem}" 2>/dev/null | \
		grep -q 'Type: regular'; then
		printf 'TDVP image guard: expected a regular executable: %s\n' "${path}" >&2
		exit 1
	fi
}

require_rootfs_gpg_fingerprint() {
	local filesystem="$1"
	local path="$2"
	local expected="$3"
	local key_file
	local gpg_home
	local actual

	if [ -z "${GPG}" ]; then
		printf '%s\n' 'TDVP image guard: host gpg is required to verify the embedded opkg public key' >&2
		exit 1
	fi
	key_file="$(mktemp)"
	gpg_home="$(mktemp -d)"
	if ! debugfs -R "dump ${path} ${key_file}" "${filesystem}" >/dev/null 2>&1; then
		rm -rf "${gpg_home}" "${key_file}"
		printf 'TDVP image guard: cannot extract public key from rootfs: %s\n' "${path}" >&2
		exit 1
	fi
	# --import-options show-only is supported by the GnuPG 2.2 host tool used
	# by the SDK build environment, unlike the newer --show-keys convenience
	# switch.  Use an ephemeral homedir so validation never changes a user's
	# keyring or trust database.
	if ! actual="$("${GPG}" --batch --no-tty --homedir "${gpg_home}" --with-colons \
		--import-options show-only --dry-run --import "${key_file}" 2>/dev/null | \
		awk -F: '$1 == "fpr" && !found { print toupper($10); found = 1 }')"; then
		rm -rf "${gpg_home}" "${key_file}"
		printf 'TDVP image guard: cannot parse embedded public key: %s\n' "${path}" >&2
		exit 1
	fi
	rm -rf "${gpg_home}" "${key_file}"
	if [ "${actual}" != "${expected}" ]; then
		printf 'TDVP image guard: %s fingerprint mismatch: expected %s, got %s\n' \
			"${path}" "${expected}" "${actual:-none}" >&2
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

reject_rootfs_line() {
	local path="$1"
	local pattern="$2"

	if debugfs -R "cat ${path}" "${ROOTFS}" 2>/dev/null | grep -Eq -- "${pattern}"; then
		printf 'TDVP image guard: rootfs %s unexpectedly contains: %s\n' \
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
	local temporary_dir
	local temporary_file

	# Do not pipe a binary file through grep -q under `set -o pipefail`.
	# grep exits as soon as it finds its match, which makes debugfs receive
	# SIGPIPE and turns a successful check into a false failure.  Dump first,
	# then inspect the complete file instead.
	temporary_dir="$(mktemp -d)"
	temporary_file="${temporary_dir}/rootfs-content"
	if ! debugfs -R "dump ${path} ${temporary_file}" "${ROOTFS}" >/dev/null 2>&1 || \
		! grep -aFq -- "${text}" "${temporary_file}"; then
		rm -rf "${temporary_dir}"
		printf 'TDVP image guard: rootfs %s does not contain required content: %s\n' \
			"${path}" "${text}" >&2
		exit 1
	fi
	rm -rf "${temporary_dir}"
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
for required_dtb_string in \
	'canaan,external-i2s-output-default' \
	'amp-shutdown-gpios' \
	'tdvp-amp-i2s-pins' \
	'tdvp-amp-shutdown-pin'; do
	if ! grep -aFq "${required_dtb_string}" "${SELECTED_DTB}"; then
		printf 'TDVP image guard: RM69A10 DTB is missing external speaker contract: %s\n' \
			"${required_dtb_string}" >&2
		exit 1
	fi
done

require_fs_path "${ROOTFS}" '/usr/lib/systemd/systemd'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/sshd.service'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/NetworkManager.service'
require_fs_path "${ROOTFS}" '/usr/bin/pulseaudio'
require_fs_path "${ROOTFS}" '/etc/pulse/default.pa.d'
require_fs_path "${ROOTFS}" '/usr/sbin/rfkill'
require_fs_path "${ROOTFS}" '/usr/bin/seatd'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/seatd.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/sshd.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/NetworkManager.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/sshd.service' '../../../../usr/lib/systemd/system/sshd.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/NetworkManager.service' '../../../../usr/lib/systemd/system/NetworkManager.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/pulseaudio.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/systemd-networkd.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/sockets.target.wants/systemd-networkd.socket'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/network-online.target.wants/systemd-networkd-wait-online.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/dbus-org.freedesktop.network1.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/systemd-networkd.service' '/dev/null'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/systemd-networkd.socket' '/dev/null'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/systemd-networkd-wait-online.service' '/dev/null'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/wpa_supplicant.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/wpa_supplicant@wlan0.service'
reject_fs_path "${ROOTFS}" '/etc/init.d/S40network'
reject_fs_path "${ROOTFS}" '/etc/network/interfaces'
reject_fs_path "${ROOTFS}" '/etc/wpa_supplicant/wpa_supplicant-wlan0.conf'
require_fs_path "${ROOTFS}" '/etc/NetworkManager/NetworkManager.conf'
require_rootfs_fixed_line '/etc/NetworkManager/NetworkManager.conf' 'plugins=keyfile'
require_rootfs_fixed_line '/etc/NetworkManager/NetworkManager.conf' 'wifi.scan-rand-mac-address=no'
require_fs_path "${ROOTFS}" '/usr/share/icons/Adwaita/scalable/status/nm-no-connection.svg'
require_fs_symlink_target "${ROOTFS}" '/usr/share/icons/Adwaita/scalable/status/nm-no-connection.svg' 'network-wireless-offline-symbolic.svg'
require_fs_symlink_target "${ROOTFS}" '/usr/share/icons/Adwaita/scalable/status/network-wireless-connected-100.svg' 'network-wireless-signal-excellent-symbolic.svg'
require_fs_symlink_target "${ROOTFS}" '/usr/share/icons/Adwaita/scalable/status/nm-signal-100.svg' 'network-wireless-signal-excellent-symbolic.svg'
require_fs_symlink_target "${ROOTFS}" '/usr/share/icons/Adwaita/scalable/status/nm-device-wired.svg' 'network-transmit-receive-symbolic.svg'
reject_fs_path "${ROOTFS}" '/usr/share/icons/Adwaita/icon-theme.cache'
require_fs_path "${ROOTFS}" '/etc/localtime'
require_fs_symlink_target "${ROOTFS}" '/etc/localtime' '/usr/share/zoneinfo/Asia/Shanghai'
require_rootfs_fixed_line '/etc/timezone' 'Asia/Shanghai'
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
# External I2S is the kernel DTS/ASoC default.  A userspace service previously
# raced card registration and made a non-critical policy retry look like a
# boot failure, so no audio-route service may sit in the login path.
reject_fs_path "${ROOTFS}" '/usr/lib/systemd/system/tdvp-external-audio.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-external-audio.service'
require_fs_regular_file "${ROOTFS}" '/usr/local/bin/tdvp-audio-route'
require_fs_regular_file "${ROOTFS}" '/usr/local/bin/tdvp-speaker-acceptance'
require_rootfs_content '/usr/local/bin/tdvp-audio-route' "readonly CONTROL='External I2S Output Switch'"
require_rootfs_content '/usr/local/bin/tdvp-audio-route' 'kernel ASoC machine driver owns the amplifier enable'
reject_rootfs_line '/usr/local/bin/tdvp-audio-route' 'gpioset'
require_rootfs_content '/usr/local/bin/tdvp-speaker-acceptance' 'confirm-audible'
require_fs_path "${ROOTFS}" '/usr/sbin/sfdisk'
require_fs_path "${ROOTFS}" '/usr/sbin/partprobe'
require_fs_path "${ROOTFS}" '/usr/sbin/blockdev'
require_rootfs_content '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-expand-rootfs' 'readonly SFDISK=/usr/sbin/sfdisk'
require_rootfs_content '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-expand-rootfs' 'PARTUUID=*'
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
require_fs_path "${ROOTFS}" '/usr/sbin/NetworkManager'
require_fs_path "${ROOTFS}" '/usr/bin/nmcli'
require_fs_path "${ROOTFS}" '/usr/bin/pulseaudio'
require_fs_path "${ROOTFS}" '/usr/bin/opkg'
require_fs_path "${ROOTFS}" '/usr/bin/gpg'
require_fs_path "${ROOTFS}" '/usr/bin/gpgv'
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
require_fs_path "${ROOTFS}" '/usr/bin/amixer'
require_fs_path "${ROOTFS}" '/usr/bin/speaker-test'
require_fs_path "${ROOTFS}" '/usr/sbin/ethtool'
require_fs_path "${ROOTFS}" '/usr/sbin/iw'
require_fs_path "${ROOTFS}" '/etc/opkg/opkg.conf'
require_fs_path "${ROOTFS}" '/etc/opkg/gpg'
require_fs_path "${ROOTFS}" '/etc/opkg/tdvp-feed.conf'
require_fs_path "${ROOTFS}" '/var/lib/opkg/lists'
require_fs_path "${ROOTFS}" '/var/lib/opkg/info'
require_fs_path "${ROOTFS}" '/var/lib/opkg/status'
require_rootfs_fixed_line '/etc/opkg/opkg.conf' 'option check_signature 1'
require_rootfs_fixed_line '/etc/opkg/opkg.conf' 'option signature_type gpg-asc'
require_rootfs_fixed_line '/etc/opkg/opkg.conf' 'option gpg_dir /etc/opkg/gpg'
require_rootfs_fixed_line '/etc/opkg/opkg.conf' 'option gpg_trust_level TrustAny'
require_rootfs_fixed_line '/etc/opkg/tdvp-feed.conf' 'src/gz tdvp_apps_r5 https://vicliu624.github.io/embedded-opkg-feed/feed/tdvp-k230-br2025.02.1-glibc2.33-rv64-lp64d-k6.6.36-r1/r5/riscv64'
reject_fs_path "${ROOTFS}" '/etc/opkg/tdvp-feed.conf.disabled'
require_fs_path "${ROOTFS}" '/usr/share/tdvp/opkg/tdvp-repo-public.asc'
require_rootfs_gpg_fingerprint "${ROOTFS}" '/usr/share/tdvp/opkg/tdvp-repo-public.asc' \
	'2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500'
require_fs_path "${ROOTFS}" '/usr/local/libexec/tdvp-opkg-bootstrap'
require_rootfs_content '/usr/local/libexec/tdvp-opkg-bootstrap' '2B091A2A8E5810954FB9FD64EA9D1CD5EFC81500'
require_fs_path "${ROOTFS}" '/usr/local/sbin/tdvp-opkg'
require_rootfs_content '/usr/local/sbin/tdvp-opkg' '/usr/local/libexec/tdvp-opkg-bootstrap'
require_rootfs_content '/usr/local/sbin/tdvp-opkg' 'exec /usr/bin/opkg'
require_fs_path "${ROOTFS}" '/etc/profile.d/tdvp-local-admin-path.sh'
require_rootfs_content '/etc/profile.d/tdvp-local-admin-path.sh' 'PATH=/usr/local/sbin:/usr/local/bin:'
require_rootfs_content '/etc/tdvp/labwc/environment' 'PATH=/usr/local/sbin:/usr/local/bin:'
require_rootfs_content '/usr/local/bin/tdvp-terminal' 'PATH=/usr/local/sbin:/usr/local/bin:'
reject_fs_path "${ROOTFS}" '/usr/lib/systemd/system/tdvp-opkg-trust.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-opkg-trust.service'
require_fs_path "${ROOTFS}" '/usr/bin/labwc'
require_fs_path "${ROOTFS}" '/usr/bin/wf-panel-pi'
require_fs_path "${ROOTFS}" '/usr/bin/pcmanfm'
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
require_fs_path "${ROOTFS}" '/usr/libexec/vicliu-pocket-linux-hardware/vpl-hardwared'
reject_fs_path "${ROOTFS}" '/usr/local/libexec/vicliu-pocket-linux-hardware/vpl-hardwared'
require_fs_path "${ROOTFS}" '/etc/tdvp/labwc/environment'
require_fs_path "${ROOTFS}" '/etc/xdg/labwc/autostart'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-wf-panel-session'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-pulseaudio-session'
require_fs_path "${ROOTFS}" '/usr/lib/tdvp-gdk-committed-compat.so'
reject_fs_path "${ROOTFS}" '/usr/local/lib/tdvp-gdk-committed-compat.so'
require_fs_path "${ROOTFS}" '/etc/tdvp/gsettings/org.rpi.nm-applet.gschema.xml'
require_fs_path "${ROOTFS}" '/etc/tdvp/gsettings/gschemas.compiled'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-pcmanfm-desktop-session'
require_fs_path "${ROOTFS}" '/etc/xdg/labwc/rc.xml'
require_fs_path "${ROOTFS}" '/etc/xdg/wf-panel-pi/wf-panel-pi.ini'
require_fs_path "${ROOTFS}" '/etc/wf-panel-pi/tdvp.css'
require_fs_path "${ROOTFS}" '/etc/xdg/pcmanfm/default/pcmanfm.conf'
require_fs_path "${ROOTFS}" '/etc/xdg/menus/lxde-applications.menu'
require_fs_path "${ROOTFS}" '/usr/lib/wf-panel-pi/libnetman.so'
require_fs_path "${ROOTFS}" '/usr/lib/wf-panel-pi/libvolumepulse.so'
require_fs_path "${ROOTFS}" '/usr/lib/wf-panel-pi/libbatt.so'
require_fs_path "${ROOTFS}" '/usr/lib/wf-panel-pi/libpower.so'
reject_fs_path "${ROOTFS}" '/usr/lib/wf-panel-pi/libtdvp-network.so'
reject_fs_path "${ROOTFS}" '/usr/lib/wf-panel-pi/libtdvp-volume.so'
reject_fs_path "${ROOTFS}" '/usr/lib/wf-panel-pi/libtdvp-power.so'
reject_fs_path "${ROOTFS}" '/usr/local/bin/vpl-files'
reject_fs_path "${ROOTFS}" '/usr/share/applications/vpl-files.desktop'
reject_fs_path "${ROOTFS}" '/usr/share/applications/pcmanfm.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/tdvp-pcmanfm.desktop'
require_rootfs_fixed_line '/usr/share/applications/tdvp-pcmanfm.desktop' 'Name=Files'
require_rootfs_fixed_line '/usr/share/applications/tdvp-pcmanfm.desktop' 'Exec=pcmanfm %U'
require_fs_path "${ROOTFS}" '/etc/xdg/foot/foot.ini'
require_fs_path "${ROOTFS}" '/usr/share/backgrounds/tdvp-pda-paper.svg'
require_fs_path "${ROOTFS}" '/usr/share/applications/foot.desktop'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-terminal'
require_rootfs_fixed_line '/usr/share/applications/foot.desktop' 'Exec=/usr/local/bin/tdvp-terminal'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-panel-menu'
require_fs_path "${ROOTFS}" '/usr/local/bin/tdvp-key-bridge'
require_fs_path "${ROOTFS}" '/usr/bin/nm-connection-editor'
require_fs_path "${ROOTFS}" '/usr/bin/lp-connection-editor'
require_fs_path "${ROOTFS}" '/usr/share/glib-2.0/schemas/org.gnome.nm-applet.gschema.xml'
require_fs_path "${ROOTFS}" '/usr/share/glib-2.0/schemas/gschemas.compiled'
require_fs_path "${ROOTFS}" '/usr/sbin/mke2fs'
# pgrep/pkill must be present as normal process-control commands.  The disk
# inspection and recovery tools must be util-linux binaries rather than
# BusyBox links with reduced large-media support.
require_fs_regular_file "${ROOTFS}" '/usr/bin/pgrep'
require_fs_regular_file "${ROOTFS}" '/usr/bin/pkill'
require_fs_regular_file "${ROOTFS}" '/usr/bin/findmnt'
require_fs_regular_file "${ROOTFS}" '/usr/sbin/fdisk'
require_fs_regular_file "${ROOTFS}" '/usr/sbin/sfdisk'
require_fs_path "${ROOTFS}" '/usr/lib/gio/modules/libgioopenssl.so'
require_fs_path "${ROOTFS}" '/usr/lib/libcanberra.so.0'
require_fs_path "${ROOTFS}" '/usr/share/sounds/freedesktop/stereo/audio-volume-change.oga'
require_fs_path "${ROOTFS}" '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-expand-rootfs'
require_fs_path "${ROOTFS}" '/usr/lib/systemd/system/tdvp-rootfs-expand.service'
require_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-rootfs-expand.service'
require_fs_symlink_target "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-rootfs-expand.service' '../../../../usr/lib/systemd/system/tdvp-rootfs-expand.service'
require_fs_path "${ROOTFS}" '/sbin/resize2fs'
reject_fs_path "${ROOTFS}" '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-provision-data'
reject_fs_path "${ROOTFS}" '/usr/lib/systemd/system/tdvp-data-storage.service'
reject_fs_path "${ROOTFS}" '/etc/systemd/system/multi-user.target.wants/tdvp-data-storage.service'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-camera'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-display-menu'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-logs'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-osk'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-package-manager'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-opkg-console'
require_fs_path "${ROOTFS}" '/usr/local/bin/vpl-power-menu'
reject_fs_path "${ROOTFS}" '/usr/local/bin/vpl-audio-menu'
reject_fs_path "${ROOTFS}" '/usr/local/bin/vpl-wifi'
require_fs_path "${ROOTFS}" '/usr/local/libexec/vpl-desktopctl'
require_rootfs_content '/usr/local/libexec/vpl-desktopctl' '/usr/bin/nmcli'
if debugfs -R 'cat /usr/local/libexec/vpl-desktopctl' "${ROOTFS}" 2>/dev/null | \
	grep -Eq '/etc/wpa_supplicant|systemd-networkd|wifi-connect|wifi-scan'; then
	printf '%s\n' 'TDVP image guard: desktop utility retains direct legacy Wi-Fi ownership' >&2
	exit 1
fi
require_fs_path "${ROOTFS}" '/etc/sudoers.d/vpl-desktop'
require_fs_path "${ROOTFS}" '/etc/xdg/wofi/config'
require_rootfs_content '/etc/xdg/wofi/config' 'location=top_left'
require_rootfs_content '/etc/xdg/wofi/config' 'width=440'
require_rootfs_content '/etc/xdg/wofi/config' 'yoffset=0'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-camera.desktop'
reject_fs_path "${ROOTFS}" '/usr/share/applications/vpl-browser.desktop'
reject_fs_path "${ROOTFS}" '/usr/local/bin/vpl-browser'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-display.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-logs.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-osk.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-package-manager.desktop'
require_fs_path "${ROOTFS}" '/usr/share/applications/vpl-power.desktop'
reject_fs_path "${ROOTFS}" '/usr/share/applications/vpl-audio.desktop'
reject_fs_path "${ROOTFS}" '/usr/share/applications/vpl-wifi.desktop'
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
require_rootfs_line '/etc/opkg/opkg.conf' '^option lists_dir /var/lib/opkg/lists$'
require_rootfs_line '/etc/opkg/opkg.conf' '^option info_dir /var/lib/opkg/info$'
require_rootfs_line '/etc/opkg/opkg.conf' '^option status_file /var/lib/opkg/status$'
require_rootfs_line '/etc/opkg/opkg.conf' '^arch all 1$'
require_rootfs_line '/etc/opkg/opkg.conf' '^arch noarch 1$'
require_rootfs_line '/etc/opkg/opkg.conf' '^arch riscv64 10$'
reject_rootfs_line '/etc/opkg/opkg.conf' '^src/gz '
require_rootfs_content '/var/lib/opkg/status' 'Package: tdvp-platform-abi'
require_rootfs_content '/var/lib/opkg/status' 'Version: 2025.02.1-k230.6.6.36-glibc2.33-rv64-lp64d-r1'
require_fs_path "${ROOTFS}" '/var/lib/opkg/info/tdvp-platform-abi.list'
require_rootfs_content '/usr/local/bin/vpl-package-manager' 'exec /usr/local/bin/tdvp-terminal'
require_rootfs_content '/usr/local/bin/vpl-opkg-console' 'TDVP Software Manager (opkg)'
require_rootfs_content '/usr/local/bin/vpl-opkg-console' 'sudo tdvp-opkg update'
require_rootfs_content '/etc/sudoers.d/vpl-desktop' '/usr/local/sbin/tdvp-opkg'
require_rootfs_content '/etc/sudoers.d/vpl-desktop' 'secure_path=/usr/local/sbin:/usr/local/bin:'
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
reject_fs_path "${ROOTFS}" '/usr/bin/sfwbar'
reject_fs_path "${ROOTFS}" '/usr/bin/swaybg'
reject_fs_path "${ROOTFS}" '/etc/sfwbar/sfwbar.config'
require_rootfs_line '/etc/greetd/config.toml' '^command = "/usr/local/bin/tdvp-greeter-session"$'
require_rootfs_line '/etc/greetd/config.toml' '^user = "greeter"$'
require_rootfs_content '/etc/pam.d/greetd' 'session    required   pam_unix.so'
require_rootfs_content '/etc/pam.d/greetd-greeter' 'session    required   pam_unix.so'
require_rootfs_content '/usr/local/bin/tdvp-greeter-session' 'set -a'
require_rootfs_content '/usr/local/bin/tdvp-greeter-session' 'XDG_RUNTIME_DIR="${HOME}/.cache/wayland-runtime"'
require_rootfs_content '/usr/local/bin/tdvp-greeter-labwc' '--transform "${TDVP_K230_OUTPUT_TRANSFORM}"'
  require_rootfs_content '/usr/local/bin/tdvp-labwc-session' 'exec /usr/bin/dbus-run-session -- /usr/bin/labwc'
  require_rootfs_content '/etc/xdg/labwc/autostart' '/usr/local/bin/tdvp-pcmanfm-desktop-session &'
  require_rootfs_content '/etc/xdg/labwc/autostart' '/usr/local/bin/tdvp-wf-panel-session &'
	require_rootfs_content '/usr/local/bin/tdvp-wf-panel-session' 'export GDK_BACKEND=wayland'
	require_rootfs_content '/usr/local/bin/tdvp-wf-panel-session' 'PATH=/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/sbin:/bin:/sbin'
	require_rootfs_content '/usr/local/bin/tdvp-wf-panel-session' '/usr/bin/wf-panel-pi --config /etc/xdg/wf-panel-pi/wf-panel-pi.ini'
  require_rootfs_content '/usr/local/bin/tdvp-pcmanfm-desktop-session' 'export GDK_BACKEND=wayland'
  require_rootfs_content '/usr/local/bin/tdvp-pcmanfm-desktop-session' '/usr/bin/pcmanfm --desktop'
if debugfs -R 'cat /usr/local/bin/tdvp-pcmanfm-desktop-session' "${ROOTFS}" 2>/dev/null | grep -Eq -- '(^|[[:space:]])--one-screen([[:space:]]|$)'; then
	printf '%s\n' 'TDVP image guard: PCManFM desktop mode must not disable monitor 0 with --one-screen' >&2
	exit 1
fi
require_rootfs_content '/etc/xdg/wf-panel-pi/wf-panel-pi.ini' 'widgets_left=smenu spacing8 window-list'
require_rootfs_content '/etc/xdg/wf-panel-pi/wf-panel-pi.ini' 'widgets_right=netman spacing4 volumepulse spacing4 batt spacing4 clock'
require_rootfs_content '/etc/xdg/wf-panel-pi/wf-panel-pi.ini' 'minimal_height=64'
require_rootfs_content '/etc/wf-panel-pi/tdvp.css' 'box-shadow: inset 0 -6px #e66a2c;'
require_rootfs_content '/etc/xdg/pcmanfm/default/pcmanfm.conf' 'wallpaper=/usr/share/backgrounds/tdvp-pda-paper.png'
require_rootfs_content '/etc/xdg/pcmanfm/default/pcmanfm.conf' 'show_wm_menu=0'
require_rootfs_content '/etc/xdg/menus/lxde-applications.menu' '<Filename>tdvp-pcmanfm.desktop</Filename>'
require_rootfs_content '/etc/xdg/menus/lxde-applications.menu' '<Filename>foot.desktop</Filename>'
require_rootfs_content '/etc/xdg/menus/lxde-applications.menu' '<Filename>vpl-package-manager.desktop</Filename>'
require_rootfs_content '/etc/xdg/menus/lxde-applications.menu' '<Menuname>Accessories</Menuname>'
require_rootfs_content '/etc/xdg/menus/lxde-applications.menu' '<Menuname>Sound &amp; Video</Menuname>'
require_rootfs_content '/etc/xdg/menus/lxde-applications.menu' '<Name>Games</Name>'
require_rootfs_content '/etc/xdg/menus/lxde-applications.menu' '<Category>Game</Category>'
require_rootfs_content '/etc/xdg/menus/lxde-applications.menu' '<Menuname>Games</Menuname>'
require_rootfs_content '/etc/xdg/labwc/rc.xml' '<default />'
require_rootfs_content '/usr/local/bin/tdvp-panel-menu' 'exec /bin/wfpanelctl smenu menu'
require_rootfs_content '/usr/local/bin/tdvp-panel-menu' 'DBUS_SESSION_BUS_ADDRESS%%,guid=*'
require_rootfs_content '/usr/lib/wf-panel-pi/libnetman.so' '/usr/bin/lp-connection-editor'
require_rootfs_content '/usr/bin/lp-connection-editor' 'exec /usr/bin/nm-connection-editor "$@"'
require_rootfs_content '/usr/bin/lp-connection-editor' 'WAYLAND_DISPLAY=wayland-0'
require_rootfs_content '/etc/xdg/labwc/autostart' '/usr/local/bin/tdvp-key-bridge &'
require_rootfs_content '/etc/xdg/labwc/rc.xml' '<mouseEmulation>yes</mouseEmulation>'
require_rootfs_content '/etc/xdg/labwc/rc.xml' '<layout>icon:iconify,max,close</layout>'
if debugfs -R 'cat /etc/xdg/labwc/rc.xml' "${ROOTFS}" 2>/dev/null | grep -Fq '<mapToOutput>'; then
	printf '%s\n' 'TDVP image guard: desktop Labwc touch configuration must not remap the output' >&2
	exit 1
fi
if debugfs -R 'stat /usr/local/bin/vpl-app-launcher' "${ROOTFS}" 2>/dev/null | grep -Fq 'Inode:'; then
	printf '%s\n' 'TDVP image guard: obsolete vpl-app-launcher wrapper is present' >&2
	exit 1
fi
require_rootfs_content '/usr/local/bin/tdvp-key-bridge' 'tca8418'
require_rootfs_content '/usr/local/bin/tdvp-key-bridge' '/usr/local/bin/tdvp-panel-menu'
require_rootfs_content '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-expand-rootfs' 'PARTUUID'
require_rootfs_content '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-expand-rootfs' 'rootfs-expand.pending'
require_rootfs_content '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-expand-rootfs' 'sfdisk'
require_rootfs_content '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-expand-rootfs' 'partprobe'
require_rootfs_content '/usr/local/libexec/vicliu-pocket-linux-hardware/tdvp-expand-rootfs' 'blockdev'
require_rootfs_content '/usr/lib/systemd/system/tdvp-rootfs-expand.service' 'Before=greetd.service'
require_rootfs_content '/usr/lib/wf-panel-pi/libvolumepulse.so' 'audio-volume-change'
require_rootfs_content '/usr/lib/wf-panel-pi/libvolumepulse.so' 'libcanberra'
reject_rootfs_line '/etc/fstab' '^[^#].*[[:space:]]/data[[:space:]]'
require_rootfs_content '/usr/share/X11/xkb/symbols/tdvp' 'key <I472>'
require_rootfs_content '/usr/share/X11/xkb/symbols/tdvp' 'modifier_map Mod5 { <I472> };'
require_rootfs_content '/usr/share/X11/xkb/symbols/us' 'xkb_symbols "tdvp"'
require_rootfs_content '/etc/tdvp/greetd/labwc/rc.xml' '<mouseEmulation>yes</mouseEmulation>'
require_rootfs_content '/etc/xdg/foot/foot.ini' 'initial-window-mode=windowed'
require_rootfs_content '/etc/xdg/foot/foot.ini' 'initial-window-size-pixels=900x460'
require_rootfs_content '/etc/greetd/gtkgreet.css' 'min-width: 740px;'
require_rootfs_content '/etc/greetd/gtkgreet.css' 'min-height: 62px;'
require_rootfs_content '/etc/greetd/gtkgreet.css' 'background-size: 14px 14px;'
require_rootfs_line '/etc/udev/rules.d/70-tdvp-touch.rules' 'ENV{LIBINPUT_CALIBRATION_MATRIX}="0 -1 1 1 0 0"'
require_rootfs_line '/etc/group' '^seat:x:'
require_rootfs_line '/etc/group' '^audio:.*:.*tdvp'
require_rootfs_line '/etc/tdvp/labwc/environment' '^XKB_DEFAULT_LAYOUT=us$'
require_rootfs_line '/etc/tdvp/labwc/environment' '^XKB_DEFAULT_VARIANT=tdvp$'
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

#!/usr/bin/env bash
set -euo pipefail

# Run the SDK board-specific boot packaging and enforce the resulting SD-card
# artifact through the TDVP image contract.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_POST_IMAGE="${SCRIPT_DIR}/../canaan/k230-soc/post-image.sh"
SDK_BOARD_DIR="$(cd "$(dirname "${SDK_POST_IMAGE}")" && pwd)"
IMAGE_GUARD="${SCRIPT_DIR}/verify-sdcard-image.sh"
SDK_STAGE_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
SDK_STAGE_MANIFEST="${SDK_STAGE_DIR}/.tdvp/sdk-baseline-manifest"
CPU1_BUILDER="${SCRIPT_DIR}/cpu1/build-rtsmart.sh"
# Buildroot may strip development headers from TARGET_DIR before post-image.
# Keep the firmware-side ABI source next to this board hook so packaging does
# not depend on the target package installation order.
CPU1_ABI_HEADER="${SCRIPT_DIR}/cpu1/tdvp_cpu1_abi.h"

# Buildroot creates rootfs.ext2 before this hook with deterministic ext4
# settings from the defconfig. This hook supplies deterministic boot ext4 and
# GPT identifiers to the SDK packaging flow.
TDVP_ROOTFS_UUID="6ed17b77-cd22-52f2-ae36-1fdbe5d476b7"
TDVP_ROOTFS_HASH_SEED="4cf1e2c3-fd06-5a49-a511-89041a8a1b98"
TDVP_BOOTFS_UUID="22c75a54-84db-52be-a5f1-69bf90788d29"
TDVP_BOOTFS_HASH_SEED="5305a011-7207-56c7-b4bf-ae5a1fc2f135"
TDVP_GPT_DISK_UUID="fbb0b6a4-c36f-5f5c-8c42-077ffba1377e"
TDVP_GPT_BOOT_UUID="900e1751-e943-5daa-a4ef-68831d3ed855"
TDVP_GPT_ROOTFS_UUID="c9cc7f55-7fd2-5d64-ae97-0715adf47fde"
TDVP_POST_IMAGE_TMP=""

cleanup_post_image_tmp() {
	if [ -n "${TDVP_POST_IMAGE_TMP}" ] && [ -d "${TDVP_POST_IMAGE_TMP}" ]; then
		rm -rf "${TDVP_POST_IMAGE_TMP}"
	fi
}

build_cpu1_firmware() {
	local firmware="${BINARIES_DIR}/tdvp-cpu1-rtsmart.bin"
	local manifest="${BINARIES_DIR}/tdvp-cpu1-rtsmart.manifest"

	[ -f "${CPU1_BUILDER}" ] || {
		printf 'TDVP CPU1 packaging error: firmware builder is missing: %s\n' \
			"${CPU1_BUILDER}" >&2
		exit 1
	}
	[ -s "${CPU1_ABI_HEADER}" ] || {
		printf 'TDVP CPU1 packaging error: staged ABI header is missing: %s\n' \
			"${CPU1_ABI_HEADER}" >&2
		exit 1
	}
	bash "${CPU1_BUILDER}" "${firmware}" "${manifest}" "${CPU1_ABI_HEADER}"
	[ -s "${firmware}" ] && [ -s "${manifest}" ] || {
		printf '%s\n' 'TDVP CPU1 packaging error: RT-Smart firmware build produced no payload' >&2
		exit 1
	}
	mkdir -p "${BINARIES_DIR}/big-core"
	install -m 0644 "${firmware}" "${BINARIES_DIR}/big-core/tdvp-cpu1-rtsmart.bin"
}

prepare_deterministic_sdk_packaging() {
	local vendor_file
	local real_mkfs_ext4

	TDVP_POST_IMAGE_TMP="$(mktemp -d "${TMPDIR:-/tmp}/tdvp-k230-post-image.XXXXXX")"
	for vendor_file in post-image.sh default.env genimage.cfg; do
		if [ ! -f "${SDK_BOARD_DIR}/${vendor_file}" ]; then
			printf 'TDVP packaging error: SDK board file is missing: %s\n' \
				"${SDK_BOARD_DIR}/${vendor_file}" >&2
			exit 1
		fi
		cp "${SDK_BOARD_DIR}/${vendor_file}" "${TDVP_POST_IMAGE_TMP}/${vendor_file}"
	done

	# The SDK's ``gz_file_add_ver`` only creates a vendor-named symlink.  It
	# assumes a release_version file which is intentionally absent from the
	# product overlay and its helper has disabled ``set -e`` by this point,
	# turning that missing optional metadata into misleading build noise.  TDVP
	# emits its explicit, reproducible sysimage-sdcard.img.gz below, so remove
	# only this optional alias call in the disposable vendor-script copy.
	sed -E -i \
		's/^[[:space:]]*gz_file_add_ver[[:space:]]+[$][{]image_name[}]\.gz;?[[:space:]]*$/\t: # TDVP owns the release artifact name and compression./' \
		"${TDVP_POST_IMAGE_TMP}/post-image.sh"
	if grep -Eq '^[[:space:]]*gz_file_add_ver[[:space:]]+\$\{image_name\}\.gz;?[[:space:]]*$' \
		"${TDVP_POST_IMAGE_TMP}/post-image.sh"; then
		printf '%s\n' 'TDVP packaging error: could not neutralize the SDK release-alias hook' >&2
		exit 1
	fi

	# The vendor helper otherwise overwrites bootcmd during env generation. CPU1
	# is reset first from the fixed raw slot, while a failed coprocessor launch
	# still falls through to Linux so recovery remains possible over SSH/serial.
	sed -E -i \
		's#bootcmd=run blinux;#bootcmd=run bootcmd_cpu1; run blinux;#g' \
		"${TDVP_POST_IMAGE_TMP}/post-image.sh"
	if ! grep -Fq 'bootcmd=run bootcmd_cpu1; run blinux;' \
		"${TDVP_POST_IMAGE_TMP}/post-image.sh"; then
		printf '%s\n' 'TDVP CPU1 packaging error: could not set CPU1-first U-Boot policy' >&2
		exit 1
	fi
	{
		printf '%s\n' 'cpu1_firmware_load=10000000'
		printf '%s\n' 'cpu1_firmware_block=5000'
		printf '%s\n' 'cpu1_firmware_blocks=a000'
		printf '%s\n' 'bootcmd_cpu1=mmc dev ${mmc_boot_dev_num} && mmc read ${cpu1_firmware_load} ${cpu1_firmware_block} ${cpu1_firmware_blocks} && boot_baremetal 1 ${cpu1_firmware_load} 1400000;'
	} >> "${TDVP_POST_IMAGE_TMP}/default.env"

	# The SDK reserves 10--30 MiB for an optional RTT payload but leaves it
	# commented out. Make it an explicit raw payload, not a GPT partition: the
	# vendor blinux command intentionally keeps boot as partition number one.
	awk '
		/^[[:space:]]*#[[:space:]]*partition rtt[[:space:]]*\{/ {
			print "\tpartition cpu1_rtsmart {"
			print "\t\tin-partition-table = false"
			print "\t\toffset = 10M"
			print "\t\timage = \"big-core/tdvp-cpu1-rtsmart.bin\""
			print "\t\tsize = 20M"
			skip = 1
			next
		}
		skip && /^[[:space:]]*#[[:space:]]*\}[[:space:]]*$/ {
			print "\t}"
			skip = 0
			next
		}
		skip { next }
		{ print }
	' "${TDVP_POST_IMAGE_TMP}/genimage.cfg" > "${TDVP_POST_IMAGE_TMP}/genimage.cfg.next"
	mv "${TDVP_POST_IMAGE_TMP}/genimage.cfg.next" "${TDVP_POST_IMAGE_TMP}/genimage.cfg"
	for required_line in \
		'partition cpu1_rtsmart {' \
		'in-partition-table = false' \
		'image = "big-core/tdvp-cpu1-rtsmart.bin"'; do
		if ! grep -Fq "${required_line}" "${TDVP_POST_IMAGE_TMP}/genimage.cfg"; then
			printf 'TDVP CPU1 packaging error: raw payload config is missing: %s\n' \
				"${required_line}" >&2
			exit 1
		fi
	done

	awk \
		-v disk_uuid="${TDVP_GPT_DISK_UUID}" \
		-v boot_uuid="${TDVP_GPT_BOOT_UUID}" \
		-v rootfs_uuid="${TDVP_GPT_ROOTFS_UUID}" '
		/partition-table-type = "gpt"/ {
			print
			print "\t\tdisk-uuid = \"" disk_uuid "\""
			next
		}
		/^[[:space:]]*partition boot[[:space:]]*\{/ { partition_name = "boot" }
		/^[[:space:]]*partition rootfs[[:space:]]*\{/ { partition_name = "rootfs" }
		partition_name != "" && /partition-type-uuid = "L"/ {
			print
			if (partition_name == "boot")
				print "\t\tpartition-uuid = \"" boot_uuid "\""
			else
				print "\t\tpartition-uuid = \"" rootfs_uuid "\""
			next
		}
		partition_name != "" && /^[[:space:]]*\}/ { partition_name = "" }
		{ print }
		' "${TDVP_POST_IMAGE_TMP}/genimage.cfg" > "${TDVP_POST_IMAGE_TMP}/genimage.cfg.next"
	mv "${TDVP_POST_IMAGE_TMP}/genimage.cfg.next" "${TDVP_POST_IMAGE_TMP}/genimage.cfg"

	for required_line in \
		"disk-uuid = \"${TDVP_GPT_DISK_UUID}\"" \
		"partition-uuid = \"${TDVP_GPT_BOOT_UUID}\"" \
		"partition-uuid = \"${TDVP_GPT_ROOTFS_UUID}\""; do
		if ! grep -Fq "${required_line}" "${TDVP_POST_IMAGE_TMP}/genimage.cfg"; then
			printf 'TDVP packaging error: deterministic GPT config was not generated: %s\n' \
				"${required_line}" >&2
			exit 1
		fi
	done

	# Use the Buildroot host e2fsprogs first.  It is built together with
	# genimage and has a matching RUNPATH, whereas the distribution mkfs can be
	# paired accidentally with a different libext2fs once host tools are on PATH.
	if [ -x "${HOST_DIR}/sbin/mkfs.ext4" ]; then
		real_mkfs_ext4="${HOST_DIR}/sbin/mkfs.ext4"
	else
		real_mkfs_ext4="$(command -v mkfs.ext4)"
	fi
	if [ -z "${real_mkfs_ext4}" ] || [ ! -x "${real_mkfs_ext4}" ]; then
		printf '%s\n' 'TDVP packaging error: host mkfs.ext4 is unavailable' >&2
		exit 1
	fi
	export TDVP_REAL_MKFS_EXT4="${real_mkfs_ext4}"
	export TDVP_BOOTFS_UUID TDVP_BOOTFS_HASH_SEED
	cat > "${TDVP_POST_IMAGE_TMP}/mkfs.ext4" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
exec "${TDVP_REAL_MKFS_EXT4}" \
	-U "${TDVP_BOOTFS_UUID}" \
	-E "hash_seed=${TDVP_BOOTFS_HASH_SEED}" \
	"$@"
EOF
	chmod 0755 "${TDVP_POST_IMAGE_TMP}/mkfs.ext4"
}

trap cleanup_post_image_tmp EXIT

: "${BINARIES_DIR:?Buildroot did not provide BINARIES_DIR}"
: "${HOST_DIR:?Buildroot did not provide HOST_DIR}"

# The vendor post-image helper invokes ``genimage`` by its bare name.  Its
# internal ``set +e`` means that a missing host tool would otherwise be
# swallowed, leaving a stale SD image beside freshly generated U-Boot files.
# Put the Buildroot host tools first and fail before packaging if the required
# image builder is unavailable.
if [ ! -x "${HOST_DIR}/bin/genimage" ]; then
	printf 'TDVP packaging error: Buildroot host genimage is unavailable: %s\n' \
		"${HOST_DIR}/bin/genimage" >&2
	exit 1
fi
export PATH="${HOST_DIR}/bin:${PATH}"

for boot_payload in Image k230-canmv-rm69a10.dtb; do
	if [ ! -s "${BINARIES_DIR}/${boot_payload}" ]; then
		printf 'TDVP packaging error: required boot payload is missing before SDK packaging: %s\n' \
			"${BINARIES_DIR}/${boot_payload}" >&2
		exit 1
	fi
done

build_cpu1_firmware
prepare_deterministic_sdk_packaging
PATH="${TDVP_POST_IMAGE_TMP}:${PATH}" "${TDVP_POST_IMAGE_TMP}/post-image.sh" "$@"

# Create the compressed SD image with `gzip -n` so its header is independent of
# source mtimes and both release artifacts are byte-reproducible.
GZIP_BIN="$(command -v gzip || true)"
if [ -z "${GZIP_BIN}" ] || [ ! -x "${GZIP_BIN}" ]; then
	printf '%s\n' 'TDVP packaging error: host gzip is unavailable' >&2
	exit 1
fi
GZIP_TMP="$(mktemp "${BINARIES_DIR}/.tdvp-sysimage-gzip.XXXXXX")"
"${GZIP_BIN}" -n -c "${BINARIES_DIR}/sysimage-sdcard.img" > "${GZIP_TMP}"
mv "${GZIP_TMP}" "${BINARIES_DIR}/sysimage-sdcard.img.gz"

IMAGE_MANIFEST="${BINARIES_DIR}/tdvp-image-manifest"
if [ ! -s "${SDK_STAGE_MANIFEST}" ]; then
	printf 'TDVP packaging error: missing staged source manifest: %s\n' \
		"${SDK_STAGE_MANIFEST}" >&2
	exit 1
fi
for image in sysimage-sdcard.img sysimage-sdcard.img.gz tdvp-cpu1-rtsmart.bin tdvp-cpu1-rtsmart.manifest; do
	if [ ! -s "${BINARIES_DIR}/${image}" ]; then
		printf 'TDVP packaging error: required SD-card artifact is missing: %s\n' \
			"${BINARIES_DIR}/${image}" >&2
		exit 1
	fi
done

bash "${IMAGE_GUARD}" "${BINARIES_DIR}"

{
	printf 'tdvp_image_manifest_version=1\n'
	printf 'profile=k230_canmv_t_display_rm69a10_labwc_desktop_defconfig\n'
	printf 'sdk_commit=5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7\n'
	printf 'linux_commit=7d4e1f444f461dbe3833bd99a4640e7b6c2cd529\n'
	printf 'cpu1_execution_model=linux-cpu0+rtsmart-cpu1\n'
	printf 'cpu1_runtime_base=0x10000000\n'
	printf 'cpu1_runtime_size=0x04000000\n'
	printf 'cpu1_mailbox_physical=0x13ff0000\n'
	printf 'gem_dma_contract=drm_gem_dma_helpers\n'
	printf 'buildroot_reproducible=y\n'
	printf 'desktop=labwc\n'
	printf 'panel=wf-panel-pi\n'
	printf 'background=pcmanfm\n'
	printf 'terminal=foot\n'
	printf 'display_manager=greetd\n'
	printf 'greeter=gtkgreet\n'
	printf 'session=tdvp-labwc-session\n'
	printf 'rootfs_uuid=%s\n' "${TDVP_ROOTFS_UUID}"
	printf 'rootfs_hash_seed=%s\n' "${TDVP_ROOTFS_HASH_SEED}"
	printf 'bootfs_uuid=%s\n' "${TDVP_BOOTFS_UUID}"
	printf 'bootfs_hash_seed=%s\n' "${TDVP_BOOTFS_HASH_SEED}"
	printf 'gpt_disk_uuid=%s\n' "${TDVP_GPT_DISK_UUID}"
	printf 'gpt_boot_uuid=%s\n' "${TDVP_GPT_BOOT_UUID}"
	printf 'gpt_rootfs_uuid=%s\n' "${TDVP_GPT_ROOTFS_UUID}"
	printf 'tdvp_stage_manifest_sha256='
	sha256sum "${SDK_STAGE_MANIFEST}" | awk '{print $1}'
	printf 'tdvp_source_lock_sha256='
	sed -n 's/^source_lock_sha256=//p' "${SDK_STAGE_MANIFEST}"
	for file in \
		"${BINARIES_DIR}/sysimage-sdcard.img" \
		"${BINARIES_DIR}/sysimage-sdcard.img.gz" \
		"${BINARIES_DIR}/tdvp-cpu1-rtsmart.bin" \
		"${BINARIES_DIR}/tdvp-cpu1-rtsmart.manifest" \
		"${BINARIES_DIR}/Image" \
		"${BINARIES_DIR}/k230-canmv-rm69a10.dtb" \
		"${BINARIES_DIR}/rootfs.ext2" \
		"${BINARIES_DIR}/boot.ext4" \
		"${BINARIES_DIR}/uboot/fn_u-boot-spl.bin" \
		"${BINARIES_DIR}/uboot/env.env" \
		"${BINARIES_DIR}/uboot/fn_ug_u-boot.bin"; do
		printf '%s_size=' "$(basename "${file}")"
		stat -c '%s' "${file}"
		printf '%s_sha256=' "$(basename "${file}")"
		sha256sum "${file}" | awk '{print $1}'
	done
} > "${IMAGE_MANIFEST}"

printf 'TDVP packaging verification: PASS\n' >&2
printf 'TDVP image manifest: %s\n' "${IMAGE_MANIFEST}" >&2

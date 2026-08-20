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

	real_mkfs_ext4="$(command -v mkfs.ext4)"
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
for boot_payload in Image k230-canmv-rm69a10.dtb; do
	if [ ! -s "${BINARIES_DIR}/${boot_payload}" ]; then
		printf 'TDVP packaging error: required boot payload is missing before SDK packaging: %s\n' \
			"${BINARIES_DIR}/${boot_payload}" >&2
		exit 1
	fi
done

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
for image in sysimage-sdcard.img sysimage-sdcard.img.gz; do
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
	printf 'gem_dma_contract=drm_gem_dma_helpers\n'
	printf 'buildroot_reproducible=y\n'
	printf 'desktop=labwc\n'
	printf 'panel=sfwbar\n'
	printf 'background=swaybg\n'
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

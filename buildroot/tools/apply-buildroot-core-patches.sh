#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  apply-buildroot-core-patches.sh <sdk-worktree>

Apply the pinned TDVP Buildroot integration patches to a staged K230 SDK
worktree. The operation is idempotent and verifies that each patch was
applied to the expected Buildroot source tree.
EOF
}

if [ "$#" -ne 1 ]; then
	usage
	exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
WORKTREE="$(cd "$1" && pwd)"
BUILDROOT_DIR="${WORKTREE}/output/buildroot-2025.02.1"
PATCH_DIR="${PROJECT_DIR}/buildroot/patches/buildroot"

[ -d "${BUILDROOT_DIR}" ] || {
	printf 'TDVP Buildroot patch: missing staged Buildroot directory: %s\n' "${BUILDROOT_DIR}" >&2
	exit 1
}

normalize_vendor_rootfs_overlay() {
	local overlay_dir="${BUILDROOT_DIR}/board/canaan/k230-soc/rootfs_overlay"
	local legacy_bin="${overlay_dir}/bin"
	local merged_bin="${overlay_dir}/usr/bin"
	local tool

	[ -d "${overlay_dir}" ] || {
		printf 'TDVP Buildroot patch: missing vendor rootfs overlay: %s\n' "${overlay_dir}" >&2
		exit 1
	}
	# A previous successful synchronization may already have converted the
	# overlay to Buildroot's merged-/usr layout.  Do not follow that /bin
	# symlink: its children are the same /usr/bin files and moving them again
	# would be a self-move rather than a normalization.
	[ -L "${legacy_bin}" ] && return 0
	[ -d "${legacy_bin}" ] || return 0

	mkdir -p "${merged_bin}"
	for tool in ap.sh k230_iomux.py ldd pwm-test.sh sta.sh; do
		[ -f "${legacy_bin}/${tool}" ] || {
			printf 'TDVP Buildroot patch: expected vendor tool is missing: %s\n' \
				"${legacy_bin}/${tool}" >&2
			exit 1
		}
		mv "${legacy_bin}/${tool}" "${merged_bin}/${tool}"
	done

	rmdir "${legacy_bin}" || {
		printf 'TDVP Buildroot patch: unexpected entries remain in vendor /bin overlay\n' >&2
		exit 1
	}
}

normalize_vendor_busybox_config() {
	local busybox_config="${BUILDROOT_DIR}/package/busybox/busybox.config"

	[ -f "${busybox_config}" ] || {
		printf 'TDVP Buildroot patch: missing vendor BusyBox configuration: %s\n' \
			"${busybox_config}" >&2
		exit 1
	}

	# The vendor profile enables a standalone Telnet daemon and presets its
	# systemd unit. The product exposes SSH as its remote administration path;
	# remove the Telnet applet at the BusyBox source of truth rather than merely
	# masking a service after boot.
	sed -i \
		-e 's/^CONFIG_TELNETD=y$/# CONFIG_TELNETD is not set/' \
		-e 's/^CONFIG_FEATURE_TELNETD_STANDALONE=y$/# CONFIG_FEATURE_TELNETD_STANDALONE is not set/' \
		-e 's/^CONFIG_FEATURE_TELNETD_PORT_DEFAULT=0$/# CONFIG_FEATURE_TELNETD_PORT_DEFAULT is not set/' \
		"${busybox_config}"

	grep -Fqx '# CONFIG_TELNETD is not set' "${busybox_config}" || {
		printf '%s\n' 'TDVP Buildroot patch: failed to disable the BusyBox Telnet daemon' >&2
		exit 1
	}
}

for patch_file in "${PATCH_DIR}"/*.patch; do
	[ -f "${patch_file}" ] || continue
	forward_log="$(mktemp)"
	reverse_log="$(mktemp)"

	if patch --dry-run --batch --forward -d "${BUILDROOT_DIR}" -p1 < "${patch_file}" \
		>"${forward_log}" 2>&1 && \
		! grep -Eq 'Reversed|Skipping patch|ignored' "${forward_log}"; then
		rm -f "${forward_log}" "${reverse_log}"
		patch --batch --forward -d "${BUILDROOT_DIR}" -p1 < "${patch_file}"
		continue
	fi

	if patch --dry-run --batch --reverse -d "${BUILDROOT_DIR}" -p1 < "${patch_file}" \
		>"${reverse_log}" 2>&1 && \
		! grep -Eq 'Unreversed|Skipping patch|ignored' "${reverse_log}"; then
		rm -f "${forward_log}" "${reverse_log}"
		continue
	fi

	cat "${forward_log}" >&2
	cat "${reverse_log}" >&2
	rm -f "${forward_log}" "${reverse_log}"
	printf 'TDVP Buildroot patch: cannot apply or verify %s\n' "${patch_file}" >&2
	exit 1
done

normalize_vendor_rootfs_overlay
normalize_vendor_busybox_config
printf 'TDVP Buildroot core patches: PASS\n'

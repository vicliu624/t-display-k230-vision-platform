#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage:
  prepare-k230-sdk-worktree.sh <worktree>

Create a disposable K230 Linux SDK worktree with the TDVP RM69A10 product
overlay. The SDK submodule is never modified. The destination must be empty,
unless TDVP_ALLOW_OVERWRITE=1 is explicitly set.

Set TDVP_STAGE_DRY_RUN=1 to report whether the next stage is source-only or
requires a Buildroot output reset. Dry-run does not create, copy, or remove
anything.
EOF
}

if [ "$#" -ne 1 ]; then
	usage
	exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SDK_DIR="${PROJECT_DIR}/vendor/k230_linux_sdk"
TDVP_OVERLAY="${PROJECT_DIR}/buildroot/k230-sdk-overlay"
USERSPACE_DIR="${PROJECT_DIR}/user-space"
TDVP_CORE_PATCHES="${PROJECT_DIR}/buildroot/patches/buildroot"
SOURCE_LOCK="${PROJECT_DIR}/buildroot/sdk-sources.lock"
WORKTREE="$(realpath -m "$1")"
SDK_COMMIT="5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7"
PROFILE="k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
COMPONENTS=(
	"tdvp-greeter"
	"tdvp-kpu-acceptance"
	"tdvp-labwc-desktop"
	"vicliu-pocket-linux-hardware"
)

require_dir() {
	[ -d "$1" ] || {
		printf 'TDVP SDK stage: missing directory: %s\n' "$1" >&2
		exit 1
	}
}

require_file() {
	[ -f "$1" ] || {
		printf 'TDVP SDK stage: missing file: %s\n' "$1" >&2
		exit 1
	}
}

normalize_text_line_endings() {
	# The SDK can be checked out on Windows before it is staged on ext4. Kconfig,
	# Make and executable helpers may use extensionless names, so normalize every
	# file that GNU grep identifies as text. Binary boot assets retain their
	# original bytes.
	while IFS= read -r -d '' file; do
		if LC_ALL=C grep -Iq . "$file"; then
			sed -i 's/\r$//' "$file"
		fi
	done < <(find "$WORKTREE/buildroot-overlay" "$WORKTREE/tools" -type f -print0)
	sed -i 's/\r$//' "$WORKTREE/Makefile"
}

write_tree_manifest() {
	local source_dir="$1"
	local source_prefix="$2"

	(
		cd "${source_dir}"
		find . -type f -print0 | sort -z |
			while IFS= read -r -d '' file; do
				if LC_ALL=C grep -Iq . "${file}"; then
					hash="$(sed 's/\r$//' "${file}" | sha256sum | awk '{print $1}')"
				else
					hash="$(sha256sum "${file}" | awk '{print $1}')"
				fi
				printf '%s  %s/%s\n' "${hash}" "${source_prefix}" "${file#./}"
			done
	)
}

write_platform_source_manifest() {
	local component

	{
		write_tree_manifest "${TDVP_OVERLAY}" "buildroot/k230-sdk-overlay"
		for component in "${COMPONENTS[@]}"; do
			write_tree_manifest "${USERSPACE_DIR}/${component}" "user-space/${component}"
		done
	} | LC_ALL=C sort
}

stage_component_sources() {
	local staged_overlay="$1"
	local component

	for component in "${COMPONENTS[@]}"; do
		require_dir "${USERSPACE_DIR}/${component}/src"
		mkdir -p "${staged_overlay}/package/${component}/src"
		rsync -a --delete --exclude '.git' \
			"${USERSPACE_DIR}/${component}/src/" \
			"${staged_overlay}/package/${component}/src/"
	done
}

write_component_source_manifest() {
	local source_root="$1"
	local component

	for component in "${COMPONENTS[@]}"; do
		write_tree_manifest "${source_root}/${component}/src" \
			"user-space/${component}/src"
	done | LC_ALL=C sort
}

write_staged_component_source_manifest() {
	local staged_overlay="$1"
	local component

	for component in "${COMPONENTS[@]}"; do
		write_tree_manifest "${staged_overlay}/package/${component}/src" \
			"user-space/${component}/src"
	done | LC_ALL=C sort
}

verify_staged_component_sources() {
	local staged_overlay="$1"

	if ! cmp -s \
		<(write_component_source_manifest "${USERSPACE_DIR}") \
		<(write_staged_component_source_manifest "${staged_overlay}"); then
		printf '%s\n' 'TDVP SDK stage: user-space package sources differ after staging' >&2
		printf '%s\n' 'TDVP SDK stage: refusing to create a build manifest for an incomplete stage' >&2
		exit 1
	fi
}

write_build_graph_manifest() {
	local overlay_dir="$1"

	(
		cd "${overlay_dir}"
		# Kconfig, the selected defconfig, board kernel fragments, and the ordered
		# Linux patch queue determine the effective Buildroot/kernel configuration.
		# Source, assets, and package make recipes are rebuilt package-by-package;
		# a Linux patch change requires a fresh output tree so the patched kernel
		# source cannot be reused accidentally.
		find . -type f \( -name Config.in -o -path './configs/*' -o -path './board/*/fragment/*' -o -path './linux/*.patch' \) -print0 | LC_ALL=C sort -z |
			while IFS= read -r -d '' file; do
				if LC_ALL=C grep -Iq . "${file}"; then
					hash="$(sed 's/\r$//' "${file}" | sha256sum | awk '{print $1}')"
				else
					hash="$(sha256sum "${file}" | awk '{print $1}')"
				fi
				printf '%s  buildroot/k230-sdk-overlay/%s\n' "${hash}" "${file#./}"
			done
	) | LC_ALL=C sort
}

write_core_patch_manifest() {
	local patch_dir="$1"

	(
		cd "${patch_dir}"
		find . -type f -name '*.patch' -print0 | sort -z |
			while IFS= read -r -d '' file; do
				sed 's/\r$//' "${file}" | sha256sum | \
					sed "s|  -$|  ${file}|"
			done
	)
}

write_previous_build_graph_manifest() {
	local manifest="$1"

	awk '$2 == "buildroot/k230-sdk-overlay/Config.in" || \
		$2 ~ /^buildroot\/k230-sdk-overlay\/.*\/Config\.in$/ || \
		$2 ~ /^buildroot\/k230-sdk-overlay\/configs\// || \
		$2 ~ /^buildroot\/k230-sdk-overlay\/board\/.*\/fragment\// || \
		$2 ~ /^buildroot\/k230-sdk-overlay\/linux\/.*\.patch$/ { print }' "${manifest}" | LC_ALL=C sort
}

write_previous_overlay_manifest() {
	local manifest="$1"

	awk '$1 ~ /^[0-9a-f]+$/ && \
		($2 ~ /^buildroot\/k230-sdk-overlay\// || $2 ~ /^user-space\//) { print }' "${manifest}" | LC_ALL=C sort
}

case "${WORKTREE}" in
	"${PROJECT_DIR}"|"${PROJECT_DIR}"/*|"${SDK_DIR}"|"${SDK_DIR}"/*)
		printf 'TDVP SDK stage: worktree must be outside the project and SDK source: %s\n' \
			"${WORKTREE}" >&2
		exit 1
		;;
esac

require_dir "${SDK_DIR}"
require_dir "${TDVP_OVERLAY}"
require_dir "${USERSPACE_DIR}"
require_dir "${TDVP_CORE_PATCHES}"
require_file "${SOURCE_LOCK}"

for component in "${COMPONENTS[@]}"; do
	require_dir "${USERSPACE_DIR}/${component}/src"
done

if [ "$(git -C "${SDK_DIR}" rev-parse HEAD)" != "${SDK_COMMIT}" ]; then
	printf 'TDVP SDK stage: expected SDK commit %s, got %s\n' \
		"${SDK_COMMIT}" "$(git -C "${SDK_DIR}" rev-parse HEAD)" >&2
	exit 1
fi

previous_manifest="${WORKTREE}/.tdvp/sdk-baseline-manifest"
current_graph_digest="$(write_build_graph_manifest "${TDVP_OVERLAY}" | sha256sum | awk '{print $1}')"
previous_graph_digest=""
current_overlay_digest="$(write_platform_source_manifest | sha256sum | awk '{print $1}')"
previous_overlay_digest=""
current_core_patch_digest="$(write_core_patch_manifest "${TDVP_CORE_PATCHES}" | sha256sum | awk '{print $1}')"
previous_core_patch_digest="$(sed -n 's/^core_patch_sha256=//p' "${previous_manifest}" 2>/dev/null | head -n 1 || true)"
if [ -f "${previous_manifest}" ] && \
	[ -n "$(write_previous_build_graph_manifest "${previous_manifest}")" ]; then
	previous_graph_digest="$(write_previous_build_graph_manifest "${previous_manifest}" | sha256sum | awk '{print $1}')"
fi
if [ -f "${previous_manifest}" ] && \
	[ -n "$(write_previous_overlay_manifest "${previous_manifest}")" ]; then
	previous_overlay_digest="$(write_previous_overlay_manifest "${previous_manifest}" | sha256sum | awk '{print $1}')"
fi
graph_changed=0
core_patch_changed=0
overlay_changed=0
if [ -n "${previous_graph_digest}" ] && \
	[ "${previous_graph_digest}" != "${current_graph_digest}" ]; then
	graph_changed=1
fi
if [ -f "${previous_manifest}" ] && \
	{ [ -z "${previous_core_patch_digest}" ] || \
		[ "${previous_core_patch_digest}" != "${current_core_patch_digest}" ]; }; then
	graph_changed=1
	core_patch_changed=1
fi
if [ -z "${previous_overlay_digest}" ] || \
	[ "${previous_overlay_digest}" != "${current_overlay_digest}" ]; then
	overlay_changed=1
fi

if [ "${TDVP_STAGE_DRY_RUN:-0}" = "1" ]; then
	if [ -z "${previous_graph_digest}" ]; then
		graph_status="baseline absent"
	elif [ "${graph_changed}" = "1" ]; then
		graph_status="package graph or core patch set changed; Buildroot output reset required"
	else
		graph_status="unchanged; incremental package rebuild is sufficient"
	fi
	if [ -z "${previous_overlay_digest}" ]; then
		overlay_status="baseline absent"
	elif [ "${previous_overlay_digest}" = "${current_overlay_digest}" ]; then
		overlay_status="unchanged"
	else
		overlay_status="source or package recipe changes detected"
	fi
	if [ -z "${previous_core_patch_digest}" ]; then
		core_patch_status="baseline absent; Buildroot output reset required"
	elif [ "${core_patch_changed}" = "1" ]; then
		core_patch_status="changed; Buildroot output reset required"
	else
		core_patch_status="unchanged"
	fi
	cat <<EOF
TDVP SDK stage: dry run
  worktree:            ${WORKTREE}
  profile:             ${PROFILE}
  build graph:         ${graph_status}
  overlay source:      ${overlay_status}
	build graph digest:  ${current_graph_digest}
	overlay digest:      ${current_overlay_digest}
	core patch digest:   ${current_core_patch_digest}
	core patch set:      ${core_patch_status}
EOF
	exit 0
fi

if [ -d "${WORKTREE}" ] && [ -n "$(find "${WORKTREE}" -mindepth 1 -maxdepth 1 -print -quit)" ] && \
	[ "${TDVP_ALLOW_OVERWRITE:-0}" != "1" ]; then
	printf 'TDVP SDK stage: destination is not empty: %s\n' "${WORKTREE}" >&2
	printf 'TDVP SDK stage: set TDVP_ALLOW_OVERWRITE=1 only for a disposable worktree.\n' >&2
	exit 1
fi

mkdir -p "${WORKTREE}"
# Keep Buildroot's downloaded sources and the disposable output directory on
# the ext4 worktree. The SDK copy refreshes the complete vendor overlay before
# the platform increment is layered on top. The platform overlay is an
# increment, not a replacement: deleting during this second rsync would remove
# vendor package recipes (for example the SDK's ffmpeg integration) that the
# product does not own.
rsync -a --delete --exclude '.git' --exclude 'dl' --exclude 'output' "${SDK_DIR}/" "${WORKTREE}/"
rsync -a "${TDVP_OVERLAY}/" "${WORKTREE}/buildroot-overlay/"
stage_component_sources "${WORKTREE}/buildroot-overlay"
normalize_text_line_endings
verify_staged_component_sources "${WORKTREE}/buildroot-overlay"

# A package graph change must start from an empty Buildroot target tree. Source
# and asset changes are deliberately incremental: their individual packages
# are cleaned and rebuilt by the requested package target.
if [ "${graph_changed}" = "1" ]; then
	rm -rf "${WORKTREE}/output/${PROFILE}" \
		"${WORKTREE}/output/buildroot-2025.02.1"
fi

mkdir -p "${WORKTREE}/.tdvp"
{
	printf 'tdvp_sdk_stage_version=6\n'
	printf 'sdk_commit=%s\n' "${SDK_COMMIT}"
	printf 'sdk_buildroot_version=2025.02.1\n'
	printf 'profile=%s\n' "${PROFILE}"
	# Keep this path location-independent so the staged-source digest is
	# reproducible across WSL and native Linux workspaces.
	printf 'overlay_source=buildroot/k230-sdk-overlay\n'
	printf 'component_source=user-space\n'
	printf 'source_lock=buildroot/sdk-sources.lock\n'
	printf 'source_lock_sha256='
	sed 's/\r$//' "${SOURCE_LOCK}" | sha256sum | awk '{print $1}'
	printf 'text_line_endings=lf-in-disposable-worktree\n'
	printf 'build_graph_sha256=%s\n' "${current_graph_digest}"
	printf 'core_patch_sha256=%s\n' "${current_core_patch_digest}"
	printf 'local_overlay_changed=%s\n' "${overlay_changed}"
	write_platform_source_manifest
} > "${WORKTREE}/.tdvp/sdk-baseline-manifest"

# A staging result is only valid when the external package graph and copied
# user-space sources can be consumed by Buildroot exactly as staged.  Keep the
# check here so every build starts from a verified worktree rather than relying
# on a separate, optional manual command.
bash "${SCRIPT_DIR}/assert-k230-sdk-rm69a10-baseline.sh" --patch-only "${WORKTREE}"

cat <<EOF
TDVP SDK stage: ready
  worktree: ${WORKTREE}
  profile:  ${PROFILE}
  manifest: ${WORKTREE}/.tdvp/sdk-baseline-manifest

Next:
  ${PROJECT_DIR}/buildroot/tools/build-k230-sdk-rm69a10.sh ${WORKTREE}
EOF

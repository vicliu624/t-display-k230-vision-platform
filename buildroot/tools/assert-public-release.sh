#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
	printf '%s\n' 'Usage: assert-public-release.sh <sdk-worktree>' >&2
	exit 2
fi

WORKTREE="$(cd "$1" && pwd)"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROFILE="k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
IMAGES="${WORKTREE}/output/${PROFILE}/images"

bash "${SCRIPT_DIR}/assert-k230-sdk-rm69a10-baseline.sh" "${WORKTREE}"
bash "${SCRIPT_DIR}/assert-tdvp-opkg-feed-release.sh"
for file in sysimage-sdcard.img sysimage-sdcard.img.gz tdvp-image-manifest; do
	[ -s "${IMAGES}/${file}" ] || {
		printf 'TDVP public release gate: missing %s\n' "${IMAGES}/${file}" >&2
		exit 1
	}
done
grep -Fqx 'desktop=labwc' "${IMAGES}/tdvp-image-manifest"
grep -Fqx 'panel=wf-panel-pi' "${IMAGES}/tdvp-image-manifest"
grep -Fqx 'background=pcmanfm' "${IMAGES}/tdvp-image-manifest"
grep -Fqx 'terminal=foot' "${IMAGES}/tdvp-image-manifest"
grep -Fqx 'display_manager=greetd' "${IMAGES}/tdvp-image-manifest"
grep -Fqx 'greeter=gtkgreet' "${IMAGES}/tdvp-image-manifest"
grep -Fqx 'session=tdvp-labwc-session' "${IMAGES}/tdvp-image-manifest"
printf '%s\n' 'TDVP public product release assertion: PASS'

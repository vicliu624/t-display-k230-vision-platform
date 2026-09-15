#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT
# Execute the production environment construction, without staging or making
# an SDK. Test values contain spaces to catch accidental shell word splitting.
sed -n '/^BUILD_ENV=(/,/^run_make() {/p' \
	"${SCRIPT_DIR}/build-k230-sdk-rm69a10.sh" | sed '$d' > "${TEMP_DIR}/environment.sh"
grep -q '^BUILD_ENV=(' "${TEMP_DIR}/environment.sh"
default_hostcc=/usr/bin/gcc
default_hostcxx=/usr/bin/g++
VARIABLES=(TDVP_CPU1_CACHE_ROOT TDVP_CPU1_TOOLCHAIN_DIR TDVP_CPU1_OPENSBI_TOOLCHAIN_DIR
	TDVP_CPU1_OPENSBI_CROSS_COMPILE TDVP_CPU1_SOURCE_DIR TDVP_CPU1_PYTHON TDVP_CPU1_JOBS)
for variable in "${VARIABLES[@]}"; do
	export "${variable}=/tmp/test cache/${variable}"
done
export TDVP_CPU1_JOBS=3
export TDVP_UNRELATED_HOST_SETTING=must-not-leak
source "${TEMP_DIR}/environment.sh"
"${BUILD_ENV[@]}" /usr/bin/env > "${TEMP_DIR}/actual"
for variable in "${VARIABLES[@]}"; do
	grep -Fxq "${variable}=${!variable}" "${TEMP_DIR}/actual"
done
if grep -q '^TDVP_UNRELATED_HOST_SETTING=' "${TEMP_DIR}/actual"; then
	printf '%s\n' 'CPU1 environment regression: unrelated host setting leaked' >&2
	exit 1
fi
grep -Fxq 'PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin' "${TEMP_DIR}/actual"
unset "${VARIABLES[@]}"
source "${TEMP_DIR}/environment.sh"
"${BUILD_ENV[@]}" /usr/bin/env > "${TEMP_DIR}/defaults"
if grep -q '^TDVP_CPU1_' "${TEMP_DIR}/defaults"; then
	printf '%s\n' 'CPU1 environment regression: unset overrides replaced defaults' >&2
	exit 1
fi
printf '%s\n' 'test-tdvp-cpu1-build-env: PASS explicit CPU1 overrides survive env -i; unset defaults and host isolation preserved'

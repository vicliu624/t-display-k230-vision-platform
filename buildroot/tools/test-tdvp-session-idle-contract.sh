#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SOURCE="${PROJECT_DIR}/user-space/tdvp-labwc-desktop/src"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TEMP_DIR}"' EXIT
export XDG_RUNTIME_DIR="${TEMP_DIR}/runtime"
export XDG_CONFIG_HOME="${TEMP_DIR}/config"
export TDVP_TEST_ARGUMENTS="${TEMP_DIR}/arguments"
mkdir -p "${XDG_RUNTIME_DIR}" "${XDG_CONFIG_HOME}/tdvp"

# Exercise the real policy with recorded Wayland-client arguments; never lock
# the developer's desktop or switch a real display off during a host test.
printf '%s\n' '#!/bin/sh' 'printf "%s\n" "$@" > "${TDVP_TEST_ARGUMENTS}"' > "${TEMP_DIR}/client"
chmod 0755 "${TEMP_DIR}/client"
sed 's/\r$//' "${SOURCE}/tdvp-session-powerctl" > "${TEMP_DIR}/powerctl"
sed -e 's/\r$//' \
	-e "s|^readonly SWAYLOCK=.*|readonly SWAYLOCK=${TEMP_DIR}/client|" \
	"${SOURCE}/tdvp-session-lock" > "${TEMP_DIR}/lock"
sed -e 's/\r$//' \
	-e "s|^readonly POWERCTL=.*|readonly POWERCTL=${TEMP_DIR}/powerctl|" \
	-e "s|^readonly SESSION_LOCK=.*|readonly SESSION_LOCK=${TEMP_DIR}/lock|" \
	-e "s|^readonly SWAYIDLE=.*|readonly SWAYIDLE=${TEMP_DIR}/client|" \
	-e "s|^readonly WLOPM=.*|readonly WLOPM=${TEMP_DIR}/client|" \
	"${SOURCE}/tdvp-session-idle" > "${TEMP_DIR}/idle"
chmod 0755 "${TEMP_DIR}/powerctl" "${TEMP_DIR}/lock" "${TEMP_DIR}/idle"

bash "${TEMP_DIR}/idle"
printf '%s\n' -w timeout 300 "${TEMP_DIR}/lock" timeout 330 \
	"${TEMP_DIR}/client --off '*'" resume "${TEMP_DIR}/client --on '*'" \
	before-sleep "${TEMP_DIR}/lock" > "${TEMP_DIR}/expected"
cmp "${TEMP_DIR}/expected" "${TDVP_TEST_ARGUMENTS}"
grep -Fqx 'blank_enabled=1' "${XDG_RUNTIME_DIR}/tdvp-session-power/state.env"

printf 'lock_after_seconds=10\nblank_after_seconds=5\n' > "${XDG_CONFIG_HOME}/tdvp/session-power.conf"
bash "${TEMP_DIR}/idle"
grep -Fqx 10 "${TDVP_TEST_ARGUMENTS}"
grep -Fqx 15 "${TDVP_TEST_ARGUMENTS}"

printf 'lock_after_seconds=0\nblank_after_seconds=5\n' > "${XDG_CONFIG_HOME}/tdvp/session-power.conf"
bash "${TEMP_DIR}/idle"
printf '%s\n' -w before-sleep "${TEMP_DIR}/lock" > "${TEMP_DIR}/expected"
cmp "${TEMP_DIR}/expected" "${TDVP_TEST_ARGUMENTS}"
grep -Fqx 'blank_enabled=0' "${XDG_RUNTIME_DIR}/tdvp-session-power/state.env"

printf 'lock_after_seconds=10\nblank_after_seconds=0\n' > "${XDG_CONFIG_HOME}/tdvp/session-power.conf"
bash "${TEMP_DIR}/idle"
printf '%s\n' -w timeout 10 "${TEMP_DIR}/lock" before-sleep "${TEMP_DIR}/lock" > "${TEMP_DIR}/expected"
cmp "${TEMP_DIR}/expected" "${TDVP_TEST_ARGUMENTS}"

bash "${TEMP_DIR}/lock"
printf '%s\n' -f -c 1f1e1b > "${TEMP_DIR}/expected"
cmp "${TEMP_DIR}/expected" "${TDVP_TEST_ARGUMENTS}"
test ! -d "${XDG_RUNTIME_DIR}/tdvp-session-lock"

grep -Fq '/usr/local/bin/tdvp-session-idle &' "${SOURCE}/autostart"
for helper in tdvp-session-idle tdvp-session-lock tdvp-session-powerctl; do
	grep -Fq "\$(TARGET_DIR)/usr/local/bin/${helper}" \
		"${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/tdvp-labwc-desktop/tdvp-labwc-desktop.mk"
done
for symbol in SWAYLOCK SWAYIDLE WLOPM LINUX_PAM; do
	grep -Fqx "BR2_PACKAGE_${symbol}=y" \
		"${PROJECT_DIR}/buildroot/k230-sdk-overlay/configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig"
done
grep -Fq 'auth       required   pam_unix.so' \
	"${PROJECT_DIR}/buildroot/k230-sdk-overlay/package/swaylock/src/swaylock"
printf '%s\n' 'test-tdvp-session-idle-contract: PASS defaults, custom/disabled timers, Wayland lock, autostart, packages and PAM'

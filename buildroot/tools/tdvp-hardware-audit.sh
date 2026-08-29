#!/usr/bin/env bash
set -euo pipefail

# Collect hardware evidence from a running T-Display K230 without changing
# its configuration. Run this from the repository root or from any path.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
REMOTE="${ROOT}/buildroot/tools/tdvp-remote.sh"

if [ "$#" -ne 0 ]; then
	printf 'usage: TDVP_REMOTE_PASSWORD=... %s\n' "$0" >&2
	exit 2
fi

: "${TDVP_REMOTE_PASSWORD:?TDVP_REMOTE_PASSWORD must be set}"

audit_payload="$(cat <<'REMOTE_AUDIT'
#!/bin/sh
set -u

section() {
	printf '\n===== %s =====\n' "$1"
}

show_command() {
	printf '$ %s\n' "$*"
	"$@" 2>&1 || printf '[command exited %s]\n' "$?"
}

section 'Identity'
show_command uname -a
show_command cat /etc/version/release_version
show_command date -u
show_command cat /proc/cmdline
if command -v nproc >/dev/null 2>&1; then
	show_command nproc
elif [ -r /sys/devices/system/cpu/online ]; then
	show_command cat /sys/devices/system/cpu/online
fi
show_command cat /sys/devices/system/cpu/present
show_command cat /sys/devices/system/cpu/online
for policy in /sys/devices/system/cpu/cpufreq/policy*; do
	[ -d "$policy" ] || continue
	printf '%s related_cpus=' "$policy"
	cat "$policy/related_cpus" 2>/dev/null || true
	printf '%s scaling_driver=' "$policy"
	cat "$policy/scaling_driver" 2>/dev/null || true
	printf '%s scaling_cur_freq=' "$policy"
	cat "$policy/scaling_cur_freq" 2>/dev/null || true
done

section 'System services'
if command -v systemctl >/dev/null 2>&1; then
	for unit in \
		NetworkManager.service \
		sshd.service \
		bluetooth.service \
		ModemManager.service \
		gpsd.service \
		systemd-timesyncd.service \
		chronyd.service; do
		printf '%-34s ' "$unit"
		systemctl is-active "$unit" 2>&1 || true
	done
else
	printf 'systemctl unavailable\n'
fi

section 'Device nodes'
for pattern in \
	'/dev/dri/*' \
	'/dev/input/*' \
	'/dev/snd/*' \
	'/dev/rtc*' \
	'/dev/i2c-*' \
	'/dev/spidev*' \
	'/dev/ttyS*' \
	'/dev/ttyUSB*' \
	'/dev/ttyACM*' \
	'/dev/cdc-wdm*' \
	'/dev/k230-gnne' \
	'/dev/k230-ai2d' \
	'/dev/gpiochip*' \
	'/dev/rfkill'; do
	set -- $pattern
	if [ "$1" != "$pattern" ] || [ -e "$1" ]; then
		ls -l $pattern 2>&1 || true
	fi
done

section 'Kernel class interfaces'
for path in \
	'/sys/class/net' \
	'/sys/class/input' \
	'/sys/class/power_supply' \
	'/sys/class/rtc' \
	'/sys/class/sound' \
	'/sys/class/drm' \
	'/sys/class/thermal' \
	'/sys/class/hwmon' \
	'/sys/bus/iio/devices' \
	'/sys/class/bluetooth'; do
	printf '\n-- %s --\n' "$path"
	if [ -d "$path" ]; then
		find "$path" -mindepth 1 -maxdepth 2 -print 2>/dev/null | sort
	else
		printf '[absent]\n'
	fi
done

section 'K230 KPU and AI2D'
for path in \
	'/sys/class/k230_gnne_class' \
	'/sys/class/k230_ai2d_class' \
	'/root/app/ai2d_kpu'; do
	printf '\n-- %s --\n' "$path"
	if [ -d "$path" ]; then
		find "$path" -mindepth 1 -maxdepth 2 -print 2>/dev/null | sort
	else
		printf '[absent]\n'
	fi
done
if command -v systemctl >/dev/null 2>&1; then
	printf '%-34s ' 'tdvp-kpu-acceptance.service'
	systemctl is-active tdvp-kpu-acceptance.service 2>&1 || true
	show_command systemctl status --no-pager tdvp-kpu-acceptance.service
fi
if [ -f /run/vicliu-pocket-linux-hardware/kpu-acceptance.pass ]; then
	printf 'kpu acceptance marker=present\n'
else
	printf 'kpu acceptance marker=absent\n'
fi
if [ -x /usr/local/bin/vpl-hwctl ]; then
	show_command /usr/local/bin/vpl-hwctl status
else
	printf 'vpl-hwctl=absent\n'
fi

section 'Network and radio state'
show_command ip -br address
show_command ip route
if command -v iw >/dev/null 2>&1; then
	show_command iw dev
fi
if command -v wpa_cli >/dev/null 2>&1; then
	show_command wpa_cli -i wlan0 status
fi
if command -v bluetoothctl >/dev/null 2>&1; then
	show_command bluetoothctl show
fi

section 'Input devices'
show_command cat /proc/bus/input/devices
if command -v libinput >/dev/null 2>&1; then
	show_command libinput list-devices
fi

section 'I2C devices'
for device in /sys/bus/i2c/devices/*; do
	[ -e "$device" ] || continue
	if [ -r "$device/name" ]; then
		printf '%s: ' "$device"
		cat "$device/name"
	fi
done

section 'Kernel configuration candidates'
if [ -r /proc/config.gz ] && command -v zcat >/dev/null 2>&1; then
	zcat /proc/config.gz 2>/dev/null | \
		grep -E 'CONFIG_(TOUCHSCREEN_TDVP_GT9895|I2C_GPIO|KEYBOARD_TCA8418|K230_GNNE_DRIVER|K230_AI2D_DRIVER|SND|SOUND|SND_SOC|RTC|POWER_SUPPLY|CHARGER_BQ|BATTERY_BQ|BT|SERIAL_DEV_BUS|GNSS)' || true
elif [ -r "/boot/config-$(uname -r)" ]; then
		grep -E 'CONFIG_(TOUCHSCREEN_TDVP_GT9895|I2C_GPIO|KEYBOARD_TCA8418|K230_GNNE_DRIVER|K230_AI2D_DRIVER|SND|SOUND|SND_SOC|RTC|POWER_SUPPLY|CHARGER_BQ|BATTERY_BQ|BT|SERIAL_DEV_BUS|GNSS)' \
			"/boot/config-$(uname -r)" || true
else
	printf '[kernel configuration is not exported by this image]\n'
fi

section 'Audio, RTC, power, and sensor values'
if command -v aplay >/dev/null 2>&1; then
	show_command aplay -l
fi
if command -v arecord >/dev/null 2>&1; then
	show_command arecord -l
fi
if command -v hwclock >/dev/null 2>&1; then
	show_command hwclock --show
fi
for supply in /sys/class/power_supply/*; do
	[ -e "$supply" ] || continue
	printf '\n-- %s --\n' "$supply"
	for field in type status capacity voltage_now current_now online present model_name; do
		if [ -r "$supply/$field" ]; then
			printf '%s=' "$field"
			cat "$supply/$field"
		fi
	done
done
for sensor in /sys/class/hwmon/hwmon* /sys/bus/iio/devices/iio:device*; do
	[ -d "$sensor" ] || continue
	printf '\n-- %s --\n' "$sensor"
	find "$sensor" -maxdepth 1 -type f \( -name 'name' -o -name 'temp*_input' -o -name 'humidity*_input' \) -print -exec cat {} \; 2>/dev/null || true
done

section 'Device tree inventory'
if [ -d /proc/device-tree ]; then
	find /proc/device-tree -maxdepth 8 -type f \( -name compatible -o -name status -o -name reg \) -print 2>/dev/null | sort | sed -n '1,320p'
else
	printf '[device tree unavailable]\n'
fi

section 'Relevant loaded modules'
if [ -r /proc/modules ]; then
	grep -Ei '8189|aic|r8152|goodix|gt9895|tca8418|pca953|k230_gnne|k230_ai2d|gnne|ai2d|bq|rtc|snd|sound|asoc|i2s|spi|lora|sx12|lr20|nrf|bluetooth|hci|gnss|pps|aht' /proc/modules || printf '[none]\n'
fi

section 'Relevant kernel messages'
if command -v dmesg >/dev/null 2>&1; then
	dmesg 2>&1 | grep -Ei 'gt9895|goodix|tca8418|pca953|k230_gnne|k230_ai2d|gnne|ai2d|rtl8189|aic|r8152|bq258|bq272|power_supply|rtc|asoc|snd|audio|i2s|microphone|speaker|nrf9151|gnss|gps|lte|modem|sx126|lr2021|lora|bluetooth|hci|aht20|thermal|fan' | tail -n 320 || printf '[no matching messages]\n'
fi
REMOTE_AUDIT
)"

printf '%s\n' "${audit_payload}" | "${REMOTE}" 'sh -s'

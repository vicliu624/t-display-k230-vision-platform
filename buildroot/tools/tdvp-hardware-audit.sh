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

section 'VGLite / DRM / Labwc recovery evidence'
# This section is deliberately forensic.  It must be possible to run it after
# a frozen desktop becomes reachable again without restarting a service,
# opening DRM master, touching /dev/vg_lite, changing sysfs, or consuming a
# VGLite completion.  The fields distinguish an old unbounded-wait kernel from
# the 0059/0060 candidate and preserve the evidence needed to explain a
# compositor fallback or a failure to reach one.
show_command cat /proc/sys/kernel/random/boot_id
show_command cat /proc/uptime
show_command cat /proc/loadavg
show_command cat /proc/meminfo
for node in /dev/vg_lite /dev/dri/card0; do
	if [ -e "$node" ]; then
		show_command ls -l "$node"
	else
		printf '%s=[absent]\n' "$node"
	fi
done
for parameter in \
	/sys/module/vglite/parameters/infinite_wait_watchdog_ms \
	/sys/module/vg_lite/parameters/infinite_wait_watchdog_ms; do
	if [ -r "$parameter" ]; then
		printf '%s=' "$parameter"
		cat "$parameter" || true
	else
		printf '%s=[absent]\n' "$parameter"
	fi
done
if [ -x /usr/bin/tdvp-vglite-watchdog-observer ]; then
	show_command /usr/bin/tdvp-vglite-watchdog-observer
else
	printf 'tdvp-vglite-watchdog-observer=[absent]\n'
fi
if [ -x /usr/local/bin/tdvp-renderer-profile ]; then
	show_command /usr/local/bin/tdvp-renderer-profile status
else
	printf 'tdvp-renderer-profile=[absent]\n'
fi

# Plane/CRTC fence properties alone do not establish that a usable explicit-
# synchronization backing exists.  This observer opens card0 read-only,
# rejects DRM master, and only reads caps/properties/blobs, so it remains safe
# in this forensic entrypoint while Labwc owns the desktop.
section 'Passive KMS format/modifier/fence capability evidence'
if [ -x /usr/bin/tdvp-kms-capability-observer ]; then
	show_command /usr/bin/tdvp-kms-capability-observer --device /dev/dri/card0
else
	printf 'tdvp-kms-capability-observer=[absent]\n'
fi

if [ -r /etc/greetd/config.toml ]; then
	show_command cat /etc/greetd/config.toml
fi
if command -v systemctl >/dev/null 2>&1; then
	printf '%-34s ' 'greetd.service'
	systemctl is-active greetd.service 2>&1 || true
	show_command systemctl status --no-pager greetd.service
fi

if command -v pgrep >/dev/null 2>&1; then
	printf '%s\n' '-- Labwc processes --'
	show_command pgrep -a -x labwc
	for pid in $(pgrep -x labwc 2>/dev/null || true); do
		[ -d "/proc/$pid" ] || continue
		printf '\n-- Labwc PID %s --\n' "$pid"
		show_command readlink "/proc/$pid/exe"
		show_command readlink "/proc/$pid/cwd"
		show_command cat "/proc/$pid/stat"
		show_command cat "/proc/$pid/status"
		if [ -r "/proc/$pid/wchan" ]; then
			show_command cat "/proc/$pid/wchan"
		fi
		if [ -r "/proc/$pid/stack" ]; then
			show_command cat "/proc/$pid/stack"
		fi
		if [ -r "/proc/$pid/environ" ]; then
			printf '%s\n' '$ filtered Labwc renderer environment'
			tr '\000' '\n' < "/proc/$pid/environ" | \
				grep -E '^(WLR_|TDVP_)' || printf '[no WLR_/TDVP_ variables]\n'
			# greetd's desktop launcher deliberately falls back to a private
			# $HOME/.cache runtime directory when the login stack did not create
			# /run/user/UID. Reading the live compositor environment is the only
			# reliable way for this forensic tool to find its two rotating logs;
			# keep it read-only and accept only an absolute directory value.
			runtime_dir="$(tr '\000' '\n' < "/proc/$pid/environ" | \
				sed -n 's/^XDG_RUNTIME_DIR=//p' | sed -n '1p')"
			case "$runtime_dir" in
				/*)
					printf 'Labwc XDG_RUNTIME_DIR=%s\n' "$runtime_dir"
					for log in "$runtime_dir/tdvp-labwc.log" \
						"$runtime_dir/tdvp-labwc.log.previous"; do
						[ -r "$log" ] || continue
						printf '\n-- %s (last 240 lines; discovered from Labwc) --\n' "$log"
						tail -n 240 "$log" 2>&1 || true
					done
					;;
				'')
					printf 'Labwc XDG_RUNTIME_DIR=[missing]\n'
					;;
				*)
					printf 'Labwc XDG_RUNTIME_DIR=[non-absolute value refused]\n'
					;;
			esac
		fi
	done
else
	printf 'pgrep=[absent]\n'
fi

# A frozen desktop can be caused by a test client which still owns a VGLite,
# KMS or DMA-BUF file descriptor; inspecting Labwc alone would miss that
# case.  Do not infer a process from its name only: report every process
# which retains a graphics-related FD, plus commands whose names look like
# local VGLite/KMS/Wayland tests.  This is intentionally procfs-only, so it
# neither obtains DRM master nor signals a potentially wedged client.
section 'Live graphics/test process evidence'
report_graphics_process() {
	proc="$1"
	cmdline="$2"
	graphics_fds="$3"
	pid="${proc##*/}"
	thread_count=0
	for task in "$proc"/task/*; do
		[ -d "$task" ] || continue
		thread_count=$((thread_count + 1))
	done
	printf '\n-- suspect graphics/test PID %s (threads=%s) --\n' \
		"$pid" "$thread_count"
	printf 'cmdline=%s\n' "$cmdline"
	printf 'graphics_fds=%s\n' "${graphics_fds:-[none; matched by test name]}"
	show_command cat "$proc/stat"
	show_command cat "$proc/status"
	if [ -r "$proc/wchan" ]; then
		show_command cat "$proc/wchan"
	fi
	if [ -r "$proc/syscall" ]; then
		show_command cat "$proc/syscall"
	fi
	if [ -r "$proc/stack" ]; then
		show_command cat "$proc/stack"
	fi
	for fd in "$proc"/fd/*; do
		[ -L "$fd" ] || continue
		fd_target="$(readlink "$fd" 2>/dev/null || true)"
		case "$fd_target" in
			/dev/dri/*|/dev/vg_lite|/dev/dma_heap/*|anon_inode:\[dmabuf\]*|anon_inode:\[sync_file\]*)
				printf 'fd[%s]=%s\n' "${fd##*/}" "$fd_target"
				fdinfo="$proc/fdinfo/${fd##*/}"
				[ -r "$fdinfo" ] && show_command cat "$fdinfo"
				;;
		esac
	done
}

graphics_or_test_count=0
for proc in /proc/[0-9]*; do
	[ -r "$proc/cmdline" ] || continue
	cmdline="$(tr '\000' ' ' < "$proc/cmdline" 2>/dev/null || true)"
	[ -n "$cmdline" ] || continue
	graphics_fds=''
	for fd in "$proc"/fd/*; do
		[ -L "$fd" ] || continue
		fd_target="$(readlink "$fd" 2>/dev/null || true)"
		case "$fd_target" in
			/dev/dri/*|/dev/vg_lite|/dev/dma_heap/*|anon_inode:\[dmabuf\]*|anon_inode:\[sync_file\]*)
				graphics_fds="${graphics_fds}${graphics_fds:+,}${fd##*/}:${fd_target}"
				;;
		esac
	done
	if [ -n "$graphics_fds" ] || printf '%s\n' "$cmdline" | \
		grep -Eiq '(^|[ /_-])(tdvp|vglite|vg_lite|wayland|wlroots|kms|drm|glmark|weston|egl|benchmark|bench|stress|test)([ /_.-]|$)'; then
		report_graphics_process "$proc" "$cmdline" "$graphics_fds"
		graphics_or_test_count=$((graphics_or_test_count + 1))
	fi
done
printf 'graphics_or_test_processes=%s\n' "$graphics_or_test_count"

# A normal systemd-logind session uses /run/user/UID. The per-Labwc capture
# above covers greetd's private runtime directory; retain this broad fallback
# for a compositor which exited before its environment could be inspected.
printf '%s\n' '-- fallback /run Labwc session logs --'
for log in /run/user/*/tdvp-labwc.log /run/user/*/tdvp-labwc.log.previous; do
	[ -r "$log" ] || continue
	printf '\n-- %s (last 240 lines) --\n' "$log"
	tail -n 240 "$log" 2>&1 || true
done
if command -v journalctl >/dev/null 2>&1; then
	printf '%s\n' '-- boot journal: Labwc/VGLite/DRM recovery (last 480 matches) --'
	journalctl -b --no-pager -o short-monotonic 2>&1 | \
		grep -Ei 'labwc|vglite|vg_lite|vg lite|drm|canaan|\bvo\b|page.?flip|gpu|hung task|blocked for more|watchdog|oom|rcu stall' | \
		tail -n 480 || printf '[no matching journal entries]\n'
fi
if command -v dmesg >/dev/null 2>&1; then
	printf '%s\n' '-- kernel: VGLite/DRM recovery (last 640 matches) --'
	dmesg 2>&1 | \
		grep -Ei 'vglite|vg_lite|\bdrm\b|canaan|\bvo\b|page.?flip|gpu|hung task|blocked for more|watchdog|oom|rcu stall' | \
		tail -n 640 || printf '[no matching kernel entries]\n'
fi
if [ -r /proc/interrupts ]; then
	printf '%s\n' '-- relevant interrupt counters --'
	grep -Ei 'vglite|vg_lite|\bdrm\b|canaan|\bvo\b|gpu' /proc/interrupts || \
		printf '[no named VGLite/DRM/VO interrupt counters]\n'
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

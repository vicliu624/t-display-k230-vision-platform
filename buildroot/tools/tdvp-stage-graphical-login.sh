#!/bin/sh
# Install a graphical-login payload without changing the active desktop unit.
set -eu

bundle=${1:?usage: tdvp-stage-graphical-login.sh BUNDLE EXPECTED_SHA256}
expected_sha256=${2:?usage: tdvp-stage-graphical-login.sh BUNDLE EXPECTED_SHA256}
actual_sha256=$(sha256sum "${bundle}" | awk '{print $1}')

if [ "${actual_sha256}" != "${expected_sha256}" ]; then
	echo "tdvp graphical login: bundle checksum mismatch" >&2
	exit 1
fi

stamp=$(date +%Y%m%d-%H%M%S)
rollback_dir="/root/tdvp-graphical-login-rollback-${stamp}"
mkdir -p "${rollback_dir}"

for path in \
	/usr/bin/greetd \
	/usr/bin/gtkgreet \
	/usr/bin/wf-panel-pi \
	/usr/bin/pcmanfm \
	/usr/bin/sfwbar \
	/usr/bin/swaybg \
	/usr/local/bin/tdvp-greeter-session \
	/usr/local/bin/tdvp-greeter-labwc \
	/usr/local/bin/tdvp-labwc-session \
	/etc/greetd \
	/etc/pam.d/greetd \
	/etc/pam.d/greetd-greeter \
	/etc/tdvp/greetd \
	/etc/tdvp/labwc/environment \
	/etc/udev/rules.d/70-tdvp-touch.rules \
	/etc/xdg/labwc/autostart \
	/etc/xdg/labwc/rc.xml \
	/etc/xdg/wf-panel-pi/wf-panel-pi.ini \
	/etc/wf-panel-pi/tdvp.css \
	/etc/xdg/pcmanfm/default/pcmanfm.conf \
	/usr/local/bin/tdvp-wf-panel-session \
	/usr/local/bin/tdvp-pcmanfm-desktop-session \
	/etc/sfwbar/sfwbar.config \
	/usr/share/sfwbar/tdvp-launcher.widget \
	/usr/share/backgrounds/tdvp-pda-paper.png \
	/usr/share/backgrounds/tdvp-pda-paper.svg \
	/usr/lib/sfwbar \
	/usr/share/sfwbar \
	/usr/lib/systemd/system/greetd.service \
	/usr/share/wayland-sessions/tdvp-labwc.desktop
do
	if [ -e "${path}" ]; then
		destination="${rollback_dir}${path}"
		mkdir -p "$(dirname "${destination}")"
		cp -a "${path}" "${destination}"
	fi
done
printf '%s\n' "${rollback_dir}" > /root/tdvp-graphical-login-rollback-path

if ! grep -q '^greeter:' /etc/passwd; then
	if ! grep -q '^greeter:' /etc/group; then
		addgroup -S greeter
	fi
	adduser -S -D -H -h /var/lib/greetd -s /bin/sh -G greeter greeter
fi

seat_members="$(sed -n 's/^seat:[^:]*:[^:]*://p' /etc/group)"
case ",${seat_members}," in
	*,greeter,*)
		;;
	*)
		# BusyBox addgroup varies by image: some variants only create a group
		# and cannot append an existing account. Preserve the existing seat GID
		# and add greeter to its supplementary-member list directly.
		sed -i '/^seat:/s/$/,greeter/' /etc/group
		;;
esac

mkdir -p /var/lib/greetd/.cache
chown -R greeter:greeter /var/lib/greetd
chmod 0700 /var/lib/greetd /var/lib/greetd/.cache

gzip -dc "${bundle}" | tar -x -f - -C /

# The image moved from SFWBar/swaybg to wf-panel-pi/PCManFM. These targets were
# saved in the rollback directory above; remove only the now-obsolete runtime
# paths so an incremental graphical-login stage cannot resurrect the old dock.
rm -f /usr/bin/sfwbar /usr/bin/swaybg /usr/local/bin/tdvp-sfwbar-session
rm -rf /etc/sfwbar /usr/lib/sfwbar /usr/share/sfwbar

if [ -e /root/tdvp-recover-direct-desktop ]; then
	mkdir -p /usr/local/sbin
	install -m 0755 /root/tdvp-recover-direct-desktop \
		/usr/local/sbin/tdvp-recover-direct-desktop
fi

systemctl daemon-reload
test -x /usr/bin/greetd
test -x /usr/bin/gtkgreet
test -x /usr/bin/wf-panel-pi
test -x /usr/bin/pcmanfm
test -s /etc/greetd/gtkgreet.css
test -s /etc/tdvp/labwc/environment
test -s /etc/udev/rules.d/70-tdvp-touch.rules
test -s /etc/xdg/labwc/autostart
test -s /etc/xdg/labwc/rc.xml
test -x /usr/local/bin/tdvp-wf-panel-session
test -x /usr/local/bin/tdvp-pcmanfm-desktop-session
test -s /etc/xdg/wf-panel-pi/wf-panel-pi.ini
test -s /etc/wf-panel-pi/tdvp.css
test -s /etc/xdg/pcmanfm/default/pcmanfm.conf
test -s /usr/share/backgrounds/tdvp-pda-paper.png
test -s /usr/share/backgrounds/tdvp-pda-paper.svg
sh -n /usr/local/bin/tdvp-greeter-session
sh -n /usr/local/bin/tdvp-greeter-labwc
sh -n /usr/local/bin/tdvp-labwc-session
sh -n /usr/local/bin/tdvp-wf-panel-session
sh -n /usr/local/bin/tdvp-pcmanfm-desktop-session
grep -q 'mouseEmulation>yes' /etc/xdg/labwc/rc.xml
grep -q 'widgets_left=smenu spacing8 window-list' /etc/xdg/wf-panel-pi/wf-panel-pi.ini
grep -q 'show_wm_menu=0' /etc/xdg/pcmanfm/default/pcmanfm.conf

echo "tdvp graphical login staged"
echo "rollback=${rollback_dir}"
grep '^greeter:' /etc/passwd
grep '^seat:' /etc/group

# Hardware Baseline Validation

This document defines the release gate for
`k230_canmv_t_display_rm69a10_labwc_desktop_defconfig`.

## Host Gate

```sh
WORKTREE="$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/assert-k230-sdk-rm69a10-baseline.sh "$WORKTREE"
bash buildroot/tools/assert-public-release.sh "$WORKTREE"
```

The image guard validates the boot payload layout, root filesystem, device
tree payload, K230 desktop session files, recovery networking tools, keyboard
layout service, touch rule, KPU acceptance utility, and hardware integration
service.

## Device Gate

Perform the following checks on a flashed device:

```sh
uname -a
systemctl status sshd systemd-networkd seatd tdvp-labwc-desktop
ip address
ls -l /dev/dri /dev/input
cat /sys/class/drm/card0-DSI-1/status
tdvp-display-smoke --device /dev/dri/card0 --seconds 5
```

For attached board functions, inspect the relevant standard Linux interface:

| Function | Validation interface |
| --- | --- |
| Wi-Fi | `iw dev`, `wpa_cli -i wlan0 status`, `ip address show wlan0` |
| USB Ethernet | `ethtool enu1`, `ip address show enu1` |
| Keyboard | `cat /proc/bus/input/devices`, `evtest` |
| Touch | `cat /proc/bus/input/devices`, `evtest` |
| I2C peripherals | `i2cdetect -y <bus>` |
| GPIO | `gpioinfo`, `gpiomon` |
| Audio | `arecord -l`, `aplay -l` |
| Camera/ISP | vendor ISP service status and V4L2 nodes |
| KPU | `tdvp-kpu-acceptance` |

The Wayland desktop is accepted when Labwc, Swaybg, SFWBar, and Foot start
from `tdvp-labwc-desktop.service` and the panel is interactive with keyboard
and touch input.

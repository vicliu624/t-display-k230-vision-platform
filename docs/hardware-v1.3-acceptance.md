# T-Display K230 V1.3 Acceptance

Run the commands below after flashing the Labwc desktop image. They record the
standard Linux evidence used for board acceptance.

```sh
systemctl status sshd systemd-networkd seatd tdvp-labwc-desktop
ip address
iw dev
cat /sys/class/drm/card0-DSI-1/status
cat /proc/bus/input/devices
arecord -l
aplay -l
gpioinfo
i2cdetect -y 2
```

The keyboard extension currently identifies peripherals on its I2C bus at the
following known addresses when the module is fitted:

| Address | Device |
| --- | --- |
| `0x38` | AHT20 temperature/humidity sensor |
| `0x55` | BQ27220 fuel gauge |
| `0x6b` | BQ25896 charger |

Display and session checks:

```sh
tdvp-display-smoke --device /dev/dri/card0 --seconds 5
systemctl status tdvp-keyboard-layout
systemctl status vicliu-pocket-linux-hardware
tdvp-kpu-acceptance
```

The device passes the desktop portion when the `DSI-1` panel shows Labwc,
Swaybg, and SFWBar; Foot opens from the application menu; keyboard and touch
both interact with the Wayland session.

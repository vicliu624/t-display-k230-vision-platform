# T-Display K230 Bring-Up Validation

This checklist defines what must be proven before the Buildroot Linux system is
accepted as the T-Display K230 platform baseline.

Chinese version:

- [validation.zh-CN.md](validation.zh-CN.md)

## Validation Rule

A feature is not accepted just because a raw Linux device node exists.

Success means:

1. the selected K230 Linux baseline drives the hardware;
2. the TDVP BSP abstracts the hardware;
3. the runtime can use the BSP path;
4. applications do not depend on Linux or vendor internals.

## Phase 0: Reproducible Inputs

| Check | Pass condition |
| --- | --- |
| Buildroot | official submodule is pinned |
| SDK baseline | exact `k230_linux_sdk` commit is pinned |
| Kernel | exact external kernel commit is pinned |
| OpenSBI/U-Boot | exact source or binary provenance is pinned |
| Defconfig | build starts from Git-tracked TDVP defconfig |
| Rootfs | all target files come from Buildroot packages or tracked overlay |
| Image | `sysimage-sdcard.img` is generated from tracked/pinned inputs |

### Current Phase 0 Record

2026-07-17 WSL ext4 build status:

| Item | Result |
| --- | --- |
| TDVP WSL workspace | `/home/vicliu/projects/t-display-k230-vision-platform` |
| SDK workspace | `/home/vicliu/projects/k230_linux_sdk` |
| SDK commit | `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` |
| SDK minimal targets | `opensbi` and `uboot` built |
| Boot artifacts | generated and imported as local ignored files |
| Buildroot defconfig | accepted |
| Full TDVP Buildroot build | passed; rebuilt successfully after `linux-dirclean` on 2026-07-17 with the Canaan DRM RGB888 fbdev and no-vblank console patch applied |
| Validation tools | `libdrm`, `modetest`, `tdvp-display-smoke`, `tdvp-keyboard-smoke`, `tdvp-k230-iomux`, Dropbear, `v4l2-ctl`, `media-ctl`, ALSA tools, SPI tools, network diagnostics, `wireless-regdb`, and WiFi/I2C/GPIO tools installed |
| Generated image | `output/t_display_k230_vision/images/sysimage-sdcard.img` |
| Image size | `268455936` bytes, about 256 MiB |
| Image SHA256 | `be9c30f3ca7004c375970dae48f26aea5cb802e159645a660183153b4ed24c27` |
| Hardware validation | Phase 1, Phase 2A, Phase 2B, and Phase 2C passed on board; USB Ethernet DHCP/SSH control plane, WiFi scan, the Goodix input device, 24bpp fbdev/fbcon, no-vblank DRM commits, and screen console output were validated; keyboard TCA8418 ACK still pending |

This record proves build reproducibility on the local WSL ext4 workspace. It
does not prove display, camera, or runtime acceptance.

## Phase 1: Boot And Console

| Check | Pass condition |
| --- | --- |
| SD flash | board boots from generated SD image |
| Boot chain | SPL/U-Boot/OpenSBI reaches K230 big-core Linux |
| UART | Linux console appears on the selected serial port |
| Init | BusyBox init runs platform startup scripts |
| Rootfs | expected root filesystem is mounted |
| Reboot | reboot returns to the same state |

### Current Phase 1 Record

2026-07-16 board boot status from the generated SD image:

| Item | Result |
| --- | --- |
| Kernel | `Linux tdisplay-k230 6.6.36 riscv64` |
| Command line | `root=/dev/mmcblk1p2 ... console=ttyS0,115200 earlycon=sbi` |
| Rootfs | `/dev/root` mounted as ext4 read/write |
| Partitions | `mmcblk1p1` boot partition and `mmcblk1p2` rootfs |
| Console | BusyBox login prompt reached; root shell available |
| Device tree model | `LILYGO T-Display K230` |
| DRM node | `/dev/dri/card0` exists |
| DSI connector | `/sys/class/drm/card0-DSI-1` exists |
| Touch input | Goodix registered `/dev/input/event0`; physical touch events are not accepted yet |

Phase 1 is accepted for boot, rootfs, serial console, and T-Display board
identity. The old Phase 1 record used the CanMV LCD DTB; new acceptance must
be based on `/proc/device-tree/model` reporting `LILYGO T-Display K230`.

## Phase 1A: Debug Control Plane

This phase does not turn the board into a general-purpose Linux server. Its
purpose is to make hardware experiments repeatable:

```text
serial console -> recovery/debug
eth0 DHCP      -> SSH/control/data collection
```

Target commands:

```sh
ip link set eth0 up
udhcpc -i eth0 -n -q -t 5
ip addr show eth0
ping -c 3 192.168.31.1
```

Host commands:

```sh
ping 192.168.31.86
ssh root@192.168.31.86
```

### Current Phase 1A Record

2026-07-17 board test result:

| Item | Result |
| --- | --- |
| Serial control | Windows `COM56`, 115200 8N1, root shell reachable |
| Ethernet driver | Realtek USB 10/100 LAN uses `r8152`; interface is `eth0` |
| DHCP | `eth0` obtained `192.168.31.86/24` from `192.168.31.1` |
| Device to gateway | `ping 192.168.31.1` passed |
| Host to device | Windows host can ping `192.168.31.86` |
| Device to host | device can ping Windows host `192.168.31.210` |
| Historical image limitation | the older `c6808945...` image had no Dropbear/OpenSSH/telnetd/netcat, so control was serial-only |
| Generated image policy | the current `be9c30f3...` image automatically starts DHCP on `eth0`, enables Dropbear, and sets the root password to `tdvp` |

Phase 1A is accepted through link and IP connectivity. After flashing the next
image, first confirm the `eth0` address over serial, then take control with
`ssh root@<eth0-ip>`. This SSH entry point is only for bring-up/debug control;
it does not change the application API boundary.

## Phase 2: Core Hardware

| Hardware | Pass condition |
| --- | --- |
| SD/MMC | rootfs remains stable under expected access |
| GPIO/pinctrl | reset, power, interrupt, and LED lines are controlled by drivers |
| I2C | touch, camera, and power devices can be probed when enabled |
| DMA/CMA | camera/display/AI buffer allocation is stable |

## Phase 2A: Display Smoke

Phase 2A is a board bring-up step. It is allowed to use DRM/KMS directly because
the BSP display API does not exist yet. This exception is limited to
validation-only tools installed by Buildroot.

Target commands:

```sh
cat /proc/device-tree/model
cat /sys/class/drm/card0-DSI-1/status
cat /sys/class/drm/card0-DSI-1/modes
modetest -M canaan-drm
tdvp-display-smoke --device /dev/dri/card0 --seconds 5
```

Pass condition:

- `card0-DSI-1` reports a usable mode.
- The mode is for the RM69A10 panel, expected first-pass size `568x1232`.
- `modetest` lists the Canaan DRM connector, CRTC, plane, and supported
  formats.
- `tdvp-display-smoke` prints `PASS`.
- The onboard RM69A10 panel shows color bars during the test window.

### Current Phase 2A Record

2026-07-17 board test result:

| Item | Result |
| --- | --- |
| Board model | `LILYGO T-Display K230` |
| Connector status | `card0-DSI-1` is `connected` |
| Mode | `568x1232` |
| Smoke tool | `tdvp-display-smoke --device /dev/dri/card0 --seconds 5` |
| Format | `RG24` |
| Result | `PASS`; the panel showed red/green/blue/white bars |

Phase 2A is accepted. Older images reported `bpp/depth value of 32/24 not
supported` and `fbdev: Failed to setup generic emulation`, but the DRM/KMS
plane path worked. A temporary `XRGB8888`/32bpp experiment created `/dev/fb0`
but left the panel purple and made DRM commits wait for missing vblank events.
The accepted path is the later RGB888/24bpp fbdev plus no-vblank/fake-vblank
DRM commit fix validated in Phase 2B and Phase 2C.

Failure handling:

- If `modetest` cannot list planes, inspect DRM/KMS client capabilities and
  kernel config.
- If the connector exists but no mode is usable, inspect DTS panel timing,
  DSI lane count, reset, and power sequencing.
- If format negotiation fails, compare plane formats against K230 SDK display
  examples and update the smoke-test candidate order.
- If color bars appear with red/blue swapped, keep the display path accepted
  and record the byte-order detail for the BSP implementation.

## Phase 2B: fbdev/fbcon Bring-Up

This phase makes the kernel create a generic fbdev on top of Canaan DRM, which
is the foundation for a traditional Linux screen console. It does not replace
the final BSP display API; it is only for boot visibility and board debugging.

2026-07-17 accepted build change:

| Item | Result |
| --- | --- |
| Kernel patch | `0002-drm-canaan-use-rgb888-fbdev-format.patch` |
| Driver path | `drivers/gpu/drm/canaan/canaan_drv.c`, `drivers/gpu/drm/canaan/canaan_crtc.c` |
| Change | generic fbdev requests 24bpp RGB888; the T-Display DSI path skips unreliable DRM vblank registration and lets atomic helpers use fake vblank events |
| Generated image | `output/t_display_k230_vision/images/sysimage-sdcard.img` |
| Image SHA256 | `be9c30f3ca7004c375970dae48f26aea5cb802e159645a660183153b4ed24c27` |
| Board status | boot artifacts were copied to the live SD boot partition over SSH, then the board was rebooted and validated |

Target commands:

```sh
dmesg | grep -Ei 'canaan-drm|fbdev|fbcon|fb0|Console'
ls -l /dev/fb0 /sys/class/graphics/fb0 2>/dev/null || true
cat /proc/consoles
modetest -M canaan-drm | grep -Ei 'RG24|RGB888|AR24|ARGB8888'
```

Pass condition:

- `dmesg` no longer prints `No compatible format found` or
  `fbdev: Failed to setup generic emulation`.
- `/dev/fb0` and `/sys/class/graphics/fb0` exist.
- `modetest` shows `RG24` on the primary OSD plane.
- To show boot logs on the screen, bootargs must still add `console=tty0` while
  keeping `console=ttyS0,115200` as the recovery console.
- To show a final login shell on the screen, the rootfs must start getty on
  `tty1`, and a usable input device must be validated, either USB HID keyboard
  or the fixed TCA8418 keyboard.

### Current Phase 2B Record

2026-07-17 board test result:

| Item | Result |
| --- | --- |
| Kernel | `Linux tdisplay-k230 6.6.36 #2 SMP Fri Jul 17 17:39:25 CST 2026 riscv64` |
| fbdev error check | `No compatible format`, `Failed to setup generic emulation`, and `bpp/depth` no longer appear |
| fbdev node | `/dev/fb0` exists |
| fbdev sysfs | `/sys/class/graphics/fb0` exists |
| fbdev name | `canaan-drmdrmfb` |
| fbdev mode | `U:568x1232p-0` |
| fbdev virtual size | `568,1232` |
| fbdev bpp | `24` |
| fbdev stride | `1704` |
| fbcon log | `Console: switching to colour frame buffer device 71x77` |
| DRM connector | `card0-DSI-1` is `connected`, mode `568x1232` |
| DRM commit behavior | `modetest` exits normally; `tdvp-display-smoke --device /dev/dri/card0 --seconds 5` returns `PASS` on `RG24`; no `vblank wait timed out`, `flip_done timed out`, or `commit wait timed out` appears |
| Network control | after reboot, `eth0` obtained `192.168.31.155/24` by DHCP, and Dropbear SSH is usable |

Phase 2B is accepted for the fbdev/fbcon foundation. The previous 32bpp
`XRGB8888` experiment is not accepted because it left the panel purple and
depended on an invalid alpha interpretation. The accepted console baseline is
24bpp RGB888 with DRM fake-vblank completion.

## Phase 2C: Screen Console And Login

This phase moves the onboard screen from "framebuffer exists" to "Linux boot
logs are visible and a login shell eventually appears". It is still a system
bring-up capability; applications must not depend directly on fbdev, VT, or
evdev.

2026-07-17 build change:

| Item | Result |
| --- | --- |
| U-Boot env source | `buildroot/board/t-display-k230/uboot-linux.env` |
| bootargs | `root=/dev/mmcblk1p2 loglevel=8 rw rootdelay=4 rootfstype=ext4 console=tty0 console=ttyS0,115200 earlycon=sbi consoleblank=0` |
| Rootfs post-build | `buildroot/board/t-display-k230/post-build.sh` |
| Added getty | `tty1::respawn:/sbin/getty -L tty1 0 vt100` |
| Kernel console config | `CONFIG_VT=y`, `CONFIG_VT_CONSOLE=y`, `CONFIG_FRAMEBUFFER_CONSOLE=y`, `CONFIG_DRM_FBDEV_EMULATION=y` |
| USB keyboard config | `CONFIG_HID_GENERIC=y`, `CONFIG_USB_HID=y` |
| Generated image | `output/t_display_k230_vision/images/sysimage-sdcard.img` |
| Image SHA256 | `be9c30f3ca7004c375970dae48f26aea5cb802e159645a660183153b4ed24c27` |
| Board status | validated after live boot-partition update and reboot |

Target commands:

```sh
cat /proc/cmdline
cat /proc/consoles
ps | grep -E '[g]etty.*tty1|[g]etty.*ttyS0'
dmesg | grep -Ei 'Console:|fb0|fbcon|tty0|tty1|usbhid|hid-generic'
ls -l /dev/tty0 /dev/tty1 /dev/fb0 /dev/input/event*
```

Pass condition:

- `/proc/cmdline` contains `console=tty0` and `console=ttyS0,115200`.
- `/proc/consoles` includes both the screen console and the serial console.
- BusyBox init respawns both the `ttyS0` getty and the `tty1` getty.
- The panel shows kernel logs or the final login prompt.
- When a USB keyboard is attached, the kernel creates HID/input events and
  login works on `tty1`.

### Current Phase 2C Record

2026-07-17 board test result:

| Item | Result |
| --- | --- |
| Upgrade method | copied the new `Image` and `tdisplay-k230.dtb` to `mmcblk1p1` over SSH, then rebooted |
| SSH address before reboot | `192.168.31.106` |
| SSH address after reboot | `192.168.31.155` |
| Command line | contains `console=tty0`, `console=ttyS0,115200`, and `consoleblank=0` |
| Consoles | `/proc/consoles` lists both `ttyS0` and `tty0` |
| Getty | BusyBox getty runs on `ttyS0` and `tty1` |
| fbdev | `/sys/class/graphics/fb0/bits_per_pixel` is `24` |
| DRM/KMS | `modetest -M canaan-drm` exits normally |
| Display smoke | `tdvp-display-smoke --device /dev/dri/card0 --seconds 5` prints `PASS plane=33 format=RG24` |
| Console write | SSH wrote readable status text to `/dev/tty1` and `/dev/tty0` without DRM errors |
| DRM errors | no `vblank wait timed out`, `flip_done timed out`, or `commit wait timed out` after the no-vblank fix |

Phase 2C is accepted for boot-visible screen console plumbing. Physical login
from the onboard screen still depends on a validated keyboard path; USB HID is
enabled, while the detachable TCA8418 keyboard remains under Phase 3B.

## Phase 3: Display And Input

| Check | Pass condition |
| --- | --- |
| RM69A10 | panel initializes and presents a test frame |
| Touch | GT9895 produces normalized BSP input events |
| Abstraction | test app does not open DRM/KMS or evdev directly |

LT9611 HDMI remains in scope after the onboard panel path is stable.

## Phase 3A: WiFi Smoke

WiFi is part of the current bring-up set because the board reference and boot
observation both point to RTL8189FTV on SDIO0.

Target commands:

```sh
dmesg | grep -Ei 'mmc0|8189|rtl|cfg80211|mac80211|wlan'
lsmod | grep -Ei '8189|cfg80211|mac80211'
ip link
iw dev || true
modprobe 8189fs || modprobe rtl8189fs || true
wpa_passphrase '<ssid>' '<passphrase>' > /tmp/wpa.conf
wpa_supplicant -B -i wlan0 -c /tmp/wpa.conf
udhcpc -i wlan0
ip addr show wlan0
```

Pass condition:

- `mmc0` remains the SDIO device and `mmc1` remains the SD-card/rootfs device.
- The Realtek module loads or is already loaded.
- A `wlan*` network interface appears.
- The device can associate to an AP and obtain an IP address.

### Current Phase 3A Record

2026-07-17 board test result:

| Item | Result |
| --- | --- |
| SDIO controller | `mmc0` enumerated a high speed SDIO card |
| Rootfs controller | `mmc1` enumerated the SDHC card and `mmcblk1p1/p2` |
| Module load | `modprobe 8189fs` loaded the out-of-tree module |
| Interfaces | `wlan0` and `wlan1` appeared |
| `iw dev` | two managed interfaces were visible |
| USB Ethernet | an attached Realtek USB 10/100 LAN adapter enumerated as `eth0` with the `r8152` driver |
| USB Ethernet DHCP | `eth0` obtained `192.168.31.86/24`; gateway is `192.168.31.1` |
| USB Ethernet ping | device->gateway, host->device, and device->host ping tests passed |
| WiFi scan | `iw dev wlan0 scan` can scan nearby APs |

Phase 3A has passed through WiFi interface enumeration and scan. AP association,
DHCP, and stable throughput still need separate validation. USB Ethernet has
passed link, DHCP, and ping and can serve as the main control plane for later
board experiments; throughput and long-running stability still need
`iperf3`/long ping testing.

## Phase 3B: Keyboard Smoke

The LilyGO demo proves the TCA8418 is polled on I2C4 address `0x34`. The
upstream Linux input driver still needs a verified IRQ line, so this phase uses
the TDVP validation tool.

Target commands:

```sh
i2cdetect -l
i2cdetect -y 0
i2cdetect -y 1
tdvp-k230-iomux dump 43 46 47
tdvp-k230-iomux keyboard
tdvp-k230-iomux dump 43 46 47
tdvp-keyboard-smoke --reset-gpio 43 --bus /dev/i2c-0 --seconds 10
```

Pass condition:

- `tdvp-keyboard-smoke` detects a TCA8418-compatible device at address `0x34`.
- Pressing keys during the polling window prints key press/release events.
- The result is treated as board hardware proof, not as the final application
  input API.

### Current Phase 3B Record

2026-07-17 board test result:

| Item | Result |
| --- | --- |
| Linux I2C adapters | `/dev/i2c-0` and `/dev/i2c-1` |
| Expected keyboard bus | `/dev/i2c-0`, matching DTS alias `i2c0 = &i2c4` |
| TCA8418 address | `0x34` did not ACK |
| Touch address cross-check | `0x5d` was visible on `/dev/i2c-1` |
| Keyboard module population | This log was captured with the detachable keyboard module installed; missing module is no longer the primary explanation |
| IOMUX preset | `tdvp-k230-iomux keyboard` ran; IO46/IO47 read back as I2C4 alt3 with `cfg=0x18f` |
| GPIO43 reset path | `tdvp-keyboard-smoke` can pulse GPIO43 through `/dev/gpiochip1 line 11` |
| Keyboard smoke | no TCA8418-compatible device detected |

The LilyGO demo additionally calls `fpioa_set_function(46, IIC4_SCL)` and
`fpioa_set_function(47, IIC4_SDA)`, then pulses GPIO43 high/low/high for reset.
The latest log rules out both "I2C4 IOMUX was not applied" and "GPIO43 reset
path is unusable" as the primary causes: the `c6808945...` image aligns IO43
reset IOMUX with the vendor FPIOA helper's GPIO default `0x18f`, and the smoke
tool can pulse reset through `/dev/gpiochip1 line 11`, but `0x34` still does
not ACK. The next layer is TCA8418 power, I2C pull-ups, physical reset
continuity, and the detachable keyboard connector. The long-term path remains
kernel pinctrl plus DTS.

## Phase 3C: Board Capability Probe

The reference source also mentions battery/fuel gauge, LR2021, GNSS/LTE, fan,
speaker, and AHT20. These stay in scope, but first need bus/address/pin proof.

Target commands:

```sh
i2cdetect -l
for bus in /dev/i2c-*; do echo "== $bus =="; i2cdetect -y "${bus##*-}"; done
gpiodetect || true
gpioinfo || true
dmesg | grep -Ei 'bq27|bq258|aht|pwm|gpio|i2c|spi|sound|audio'
```

Pass condition:

- Candidate I2C addresses are recorded before adding DTS nodes.
- GPIO/PWM ownership is understood before enabling fan, keyboard backlight, or
  indicator LEDs.
- Audio/speaker support is accepted only after a real playback test.

## Phase 4: Camera

| Check | Pass condition |
| --- | --- |
| Sensor | first selected sensor probes |
| Capture | BSP `camera_read` returns frames |
| Mode | one stable mode is accepted before expanding modes |
| Memory | continuous capture does not leak buffers |
| Abstraction | test app does not call V4L2/ioctl directly |

## Phase 5: Runtime And AI

| Check | Pass condition |
| --- | --- |
| Pipeline | camera -> preprocess -> optional AI -> overlay -> display runs |
| Buffers | ownership is deterministic |
| Event loop | camera, input, AI, and display scheduling is runtime-owned |
| AI | one model loads and runs through stable TDVP API |
| Shutdown | runtime exits without leaving hardware in an unknown state |

## Phase 6: Additional Board Capabilities

Additional populated board capabilities remain in scope:

- LoRa;
- BLE;
- LTE-M/GNSS;
- WiFi;
- keyboard;
- battery/charger/fuel gauge;
- audio/speaker;
- fan;
- AHT20/environment sensor.

They are accepted only through BSP/system service APIs, not by application-level
GPIO, SPI, I2C, ALSA, or ioctl access.

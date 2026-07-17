# T-Display K230 Linux Bring-Up Plan

This document defines the current bring-up plan for adapting T-Display K230
through the official K230 Linux SDK baseline.

Chinese version:

- [bringup-plan.zh-CN.md](bringup-plan.zh-CN.md)

## Goal

Produce the first TDVP SD-card image that boots T-Display K230 into a minimal
Buildroot Linux userspace and creates a controlled path toward hardware-complete
support.

The first accepted boot path is:

```text
SD card
  -> SPL / U-Boot / OpenSBI compatible with the Canaan Linux SDK path
  -> K230 big-core Linux
  -> BusyBox rootfs
  -> TDVP platform init
```

## Primary References

Use these sources with separate roles:

| Source | Role |
| --- | --- |
| `kendryte/k230_linux_sdk` | primary Linux boot/kernel/image reference |
| `Xinyuan-LilyGO/T-Display-K230_canmv_rt` | T-Display K230 hardware facts |
| TDVP Buildroot external tree | platform-owned minimal rootfs, packages, image policy, and API boundary |

## Starting Strategy

Start from the official Linux SDK shape, then reduce and adapt it:

```text
k230_linux_sdk reference
  -> select closest K230/CanMV Linux profile
  -> extract kernel, DTS, U-Boot, OpenSBI, image-layout facts
  -> compare with T-Display K230 hardware facts
  -> create TDVP board delta
  -> build minimal TDVP rootfs and image
```

Do not start from a clean upstream kernel as the first implementation path.
Do not start from LilyGO RT-Smart application behavior as the platform model.

## First Work Items

### 1. Pin The SDK Baseline

Status: done for the first baseline.

Pinned input:

```text
repository: https://github.com/kendryte/k230_linux_sdk
branch: dev
commit: 5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7
```

See [sources.lock](sources.lock) and
[k230-linux-sdk-baseline.md](k230-linux-sdk-baseline.md).

### 2. Select The Closest Official Profile

Start analysis from:

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

Extract and document:

- kernel repo and commit;
- kernel defconfig;
- DTS names;
- OpenSBI version and build mode;
- U-Boot board name and version;
- genimage layout;
- boot environment;
- packages required for boot and hardware validation.

Status: extracted into [k230-linux-sdk-baseline.md](k230-linux-sdk-baseline.md).

### 3. Define The T-Display Delta

Compare the official K230/CanMV profile with LilyGO T-Display K230 facts:

- RM69A10 AMOLED panel.
- GT9895 touch.
- CSI camera route and first sensor.
- UART console.
- SD/MMC boot device.
- GPIO reset, power, interrupt, and backlight lines.
- Optional board peripherals.

The result should be a TDVP board delta, not a fork of the full SDK behavior.

### 4. Create The Minimal Buildroot Profile

The TDVP profile must include:

- RISC-V target and toolchain policy compatible with the selected SDK baseline.
- Selected K230 Linux kernel source.
- Selected DTS or TDVP DTS delta.
- BusyBox rootfs.
- deterministic SD-card image generation.
- platform init files.

It must exclude by default:

- desktop packages;
- distro package managers;
- app-specific packages;
- full vendor demo package set;
- direct application access to Linux devices.

### 5. Build The First Image

The first image should prove:

```text
kernel + dtb + rootfs + boot artifacts are reproducible from pinned inputs
```

The image output remains:

```text
sysimage-sdcard.img
```

### 6. Validate Hardware In Waves

Wave A:

- SPL/U-Boot/OpenSBI handoff.
- serial console.
- SD/MMC rootfs.
- BusyBox init.
- debug control plane: `COM56` serial for recovery and USB Ethernet `eth0`
  DHCP for SSH/data collection.

Wave B:

- RM69A10 display.
- GT9895 touch.
- RTL8189FTV WiFi on SDIO0.
- TCA8418 keyboard detection on I2C4.
- GPIO/pinctrl basics.

2026-07-17 status:

- RM69A10 display passed board validation: `card0-DSI-1` is `connected`, mode
  is `568x1232`, `tdvp-display-smoke` passed, and the panel showed color bars.
- RTL8189FTV passed interface enumeration: the startup script loads `8189fs`
  and `wlan0`/`wlan1` appear. `iw dev wlan0 scan` can scan nearby APs. AP
  association and DHCP remain to be tested.
- An attached Realtek USB 10/100 LAN adapter enumerates as `eth0` through the
  `r8152` driver. `eth0` obtained `192.168.31.86/24` from `192.168.31.1`, and
  device->gateway, host->device, and device->host ping all passed. The
   `be9c30f3...` image enables Dropbear and automatically runs DHCP on `eth0`.
- GT9895/Goodix now registers `/dev/input/event0`. Physical touch coordinates
  and orientation still need `evtest` validation.
- TCA8418 has not ACKed at `0x34` on `/dev/i2c-0` yet. The failure log was
  captured with the detachable keyboard module installed. `tdvp-k230-iomux
  keyboard` has confirmed IO46/47 as I2C4 alt3; the IO43 reset path can pulse
  through `/dev/gpiochip1 line 11`, but retesting still did not produce an ACK
  at `0x34`. The next step is to inspect TCA8418 power, I2C pull-ups, GPIO43
  physical continuity, and the detachable keyboard connector.

Wave C:

- CSI camera path.
- DMA/CMA/reserved memory.
- first BSP camera/display/input smoke tests.

Wave D:

- AI/KPU userspace path.
- LoRa, BLE, LTE/GNSS, audio, battery, fan, AHT20, and other populated board
  capabilities.

The waves define validation order only. They do not remove hardware from
scope.

## Non-Goals For The First Pass

- No desktop Linux.
- No Debian/Ubuntu runtime assumptions.
- No package installation on the target device.
- No full vendor demo environment.
- No application-owned driver setup.
- No direct app use of V4L2, DRM, evdev, ioctl, sysfs, procfs, or vendor MPP
  handles.

## Acceptance Criteria

This bring-up stage is accepted when:

- the SDK baseline is pinned;
- the kernel baseline is pinned;
- the T-Display K230 board delta is documented;
- the image boots reproducibly from SD card;
- Linux reaches BusyBox userspace over serial;
- the rootfs is generated by Buildroot;
- the active DTB is `tdisplay-k230.dtb`, not the CanMV ST7701/FT5306 DTB;
- application-facing code remains behind TDVP BSP/runtime APIs.

## Current Expanded Adaptation Set

The first hardware-complete direction is larger than display-only bring-up.
The current Linux adaptation should include these board-critical devices:

| Hardware | First-pass implementation |
| --- | --- |
| RM69A10 display | `tdisplay-k230.dtb` with RM69A10 timing/init sequence; display smoke passed on 2026-07-17 |
| GT9895 touch | Goodix-compatible DTS node on I2C3 address `0x5d`; Goodix input device is visible, physical touch events pending |
| RTL8189FTV WiFi | SDIO0 enabled plus Buildroot `rtl8189fs`, `iw`, `wireless-regdb`, `wireless_tools`, `wpa_supplicant`; interfaces now appear |
| USB Ethernet | `r8152` can enumerate an attached Realtek USB 10/100 LAN adapter as `eth0`; DHCP and ping passed; the `be9c30f3...` image starts DHCP automatically and enables Dropbear |
| TCA8418 keyboard | I2C4 enabled plus `tdvp-k230-iomux` and `tdvp-keyboard-smoke` with GPIO43 reset; IO46/47 IOMUX is verified, `0x34` ACK remains pending |
| CSI camera foundation | MIPI/CSI2 path kept aligned with the selected K230 Linux SDK baseline |
| GPIO/I2C/SPI/MMC/PWM | kernel foundations and validation tools enabled |
| Battery/fuel gauge, LR2021, GNSS/LTE, fan, speaker, AHT20 | in scope, but DTS nodes wait for bus/address/pin proof |

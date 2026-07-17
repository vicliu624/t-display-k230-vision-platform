# K230 Linux SDK Baseline Extraction

This document records the extracted facts from the pinned
`kendryte/k230_linux_sdk` baseline that TDVP currently uses as the first Linux
adaptation reference.

Chinese version:

- [k230-linux-sdk-baseline.zh-CN.md](k230-linux-sdk-baseline.zh-CN.md)

Machine-readable lock:

- [sources.lock](sources.lock)

## Source Snapshot

| Item | Value |
| --- | --- |
| Repository | `https://github.com/kendryte/k230_linux_sdk.git` |
| Branch | `dev` |
| Commit | `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` |
| Verified date | 2026-07-16 |
| Local reference checkout | `.tmp/k230_linux_sdk` |

The selected official profile is:

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

This is the closest official K230/CanMV Linux profile for the first TDVP
baseline. It is a reference input, not a complete TDVP image policy.

## Buildroot Profile Facts

Important settings extracted from `k230_canmv_v3_defconfig`:

```text
BR2_riscv=y
BR2_RISCV_ISA_RVC=y
BR2_RISCV_ISA_RVV=y
BR2_TOOLCHAIN_EXTERNAL=y
BR2_TOOLCHAIN_EXTERNAL_PATH="/opt/toolchain/Xuantie-900-gcc-linux-6.6.0-glibc-x86_64-V3.0.2/"
BR2_TOOLCHAIN_EXTERNAL_CUSTOM_PREFIX="riscv64-unknown-linux-gnu"
BR2_TOOLCHAIN_EXTERNAL_HEADERS_6_6=y
BR2_TOOLCHAIN_EXTERNAL_CUSTOM_GLIBC=y
BR2_TARGET_OPTIMIZATION="-mcpu=c908v -mtune=c908 -mrvv-v0p10-compatible  -mrvv-auto-vectorize"
```

TDVP does not automatically inherit the SDK external toolchain as a public
developer requirement. The first TDVP Buildroot profile may still use a minimal
Buildroot toolchain while the kernel/boot baseline is being validated. If C908
optimization, RVV compatibility, or SDK user-space binary compatibility becomes
required, the toolchain policy must be explicitly changed.

## Kernel Baseline

The SDK-selected kernel is:

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
```

The kernel commit was verified with a shallow fetch on 2026-07-16.

Kernel version from the pinned commit:

```text
VERSION = 6
PATCHLEVEL = 6
SUBLEVEL = 36
```

Commit subject:

```text
k230:dts:rtl8189fs wifi
```

The first TDVP defconfig now follows this kernel baseline instead of the
previous upstream Linux experiment.

## Device Tree Facts

The selected SDK DTS names are:

```text
canaan/k230-canmv-v3-lcd
canaan/k230-canmv-v3
```

Observed `k230-canmv-v3-lcd.dts` facts:

| Fact | SDK value |
| --- | --- |
| Model | `Canaan CanMV-K230` |
| Compatible | `canaan,canmv-k230`, `canaan,kendryte-k230` |
| Memory | `0x40000000` bytes |
| stdout-path | `serial0:115200n8` |
| UARTs enabled | `uart0`, `uart3` |
| MMC aliases | `mmc0 = &mmc_sd0`, `mmc1 = &mmc_sd1` |
| Touch baseline | `edt,edt-ft5306` on `i2c3`, GPIO 23/24 |
| Display baseline | `display-st7701-480x800.dtsi` |
| LCD reset/backlight | GPIO 22/25 |
| CSI/MIPI baseline | `mipi0` set to CSI2 |

TDVP must treat the SDK display/touch nodes as reference structure only. The
T-Display K230 hardware delta must replace or adapt them for the board's actual
panel and touch controller.

## Kernel Fragment Facts

The SDK profile adds these Linux fragments:

```text
board/canaan/k230-soc/fragment/linux.fragment
board/canaan/k230-soc/fragment/linux.led_btn
```

Important fragment content:

```text
CONFIG_MEDIA_SUPPORT=y
CONFIG_VIDEO_DEV=y
CONFIG_VIDEO_V4L2=y
CONFIG_V4L2_MEM2MEM_DEV=y
CONFIG_VIDEOBUF2_DMA_CONTIG=y
CONFIG_PINCTRL_K230_IOMUX=y
CONFIG_KEYBOARD_GPIO=y
CONFIG_NEW_LEDS=y
CONFIG_LEDS_CLASS=y
CONFIG_LEDS_GPIO=y
```

These are platform-driver facts, not application APIs. TDVP applications still
must use BSP/runtime APIs instead of V4L2, evdev, GPIO, or LED sysfs paths.

## OpenSBI Facts

The SDK profile selects:

```text
BR2_TARGET_OPENSBI=y
BR2_TARGET_OPENSBI_CUSTOM_VERSION_VALUE="1.4"
BR2_TARGET_OPENSBI_PLAT="generic"
BR2_TARGET_OPENSBI_LINUX_PAYLOAD=y
BR2_TARGET_OPENSBI_ADDITIONAL_VARIABLES="FW_TEXT_START=0"
```

The SDK post-image flow wraps the OpenSBI jump image as:

```text
boot/fw_jump_add_uboot_head.bin
```

TDVP currently treats this as an SDK-derived boot artifact. It is not yet built
by the official Buildroot submodule alone because the SDK carries boot package
overlays.

## U-Boot Facts

The SDK profile selects:

```text
BR2_TARGET_UBOOT=y
BR2_TARGET_UBOOT_BOARDNAME="k230_canmv_v3"
BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2022.10"
```

Important U-Boot defconfig facts:

| Item | Value |
| --- | --- |
| Default device tree | `k230_canmv_v3` |
| SPL text base | `0x80300000` |
| U-Boot text base | `0` |
| Environment offset | `0x1e0000` |
| Environment size | `0x10000` |
| Default load address | `0xc000000` |
| Prompt | `K230# ` |
| Boot delay | `5` in U-Boot defconfig, `1` in SDK default env |
| DRAM profile | `CONFIG_CANMV_V3_LPDDR4_2667=y` |

The SDK board code reports `SYSCTL_BOOT_SDIO1`, which matches the observed
SD-card boot controller used by the T-Display bring-up logs.

## U-Boot Environment Facts

The SDK default environment uses:

```text
bootcmd=run blinux;
console_port=console=ttyS1,115200
mmc_boot_dev_num=1
kernel_addr=0xc100000
loadaddr=0xc000000
dtb_addr=0xa000000
fdt_high=0xa100000
```

The active Linux boot command is:

```text
blinux=ext4load mmc ${mmc_boot_dev_num}:1 0x3000000 /fw_jump_add_uboot_head.bin && ext4load mmc ${mmc_boot_dev_num}:1 0x200000 /${k} && ext4load mmc ${mmc_boot_dev_num}:1 0x2200000 /k.dtb && bootm 0x3000000 - 0x2200000;
```

TDVP has updated `uboot-linux.env` to follow this `blinux` shape. Console
mapping remains a hardware validation item because earlier T-Display serial
logs were observed on the U-Boot console while the SDK environment names
`ttyS1`.

## Image Layout Facts

The SDK `genimage.cfg` uses GPT and these regions:

| Region | Offset | Image |
| --- | --- | --- |
| SPL copy 1 | `1M` | `uboot/fn_u-boot-spl.bin` |
| SPL copy 2 | `0x180000` | `uboot/fn_u-boot-spl.bin` |
| U-Boot env | `0x1e0000` | `uboot/env.env`, reserved size `0x20000` |
| U-Boot | `2M` | `uboot/fn_ug_u-boot.bin` |
| env copy | `0x380000` | `uboot/env.env` |
| boot partition | `30M`, size `80M` | `boot.ext4` |
| rootfs partition | `128M` | `rootfs.ext4` in SDK |

TDVP now mirrors this shape but keeps its minimal Buildroot rootfs output as
`rootfs.ext2` until the rootfs policy changes.

## SDK Packages Not Automatically Imported

The SDK profile enables many validation/demo packages, including AI demos,
OpenCV, Python, networking services, WiFi/Bluetooth packages, RTSP/WebRTC
demos, audio demos, and benchmarking tools.

TDVP must classify each package before admission:

| Class | Rule |
| --- | --- |
| boot-required | allowed in the base image |
| hardware-validation | allowed during bring-up, removable later |
| BSP/runtime-required | allowed after API ownership is clear |
| demo-only | not part of the base image |
| rejected | not imported |

Do not copy the full SDK package set into TDVP.

## Current TDVP Delta

Already applied:

- SDK commit pinned in `sources.lock`.
- Kernel source changed to `linux-xuantie-kernel` commit
  `7d4e1f444f461dbe3833bd99a4640e7b6c2cd529`.
- Kernel headers series changed to 6.6.
- Kernel defconfig changed to `k230`.
- SDK in-tree DTS names selected.
- SD image layout moved to SDK-style GPT + ext4 boot partition.
- U-Boot environment moved to SDK `blinux` shape.

Still pending:

- Build or import SDK-derived SPL, U-Boot, and OpenSBI jump artifacts with
  checksum and provenance.
- Define the real T-Display K230 DTS delta for panel, touch, camera, GPIO,
  power, and optional peripherals.
- Decide whether TDVP must adopt the SDK external Xuantie toolchain.
- Validate console mapping on hardware.
- Validate Linux reaches BusyBox userspace from the SDK-style boot flow.

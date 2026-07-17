# K230 Linux SDK Baseline 提取

本文记录从固定的 `kendryte/k230_linux_sdk` baseline 中提取出的事实。TDVP 当前把这些事实作为
第一版 Linux 适配参考。

英文版本：

- [k230-linux-sdk-baseline.md](k230-linux-sdk-baseline.md)

机器可读 lock：

- [sources.lock](sources.lock)

## Source Snapshot

| 项 | 值 |
| --- | --- |
| Repository | `https://github.com/kendryte/k230_linux_sdk.git` |
| Branch | `dev` |
| Commit | `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7` |
| Verified date | 2026-07-16 |
| Local reference checkout | `.tmp/k230_linux_sdk` |

所选官方 profile：

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

这是第一版 TDVP baseline 最接近的官方 K230/CanMV Linux profile。它是参考输入，不是完整的
TDVP image policy。

## Buildroot Profile Facts

从 `k230_canmv_v3_defconfig` 提取出的重要设置：

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

TDVP 不会自动把 SDK external toolchain 继承成 public developer requirement。第一版 TDVP
Buildroot profile 在验证 kernel/boot baseline 时仍可以使用最小 Buildroot toolchain。
如果 C908 optimization、RVV compatibility 或 SDK userspace binary compatibility 成为必要条件，
toolchain policy 必须显式变更。

## Kernel Baseline

SDK 选择的 kernel：

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
```

这个 kernel commit 已于 2026-07-16 通过 shallow fetch 验证可以获取。

Pinned commit 中的 kernel version：

```text
VERSION = 6
PATCHLEVEL = 6
SUBLEVEL = 36
```

Commit subject：

```text
k230:dts:rtl8189fs wifi
```

第一版 TDVP defconfig 已改为跟随这个 kernel baseline，而不是之前的 upstream Linux 实验。

## Device Tree Facts

SDK 选择的 DTS names：

```text
canaan/k230-canmv-v3-lcd
canaan/k230-canmv-v3
```

已观察到的 `k230-canmv-v3-lcd.dts` 事实：

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

TDVP 必须把 SDK display/touch nodes 当成 reference structure。T-Display K230 硬件 delta
必须根据真实 panel 和 touch controller 进行替换或适配。

## Kernel Fragment Facts

SDK profile 增加了这些 Linux fragments：

```text
board/canaan/k230-soc/fragment/linux.fragment
board/canaan/k230-soc/fragment/linux.led_btn
```

重要内容：

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

这些是 platform-driver facts，不是 application APIs。TDVP applications 仍必须使用 BSP/runtime APIs，
不能直接使用 V4L2、evdev、GPIO 或 LED sysfs paths。

## OpenSBI Facts

SDK profile 选择：

```text
BR2_TARGET_OPENSBI=y
BR2_TARGET_OPENSBI_CUSTOM_VERSION_VALUE="1.4"
BR2_TARGET_OPENSBI_PLAT="generic"
BR2_TARGET_OPENSBI_LINUX_PAYLOAD=y
BR2_TARGET_OPENSBI_ADDITIONAL_VARIABLES="FW_TEXT_START=0"
```

SDK post-image flow 会把 OpenSBI jump image 包装成：

```text
boot/fw_jump_add_uboot_head.bin
```

TDVP 当前把它视为 SDK-derived boot artifact。它还不能假装只靠 official Buildroot submodule
就能直接构建，因为 SDK 带有 boot package overlays。

## U-Boot Facts

SDK profile 选择：

```text
BR2_TARGET_UBOOT=y
BR2_TARGET_UBOOT_BOARDNAME="k230_canmv_v3"
BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2022.10"
```

重要 U-Boot defconfig facts：

| Item | Value |
| --- | --- |
| Default device tree | `k230_canmv_v3` |
| SPL text base | `0x80300000` |
| U-Boot text base | `0` |
| Environment offset | `0x1e0000` |
| Environment size | `0x10000` |
| Default load address | `0xc000000` |
| Prompt | `K230# ` |
| Boot delay | U-Boot defconfig 中为 `5`，SDK default env 中为 `1` |
| DRAM profile | `CONFIG_CANMV_V3_LPDDR4_2667=y` |

SDK board code 报告 `SYSCTL_BOOT_SDIO1`，这与 T-Display bring-up logs 中观察到的 SD-card
boot controller 一致。

## U-Boot Environment Facts

SDK default environment 使用：

```text
bootcmd=run blinux;
console_port=console=ttyS1,115200
mmc_boot_dev_num=1
kernel_addr=0xc100000
loadaddr=0xc000000
dtb_addr=0xa000000
fdt_high=0xa100000
```

Active Linux boot command：

```text
blinux=ext4load mmc ${mmc_boot_dev_num}:1 0x3000000 /fw_jump_add_uboot_head.bin && ext4load mmc ${mmc_boot_dev_num}:1 0x200000 /${k} && ext4load mmc ${mmc_boot_dev_num}:1 0x2200000 /k.dtb && bootm 0x3000000 - 0x2200000;
```

TDVP 已把 `uboot-linux.env` 更新为这个 `blinux` 形态。Console mapping 仍是硬件验证项，
因为早期 T-Display 串口日志来自 U-Boot console，而 SDK environment 使用 `ttyS1`。

## Image Layout Facts

SDK `genimage.cfg` 使用 GPT 和以下区域：

| Region | Offset | Image |
| --- | --- | --- |
| SPL copy 1 | `1M` | `uboot/fn_u-boot-spl.bin` |
| SPL copy 2 | `0x180000` | `uboot/fn_u-boot-spl.bin` |
| U-Boot env | `0x1e0000` | `uboot/env.env`, reserved size `0x20000` |
| U-Boot | `2M` | `uboot/fn_ug_u-boot.bin` |
| env copy | `0x380000` | `uboot/env.env` |
| boot partition | `30M`, size `80M` | `boot.ext4` |
| rootfs partition | `128M` | SDK 中为 `rootfs.ext4` |

TDVP 现在镜像布局跟随这个形态，但最小 Buildroot rootfs 仍输出为 `rootfs.ext2`，直到 rootfs
policy 后续变更。

## SDK Packages Not Automatically Imported

SDK profile 启用了很多 validation/demo packages，包括 AI demos、OpenCV、Python、networking
services、WiFi/Bluetooth packages、RTSP/WebRTC demos、audio demos 和 benchmark tools。

TDVP 必须先分类每个 package 才能准入：

| Class | Rule |
| --- | --- |
| boot-required | 允许进入 base image |
| hardware-validation | bring-up 阶段允许，后续可移除 |
| BSP/runtime-required | API ownership 清晰后允许 |
| demo-only | 不属于 base image |
| rejected | 不导入 |

不要照搬完整 SDK package set。

## Current TDVP Delta

已完成：

- SDK commit 固定到 `sources.lock`。
- Kernel source 改为 `linux-xuantie-kernel` commit
  `7d4e1f444f461dbe3833bd99a4640e7b6c2cd529`。
- Kernel headers series 改为 6.6。
- Kernel defconfig 改为 `k230`。
- 选择 SDK in-tree DTS names。
- SD image layout 改为 SDK-style GPT + ext4 boot partition。
- U-Boot environment 改为 SDK `blinux` 形态。

仍待完成：

- 构建或导入 SDK-derived SPL、U-Boot 和 OpenSBI jump artifacts，并记录 checksum/provenance。
- 为真实 T-Display K230 定义 panel、touch、camera、GPIO、power 和 optional peripherals 的 DTS delta。
- 裁决 TDVP 是否必须采用 SDK external Xuantie toolchain。
- 在硬件上验证 console mapping。
- 验证 SDK-style boot flow 能进入 BusyBox userspace。

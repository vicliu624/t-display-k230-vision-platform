# T-Display K230 Linux 设计说明

本文把当前适配基线映射到 Linux、device tree、driver、Buildroot 和 BSP 工作。

英文版本：

- [linux.md](linux.md)

## Linux 范围

Linux system 是 TDVP 的最小 embedded runtime environment。

它必须提供：

- boot；
- kernel drivers；
- minimal BusyBox userspace；
- BSP/runtime process support；
- deterministic image generation。

它不得提供：

- desktop UI；
- general-purpose distro behavior；
- target-side package management；
- per-app hardware access policy；
- application 对 V4L2、DRM/KMS、evdev、GPIO sysfs、procfs 或 ioctl 的直接依赖。

## Baseline

Linux baseline 来自：

```text
kendryte/k230_linux_sdk
```

目前观察到最接近的官方 profile 是：

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

重要 Linux 参考设置：

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
```

这些值是官方 Linux 起点。TDVP 必须定义 T-Display K230 delta，而不是照抄完整
SDK system behavior。

## 架构目标

```text
Buildroot image
  -> K230 big-core Linux kernel + T-Display K230 DTB
  -> BusyBox userspace
  -> platform init scripts
  -> BSP libraries/services
  -> vision runtime
  -> optional application
```

## Kernel Subsystem 清单

| Area | Required | Notes |
| --- | --- | --- |
| RISC-V K230 platform support | yes | SoC 和 board boot foundation |
| UART serial console | yes | 第一条 serial validation path |
| MMC/SD | yes | boot/rootfs media |
| GPIO/pinctrl | yes | reset、power、interrupt、LED、backlight |
| I2C | yes | touch、camera sensors、HDMI bridge、power devices |
| DRM/KMS 或 vendor display stack | yes | RM69A10 panel path |
| MIPI DSI | yes | RM69A10 和可选 LT9611 |
| Input subsystem | yes | GT9895 touch 和其他 controls |
| V4L2/media controller 或 vendor camera stack | yes | camera pipeline |
| DMA/CMA/reserved memory | yes | camera/display/AI buffers |
| SPI | in scope | LoRa 或 expansion hardware enabled 时 |
| ALSA/audio | in scope | audio/speaker 验证后 |
| USB | in scope | debug、storage 或 network workflows |
| WiFi/BLE/LTE/GNSS | in scope | 取决于 board variant |
| Thermal/fan | in scope | 实际存在时 |
| Battery/charger/fuel gauge | in scope | 实际存在并验证后 |

## Device Tree 工作

TDVP 必须在对比 SDK baseline 与 T-Display 硬件事实后，选择一种 DTS 策略：

1. 从 `canaan/k230-canmv-v3-lcd` 开始并添加 TDVP delta；
2. 从 `canaan/k230-canmv-v3` 开始并添加 panel/touch/camera nodes；
3. 携带 TDVP-owned board DTS，只包含已验证硬件事实。

第一版接受的 DTS 必须描述：

- memory size；
- CPU 和 interrupt controller；
- UART console；
- SD/MMC boot device；
- boot 和 board devices 所需 clock、reset、pinctrl 和 GPIO routing；
- RM69A10 panel path；
- 启用时的 GT9895 touch path；
- 启用时的 first camera path 和 sensor；
- required reserved memory 或 CMA regions。

在 exact board variant、power rails、pins、buses 和 compatible drivers 明确之前，
不要为 optional peripherals 添加节点。

## Display 策略

第一条 display target：

```text
Linux display stack -> RM69A10 AMOLED -> BSP display API
```

规则：

- RM69A10 是第一条 onboard display validation target。
- LT9611 HDMI 作为 alternate display path 保持在范围内，在 panel bring-up 之后验证。
- Application code 只看到 BSP/runtime display APIs。
- DRM/KMS、vendor display handles、connector names 和 plane details 都停在 BSP/runtime
  边界之下。

## Camera 策略

第一条 camera target：

```text
CSI route -> selected sensor -> Linux camera stack -> BSP camera API -> runtime
```

规则：

- 先验证一个 sensor 和一个 mode。
- 在接通 camera、AI 和 display 前定义 buffer ownership。
- V4L2/media/vendor handles 保持在 BSP 或 runtime internals 内部。

## AI/KPU 策略

即使实现使用 Canaan SDK libraries，AI userspace contract 也属于 TDVP。

未决工作：

- 识别 `k230_linux_sdk` 中官方 Linux AI/KPU packages；
- 决定哪些 packages 是 platform dependencies，哪些只是 demo-only；
- 定义稳定的 `ai_load_model` 和 `ai_run` APIs；
- 定义 model file placement；
- 定义 camera frames、AI tensors 和 display overlays 之间的 memory ownership。

## Root Filesystem 策略

最小 rootfs：

- BusyBox；
- init scripts；
- platform configuration；
- BSP libraries/services；
- vision runtime；
- 明确选择时的 validation demos。

Base image 中禁止：

- desktop environment；
- target package manager；
- full vendor demo package set；
- 把 V4L2/DRM/ioctl direct app access 文档化为正常用法；
- shell-heavy app deployment workflow。

## BSP 边界

Linux internals 止步于 BSP 之下。

Applications 可以调用：

```c
camera_open();
camera_read();
display_init();
display_present();
input_read();
```

Applications 不得依赖：

```text
/dev/video*
/dev/dri/*
/dev/input/*
ioctl request values
media-ctl graph names
DRM connector names
sensor driver private controls
vendor MPP handles
```

这是 Linux port 中最重要的设计线。

## 未决决策

下一轮实现前必须解决：

1. exact `k230_linux_sdk` revision；
2. exact external kernel revision；
3. selected DTS starting point；
4. T-Display K230 的 TDVP DTS delta；
5. toolchain choice：跟随 SDK external toolchain，还是沿用现有 Buildroot internal toolchain policy；
6. OpenSBI 和 U-Boot ownership：通过 SDK/Buildroot 构建，还是导入 pinned artifacts；
7. minimal rootfs package set；
8. 第一条 display、touch 和 camera validation targets。


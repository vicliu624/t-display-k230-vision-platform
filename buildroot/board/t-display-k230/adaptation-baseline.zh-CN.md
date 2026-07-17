# T-Display K230 适配基线

本文定义当前项目基线。

英文版本：

- [adaptation-baseline.md](adaptation-baseline.md)

## 目标

本项目以 `kendryte/k230_linux_sdk` 作为主要官方 Linux 参考，把 T-Display K230
适配成一个最小 Buildroot Linux 视觉平台。

这个平台不是通用 Linux 发行版，也不是 demo 集合。它必须产出确定性的系统镜像、
稳定的 BSP/runtime API，以及隐藏 Linux 和 vendor internals 的开发者 SDK。

## 参考源

| 来源 | 角色 |
| --- | --- |
| `kendryte/k230_linux_sdk` | K230 大核 Linux boot、kernel、DTS、OpenSBI、U-Boot、image layout 和 driver assumptions 的主要 Linux baseline。 |
| `Xinyuan-LilyGO/T-Display-K230_canmv_rt` | T-Display K230 板级硬件参考，提供 panel、touch、camera、GPIO、schematic 和板级 wiring facts。 |
| upstream Linux | vendor Linux baseline 明确后的后续 upstream comparison 和 forward-port target。 |
| official Buildroot submodule | 通用 Buildroot 构建引擎。平台策略保留在外层 `buildroot/` external tree。 |

已固定的 `k230_linux_sdk` snapshot：

```text
repository: https://github.com/kendryte/k230_linux_sdk
branch: dev
commit: 5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7
verified: 2026-07-16
```

提取出的 baseline facts 记录在
[k230-linux-sdk-baseline.zh-CN.md](k230-linux-sdk-baseline.zh-CN.md)，外部输入 lock 记录在
[sources.lock](sources.lock)。

## 架构边界

公开平台是：

```text
Buildroot Linux on K230 big core
  -> BSP
  -> Vision Runtime
  -> SDK
  -> applications
```

Application layer 不应该知道某项能力背后是 Linux driver、vendor userspace library，
还是未来的小核 companion service。这些细节属于 BSP/runtime 以下。

Application 不得使用：

```text
/dev/video*
/dev/dri/*
/dev/input/*
ioctl request values
sysfs/procfs implementation paths
vendor MPP handles
OpenSBI, U-Boot, or boot_baremetal details
```

## 适配策略

第一版平台镜像从官方 Linux SDK 形态出发，然后裁剪并适配到 T-Display K230。

工作顺序：

1. 固定 `k230_linux_sdk` revision，并识别最接近的 K230/CanMV Linux profile。
2. 提取官方 Linux boot chain、kernel、DTS、OpenSBI、U-Boot、genimage 和 rootfs facts。
3. 将这些事实与 LilyGO T-Display K230 硬件事实对比。
4. 创建 T-Display K230 delta，包括 DTS、kernel config、image layout、U-Boot
   environment 和 rootfs package selection。
5. Base image 只保留平台必需 packages。
6. 分波次验证硬件：boot/UART/SD 优先，display/touch 其次，camera 和 DMA/CMA
   再其次，AI 和可选外设在基础系统稳定后推进。

## 第一版 Linux Baseline

目前观察到最接近的官方 profile 是：

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

重要参考设置：

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
BR2_TARGET_OPENSBI_CUSTOM_VERSION_VALUE="1.4"
BR2_TARGET_UBOOT_BOARDNAME="k230_canmv_v3"
BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2022.10"
```

这些设置是起点参考，不表示可以照抄完整 SDK configuration。Demo packages、Python
packages、network services、media demos 和 AI demos 只有在服务平台契约或明确标记为
validation step 时，才能进入 TDVP。

## 成功标准

第一版被接受的 baseline 必须证明：

```text
SD boot
  -> Canaan-compatible SPL/U-Boot/OpenSBI path
  -> K230 big-core Linux
  -> BusyBox rootfs
  -> TDVP platform init
```

只有满足以下条件后才算接受：

- SDK、kernel、OpenSBI、U-Boot 和 Buildroot 的 exact revisions 已记录；
- T-Display K230 DTS differences 已记录；
- SD image 可以可复现启动；
- serial console 进入 userspace；
- rootfs 正常挂载；
- application code 只使用 TDVP BSP/runtime/SDK APIs；
- vendor demo packages 被移除，或明确标记为 validation-only。

## 不可破坏规则

- 不照搬整个 `k230_linux_sdk` package set。
- 不向 applications 暴露 Linux device nodes、ioctl values、sysfs paths、media graph
  names 或 vendor handles。
- 不把移动 branch 当作 firmware input。
- 不把 LilyGO RT-Smart application behavior 当成 Linux platform behavior。
- 不把小核做成第二个公开 application platform。

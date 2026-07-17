# T-Display K230 Kernel 选型

本文记录 T-Display K230 Vision Platform 当前的 kernel 选型规则。

英文版本：

- [kernel-selection.md](kernel-selection.md)

## 决策

第一版 Linux kernel baseline 必须来自：

```text
kendryte/k230_linux_sdk
```

平台不再把 upstream release candidate 选为第一轮 bring-up kernel。Upstream Linux
是官方 K230 Linux baseline 明确之后的后续对照和 forward-port target。

## 原因

当前目标是适配 T-Display K230，而不是在板级 Linux baseline 成立之前先证明一条干净的
upstream kernel 路线。

`k230_linux_sdk` 已经定义了 K230 大核 Linux 集成模型：

- Buildroot overlay 结构。
- External K230/Xuantie kernel source。
- K230 kernel defconfig。
- K230 in-tree DTS names。
- OpenSBI 集成。
- U-Boot 集成。
- SD-card image layout。
- Linux rootfs layout。

这些才是第一轮应该适配的事实。

## 参考 Profile

目前观察到最接近的官方 profile 是：

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

从该 profile 观察到的 kernel 和 boot 设置：

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
BR2_TARGET_OPENSBI_CUSTOM_VERSION_VALUE="1.4"
BR2_TARGET_UBOOT_BOARDNAME="k230_canmv_v3"
BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2022.10"
```

这些是参考输入。TDVP 必须把它们适配到 T-Display K230，而不是照抄完整 SDK
configuration。

## 必需 TDVP Delta

第一轮 kernel-selection 工作是定义官方 K230/CanMV Linux profile 与 T-Display K230
之间的 delta：

| Area | Required decision |
| --- | --- |
| SDK revision | 需要固定的 exact `k230_linux_sdk` commit |
| Kernel revision | 所选 SDK profile 使用的 exact external kernel commit |
| Kernel defconfig | SDK `k230` defconfig 是否足够，或是否需要 TDVP fragment |
| DTS | 从 `k230-canmv-v3-lcd`、`k230-canmv-v3` 还是 TDVP board DTS 开始 |
| Display | RM69A10 panel enablement，以及 T-Display-specific reset/power/backlight facts |
| Touch | GT9895 bus、reset、interrupt 和 driver compatibility |
| Camera | 第一条 CSI/sensor route 和所需 memory reservation |
| Rootfs | 最小 TDVP package set，除非用于 validation，否则排除 SDK demos |
| Image layout | 兼容 Canaan Linux boot path 和 TDVP rootfs 的 SD-card layout |

## 规则

- 使用任何 SDK、kernel、OpenSBI 或 U-Boot 输入前，必须固定 exact revisions。
- vendor driver handles 必须停在 BSP/runtime 边界之下。
- application APIs 必须独立于 kernel、DTS、media graph 和 ioctl details。
- base image 不导入完整 SDK demo package set。
- 除非本文被明确再次修订，否则不把更新的 upstream kernel 选作第一 baseline。

## 验收门槛

Kernel baseline 只有满足以下条件后才算接受：

1. SDK revision 已固定。
2. Kernel revision 已固定。
3. Kernel 可以通过平台 Buildroot flow 可复现构建。
4. 所选 DTB 可以追溯到 T-Display K230 硬件事实。
5. U-Boot/OpenSBI 可以 hand off 到大核 Linux。
6. Linux 可以从 SD-card rootfs 进入 BusyBox userspace。
7. 没有 application code 依赖 Linux internals。


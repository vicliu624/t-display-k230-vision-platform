# T-Display K230 板级文档

本目录记录 T-Display K230 Vision Platform Buildroot 移植所需的板级事实。

本目录的目的不是记录上游 CanMV/RT-Smart 应用栈。它是本项目最小 Linux
系统、BSP 和 vision runtime 使用的适配层。

英文版本：

- [README.md](README.md)

## 事实来源

主要 Linux 适配参考：

- 仓库：https://github.com/kendryte/k230_linux_sdk
- 已固定的参考 branch：`dev`
- 已固定的参考 commit：`5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7`
- 验证日期：2026-07-16

该仓库中的重要参考路径：

- `README.md`
- `buildroot-overlay/configs/k230_canmv_v3_defconfig`
- `buildroot-overlay/board/canaan/k230-soc/default.env`
- `buildroot-overlay/board/canaan/k230-soc/genimage.cfg`
- `buildroot-overlay/board/canaan/k230-soc/post-image.sh`

主要 T-Display 硬件参考：

- 仓库：https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt
- 已检查的参考 commit：`111b67743f0c238b717ae502a4b4a9638c45f691`
- 硬件参考检查日期：2026-06-14

该仓库中的重要参考路径：

- `README.md`
- `README_CN.md`
- `schematic/T-Display K230 V1.0.pdf`
- `canmv_k230/configs/k230_canmv_v3p0_defconfig`
- `canmv_k230/boards/k230_canmv_v3p0/default.env`
- `canmv_k230/boards/k230_canmv_v3p0/genimage-sdcard.cfg`
- `canmv_k230/src/rtsmart/mpp/kernel/connector/src/rm69a10.c`
- `canmv_k230/src/rtsmart/mpp/kernel/connector/src/lt9611.c`
- `canmv_k230/src/rtsmart/mpp/kernel/sensor/src/sensor_dev.c`
- `canmv_k230/src/rtsmart/rtsmart/kernel/bsp/maix3/drivers/Kconfig`

## 文档

- [adaptation-baseline.zh-CN.md](adaptation-baseline.zh-CN.md)：当前项目目标、
  参考源角色，以及基于 `k230_linux_sdk` 的适配策略。
- [k230-linux-sdk-baseline.zh-CN.md](k230-linux-sdk-baseline.zh-CN.md)：从固定 K230
  Linux SDK profile 中提取出的事实。
- [sources.lock](sources.lock)：机器可读的外部 source 和 artifact baseline lock。
- [hardware.zh-CN.md](hardware.zh-CN.md)：板级硬件清单和引脚事实。
- [boot.zh-CN.md](boot.zh-CN.md)：启动事实、串口事实和 Linux 启动策略。
- [boot-artifact-decision.zh-CN.md](boot-artifact-decision.zh-CN.md)：boot artifacts 的接受、
  拒绝和延后裁决。
- [bootloader.zh-CN.md](bootloader.zh-CN.md)：bootloader artifact 的接受标准、
  provenance 和集成规则。
- [image.zh-CN.md](image.zh-CN.md)：SD 镜像布局参考和本项目的 Buildroot 镜像策略。
- [uboot-env.zh-CN.md](uboot-env.zh-CN.md)：平台自有 Linux U-Boot environment source、
  生成方式和验证规则。
- [linux.zh-CN.md](linux.zh-CN.md)：Linux kernel、device tree、driver 和 BSP 影响。
- [kernel-policy.zh-CN.md](kernel-policy.zh-CN.md)：kernel headers 与 target kernel 版本策略。
- [kernel-selection.zh-CN.md](kernel-selection.zh-CN.md)：基于官方 K230 Linux SDK
  路径的当前 kernel baseline。
- [bringup-plan.zh-CN.md](bringup-plan.zh-CN.md)：第一轮 Linux bring-up 策略和后续实现步骤。
- [artifacts.zh-CN.md](artifacts.zh-CN.md)：官方 firmware artifact 清单和 image layout 检查。
- [rootfs.zh-CN.md](rootfs.zh-CN.md)：第一版 Buildroot-owned root filesystem 构建和验收记录。
- [validation.zh-CN.md](validation.zh-CN.md)：bring-up 和验收清单。

英文原文：

- [k230-linux-sdk-baseline.md](k230-linux-sdk-baseline.md)
- [hardware.md](hardware.md)
- [boot.md](boot.md)
- [boot-artifact-decision.md](boot-artifact-decision.md)
- [bootloader.md](bootloader.md)
- [image.md](image.md)
- [uboot-env.md](uboot-env.md)
- [linux.md](linux.md)
- [kernel-policy.md](kernel-policy.md)
- [kernel-selection.md](kernel-selection.md)
- [bringup-plan.md](bringup-plan.md)
- [artifacts.md](artifacts.md)
- [rootfs.md](rootfs.md)
- [validation.md](validation.md)

## 边界规则

`kendryte/k230_linux_sdk` 是主要官方 Linux 适配参考。它可以为第一版 Linux baseline
定义 boot-flow、Buildroot-overlay、kernel、DTS、OpenSBI、U-Boot 和 image-layout 事实。

LilyGO 仓库只作为 T-Display 板级事实来源。

我们可以复用 display controller、touch controller、GPIO 编号、CSI 走线、
镜像 offset、boot environment value 等事实。我们不继承它的 application
model、CanMV UI model、MicroPython model 或 RT-Smart 分裂架构作为本项目的平台架构。

Upstream Linux 是后续 forward-port 和 upstream-comparison target。除非后续有明确新决策，
否则它不是第一轮 bring-up baseline。

对本项目来说：

- Buildroot 负责确定性的系统镜像生成。
- Linux 负责 kernel、driver、device tree 和最小用户空间。
- BSP 负责稳定的硬件 API。
- Vision runtime 负责 camera、AI、overlay、display、buffer 和 event loop。
- Application 不得直接依赖 `/dev/video*`、DRM node、`ioctl` 或 vendor device path。

## 事实质量标签

本目录中的板级事实使用以下标签：

- `confirmed` / 已确认：来自 schematic、config、board README 或 driver source。
- `reference-only` / 仅供参考：来自 RT-Smart/CanMV 镜像流程，有参考价值，但还不是 Linux 契约。
- `needs-validation` / 需验证：从来源看合理，但必须在硬件上证明。
- `linux-decision` / Linux 平台决策：本平台选择的策略，不是从参考仓库继承的事实。

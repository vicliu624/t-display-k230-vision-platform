# T-Display K230 Vision Platform

T-Display K230 Vision Platform 是面向 LilyGO T-Display K230 的系统级 embedded
Linux platform SDK。

英文版本：

- [README.md](README.md)

它不是单个 camera app、AI demo、desktop Linux image，也不是通用 Linux distribution。
本项目的目标是提供一个确定性的 Buildroot 系统镜像、严格的 BSP 抽象层、可复用的
vision runtime，以及让应用在不接触 Linux internals 的情况下使用 camera、display、
input、AI 和板级能力的 developer SDK。

## 当前 Baseline

当前 Linux 适配策略是：

```text
kendryte/k230_linux_sdk
  -> 最接近的 K230/CanMV Linux profile
  -> T-Display K230 硬件差异
  -> TDVP Buildroot board profile
  -> BSP/runtime/SDK contract
```

参考源角色：

- `kendryte/k230_linux_sdk` 是主要 Linux boot、kernel、DTS、OpenSBI、U-Boot、
  Buildroot-overlay 和 image-layout 参考。
- `Xinyuan-LilyGO/T-Display-K230_canmv_rt` 只作为 T-Display 硬件事实参考。
- Upstream Linux 是后续对照和 forward-port target，不是第一轮 bring-up baseline。

平台不把 RT-Smart、CanMV UI、MicroPython、V4L2、DRM/KMS、evdev、ioctl、sysfs、
procfs 或 raw device node 暴露为 application contract。

## 架构

```text
+--------------------------------------------------+
| Layer 4: Applications                            |
| Optional apps and demos                          |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| Layer 3: Developer SDK                           |
| examples, templates, CLI tools, documentation    |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| Layer 2: Vision Runtime                          |
| pipeline, buffers, AI wrapper, event loop        |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| Layer 1: BSP Hardware Abstraction Layer          |
| camera, display, input, storage, radio, sensors  |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| Layer 0: Buildroot Linux                         |
| Linux kernel, BusyBox rootfs, boot image         |
+--------------------------+-----------------------+
                           |
+--------------------------v-----------------------+
| T-Display K230 Hardware                          |
+--------------------------------------------------+
```

## 核心规则

- Buildroot 负责确定性的系统镜像生成。
- Linux 负责 kernel、drivers、device tree 和最小 userspace。
- BSP 负责稳定硬件 API，并隐藏 kernel/device details。
- Vision runtime 负责 camera 到 AI 到 display 的执行模型。
- SDK 负责 examples、templates、tools 和 developer onboarding。
- Applications 只能使用 public platform APIs。

## 仓库结构

```text
t-display-k230-vision-platform/
|-- buildroot/
|   |-- buildroot/          # official Buildroot source submodule
|   |-- configs/            # platform defconfigs
|   |-- board/              # board adaptation documents and image scripts
|   |-- package/            # platform packages
|   `-- rootfs_overlay/     # rootfs overlay
|-- bsp/                    # hardware abstraction APIs and implementations
|-- runtime/                # vision runtime
|-- sdk/                    # examples, templates, tools
|-- apps/                   # optional applications and demos
`-- docs/                   # platform architecture and API docs
```

## 关键文档

- `docs/architecture.md` / `docs/architecture.zh-CN.md`：平台架构和分层契约。
- `docs/api.md` / `docs/api.zh-CN.md`：BSP/runtime API 契约和抽象规则。
- `docs/getting_started.md` / `docs/getting_started.zh-CN.md`：build、flash 和 first-demo workflow。
- `buildroot/README.md` / `buildroot/README.zh-CN.md`：Buildroot 层契约。
- `buildroot/CUSTOMIZATION.md` / `buildroot/CUSTOMIZATION.zh-CN.md`：如何定制 Linux system。
- `buildroot/board/t-display-k230/README.md` / `README.zh-CN.md`：板级事实来源和适配边界。
- `buildroot/board/t-display-k230/adaptation-baseline.md` / `adaptation-baseline.zh-CN.md`：
  当前 `k230_linux_sdk` based adaptation baseline。

## Buildroot Source Model

Official Buildroot 作为 submodule 跟踪：

```text
buildroot/buildroot/
```

平台代码位于外层 `buildroot/`，作为 `BR2_EXTERNAL` tree。不要为了平台定制修改
official Buildroot files。

初始化 submodule：

```sh
git submodule update --init --depth 1 buildroot/buildroot
```

配置并构建：

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

期望镜像：

```text
output/t_display_k230_vision/images/sysimage-sdcard.img
```

## 非目标

本项目不得变成：

- Desktop Linux system。
- Debian/Ubuntu derivative。
- Package-manager based runtime。
- Per-app demo collection。
- 直接暴露给 application developers 的 raw Linux BSP。
- Public RTOS/Linux split ecosystem。

## 一句话定义

```text
Buildroot 提供系统确定性。
BSP 提供硬件抽象。
Runtime 提供视觉与 AI 执行。
SDK 提供开发者体验。
```

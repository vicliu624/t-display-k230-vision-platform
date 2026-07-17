# T-Display K230 Vision Platform

T-Display K230 Vision Platform is a system-level embedded Linux platform SDK
for the LilyGO T-Display K230.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

It is not a single camera application, AI demo, desktop Linux image, or
general-purpose distribution. The project goal is to provide a deterministic
Buildroot-based system image, a strict BSP abstraction layer, a reusable vision
runtime, and a developer SDK that lets applications use camera, display, input,
AI, and board capabilities without touching Linux internals.

## Current Baseline

The current Linux adaptation strategy is:

```text
kendryte/k230_linux_sdk
  -> closest K230/CanMV Linux profile
  -> T-Display K230 hardware delta
  -> TDVP Buildroot board profile
  -> BSP/runtime/SDK contract
```

Reference roles:

- `kendryte/k230_linux_sdk` is the primary Linux boot, kernel, DTS, OpenSBI,
  U-Boot, Buildroot-overlay, and image-layout reference.
- `Xinyuan-LilyGO/T-Display-K230_canmv_rt` is a T-Display hardware fact
  reference only.
- Upstream Linux is a later comparison and forward-port target, not the first
  bring-up baseline.

The platform does not expose RT-Smart, CanMV UI, MicroPython, V4L2, DRM/KMS,
evdev, ioctl, sysfs, procfs, or raw device nodes as application contracts.

## Architecture

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

## Core Rules

- Buildroot owns deterministic system image generation.
- Linux owns kernel, drivers, device tree, and minimal userspace.
- BSP owns stable hardware APIs and hides kernel/device details.
- Vision runtime owns the camera to AI to display execution model.
- SDK owns examples, templates, tools, and developer onboarding.
- Applications must use public platform APIs only.

## Repository Layout

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

## Key Documents

- `docs/architecture.md`: full platform architecture and layer contract.
- `docs/api.md`: BSP/runtime API contract and abstraction rules.
- `docs/getting_started.md`: build, flash, and first-demo workflow.
- `buildroot/README.md`: Buildroot layer contract.
- `buildroot/CUSTOMIZATION.md`: how to customize the Linux system.
- `buildroot/board/t-display-k230/README.md`: board-specific source-of-truth
  map and adaptation boundary.
- `buildroot/board/t-display-k230/adaptation-baseline.md`: current
  `k230_linux_sdk` based adaptation baseline.

## Buildroot Source Model

Official Buildroot is tracked as a submodule:

```text
buildroot/buildroot/
```

The platform code lives in the outer `buildroot/` directory as a
`BR2_EXTERNAL` tree. Do not edit official Buildroot files for platform
customization.

Initialize the submodule:

```sh
git submodule update --init --depth 1 buildroot/buildroot
```

Configure and build:

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

Expected image:

```text
output/t_display_k230_vision/images/sysimage-sdcard.img
```

## Non-Goals

This project must not become:

- A desktop Linux system.
- A Debian/Ubuntu derivative.
- A package-manager based runtime.
- A per-app demo collection.
- A raw Linux BSP exposed directly to application developers.
- A public RTOS/Linux split ecosystem.

## One-Line Definition

```text
Buildroot provides system determinism.
BSP provides hardware abstraction.
Runtime provides vision and AI execution.
SDK provides developer experience.
```

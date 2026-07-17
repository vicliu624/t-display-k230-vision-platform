# T-Display K230 Linux Design Notes

This document maps the current adaptation baseline into Linux, device-tree,
driver, Buildroot, and BSP work.

Chinese version:

- [linux.zh-CN.md](linux.zh-CN.md)

## Linux Scope

The Linux system is a minimal embedded runtime environment for TDVP.

It must provide:

- boot;
- kernel drivers;
- minimal BusyBox userspace;
- BSP/runtime process support;
- deterministic image generation.

It must not provide:

- desktop UI;
- general-purpose distro behavior;
- target-side package management;
- per-app hardware access policy;
- direct application dependency on V4L2, DRM/KMS, evdev, GPIO sysfs, procfs, or
  ioctl.

## Baseline

The Linux baseline is derived from:

```text
kendryte/k230_linux_sdk
```

The closest observed official profile is:

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

Important Linux reference settings:

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
```

These values are the official Linux starting point. TDVP must define the
T-Display K230 delta instead of copying the full SDK system behavior.

## Architecture Target

```text
Buildroot image
  -> K230 big-core Linux kernel + T-Display K230 DTB
  -> BusyBox userspace
  -> platform init scripts
  -> BSP libraries/services
  -> vision runtime
  -> optional application
```

## Kernel Subsystem Checklist

| Area | Required | Notes |
| --- | --- | --- |
| RISC-V K230 platform support | yes | SoC and board boot foundation |
| UART serial console | yes | first serial validation path |
| MMC/SD | yes | boot/rootfs media |
| GPIO/pinctrl | yes | reset, power, interrupt, LED, backlight |
| I2C | yes | touch, camera sensors, HDMI bridge, power devices |
| DRM/KMS or vendor display stack | yes | RM69A10 panel path |
| MIPI DSI | yes | RM69A10 and optional LT9611 |
| Input subsystem | yes | GT9895 touch and other controls |
| V4L2/media controller or vendor camera stack | yes | camera pipeline |
| DMA/CMA/reserved memory | yes | camera/display/AI buffers |
| SPI | in scope | LoRa or expansion hardware when enabled |
| ALSA/audio | in scope | audio/speaker when validated |
| USB | in scope | debug, storage, or network workflows |
| WiFi/BLE/LTE/GNSS | in scope | board-variant dependent |
| Thermal/fan | in scope | when populated |
| Battery/charger/fuel gauge | in scope | when populated and validated |

## Device Tree Work

TDVP must choose one of these DTS strategies after comparing the SDK baseline
with T-Display hardware facts:

1. start from `canaan/k230-canmv-v3-lcd` and add a TDVP delta;
2. start from `canaan/k230-canmv-v3` and add panel/touch/camera nodes;
3. carry a TDVP-owned board DTS that includes only validated hardware facts.

The first accepted DTS must describe:

- memory size;
- CPU and interrupt controller;
- UART console;
- SD/MMC boot device;
- clock, reset, pinctrl, and GPIO routing needed for boot and board devices;
- RM69A10 panel path;
- GT9895 touch path when enabled;
- first camera path and sensor when enabled;
- required reserved memory or CMA regions.

Do not add nodes for optional peripherals before the exact board variant,
power rails, pins, buses, and compatible drivers are known.

## Display Strategy

First display target:

```text
Linux display stack -> RM69A10 AMOLED -> BSP display API
```

Rules:

- RM69A10 is the first onboard display validation target.
- LT9611 HDMI remains in scope as an alternate display path after panel bring-up.
- Application code sees only BSP/runtime display APIs.
- DRM/KMS, vendor display handles, connector names, and plane details stay
  below the BSP/runtime boundary.

## Camera Strategy

First camera target:

```text
CSI route -> selected sensor -> Linux camera stack -> BSP camera API -> runtime
```

Rules:

- Validate one sensor and one mode first.
- Define buffer ownership before joining camera, AI, and display.
- Keep V4L2/media/vendor handles inside BSP or runtime internals.

## AI/KPU Strategy

The AI userspace contract belongs to TDVP, even if the implementation uses
Canaan SDK libraries.

Open work:

- identify the official Linux AI/KPU packages required from `k230_linux_sdk`;
- decide which packages are platform dependencies and which are demo-only;
- define stable `ai_load_model` and `ai_run` APIs;
- define model file placement;
- define memory ownership between camera frames, AI tensors, and display
  overlays.

## Root Filesystem Policy

Minimum rootfs:

- BusyBox;
- init scripts;
- platform configuration;
- BSP libraries/services;
- vision runtime;
- validation demos only when explicitly selected.

Forbidden in the base image:

- desktop environment;
- target package manager;
- full vendor demo package set;
- direct app access documentation for V4L2/DRM/ioctl as normal use;
- shell-heavy app deployment workflow.

## BSP Boundary

Linux internals end below the BSP.

Applications may call:

```c
camera_open();
camera_read();
display_init();
display_present();
input_read();
```

Applications must not rely on:

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

This is the most important design line in the Linux port.

## Open Decisions

Before the next implementation stage, resolve:

1. exact `k230_linux_sdk` revision;
2. exact external kernel revision;
3. selected DTS starting point;
4. TDVP DTS delta for T-Display K230;
5. toolchain choice and whether it follows the SDK external toolchain or the
   existing Buildroot internal toolchain policy;
6. OpenSBI and U-Boot ownership: build through SDK/Buildroot or import pinned
   artifacts;
7. minimal rootfs package set;
8. first display, touch, and camera validation targets.


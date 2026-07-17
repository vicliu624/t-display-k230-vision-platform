# T-Display K230 Adaptation Baseline

This document defines the current project baseline.

Chinese version:

- [adaptation-baseline.zh-CN.md](adaptation-baseline.zh-CN.md)

## Goal

The project adapts T-Display K230 into a minimal Buildroot-based Linux vision
platform by using `kendryte/k230_linux_sdk` as the primary official Linux
reference.

The platform is not a general Linux distribution and not a demo collection. It
must produce a deterministic system image, a stable BSP/runtime API, and a
developer SDK that hides Linux and vendor internals from applications.

## Reference Sources

| Source | Role |
| --- | --- |
| `kendryte/k230_linux_sdk` | Primary Linux baseline for K230 big-core Linux boot, kernel, DTS, OpenSBI, U-Boot, image layout, and driver assumptions. |
| `Xinyuan-LilyGO/T-Display-K230_canmv_rt` | T-Display K230 board hardware reference for panel, touch, camera, GPIO, schematic, and board-specific wiring facts. |
| upstream Linux | Future upstream comparison and forward-port target after the vendor Linux baseline is understood. |
| official Buildroot submodule | Generic Buildroot build engine. Platform policy stays in the outer `buildroot/` external tree. |

Pinned `k230_linux_sdk` snapshot:

```text
repository: https://github.com/kendryte/k230_linux_sdk
branch: dev
commit: 5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7
verified: 2026-07-16
```

The extracted baseline facts are recorded in
[k230-linux-sdk-baseline.md](k230-linux-sdk-baseline.md), and the external input
lock is recorded in [sources.lock](sources.lock).

## Architecture Boundary

The public platform is:

```text
Buildroot Linux on K230 big core
  -> BSP
  -> Vision Runtime
  -> SDK
  -> applications
```

The application layer must not know whether a capability is implemented through
Linux drivers, vendor user-space libraries, or a future small-core companion
service. Those details belong below BSP/runtime.

Applications must not use:

```text
/dev/video*
/dev/dri/*
/dev/input/*
ioctl request values
sysfs/procfs implementation paths
vendor MPP handles
OpenSBI, U-Boot, or boot_baremetal details
```

## Adaptation Strategy

The first platform image is derived from the official Linux SDK shape, then
reduced and adapted for T-Display K230.

Work order:

1. Pin the `k230_linux_sdk` revision and identify the nearest K230/CanMV Linux
   profile.
2. Extract the official Linux boot chain, kernel, DTS, OpenSBI, U-Boot,
   genimage, and rootfs facts.
3. Compare those facts with LilyGO T-Display K230 hardware facts.
4. Create a T-Display K230 delta for DTS, kernel config, image layout, U-Boot
   environment, and rootfs package selection.
5. Keep only platform-required packages in the base image.
6. Validate hardware in waves: boot/UART/SD first, display/touch next, camera
   and DMA/CMA next, AI and optional peripherals after the base system is
   stable.

## First Linux Baseline

The closest observed official profile is:

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

Important reference settings:

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
BR2_TARGET_OPENSBI_CUSTOM_VERSION_VALUE="1.4"
BR2_TARGET_UBOOT_BOARDNAME="k230_canmv_v3"
BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2022.10"
```

These settings are the starting reference, not a license to copy the full SDK
configuration. Demo packages, Python packages, network services, media demos,
and AI demos must be admitted into TDVP only when they serve the platform
contract or a clearly labeled validation step.

## Success Criteria

The first accepted baseline must prove:

```text
SD boot
  -> Canaan-compatible SPL/U-Boot/OpenSBI path
  -> K230 big-core Linux
  -> BusyBox rootfs
  -> TDVP platform init
```

It is accepted only when:

- exact SDK, kernel, OpenSBI, U-Boot, and Buildroot revisions are recorded;
- T-Display K230 DTS differences are documented;
- the image boots reproducibly from SD;
- serial console reaches userspace;
- rootfs mounts cleanly;
- application code uses only TDVP BSP/runtime/SDK APIs;
- vendor demo packages are removed or explicitly marked validation-only.

## Non-Negotiable Rules

- Do not copy the whole `k230_linux_sdk` package set into TDVP.
- Do not expose Linux device nodes, ioctl values, sysfs paths, media graph
  names, or vendor handles to applications.
- Do not use a moving branch as a firmware input.
- Do not treat LilyGO RT-Smart application behavior as Linux platform behavior.
- Do not make the small core a second public application platform.

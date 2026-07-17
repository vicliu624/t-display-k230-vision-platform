# T-Display K230 Kernel Selection

This document records the current kernel-selection rule for the T-Display K230
Vision Platform.

Chinese version:

- [kernel-selection.zh-CN.md](kernel-selection.zh-CN.md)

## Decision

The first Linux kernel baseline must be derived from:

```text
kendryte/k230_linux_sdk
```

The platform no longer selects an upstream release candidate as the first
bring-up kernel. Upstream Linux is a later comparison and forward-port target
after the official K230 Linux baseline is understood.

## Why

The goal is to adapt T-Display K230, not to prove a clean upstream kernel path
before the board has a working Linux baseline.

`k230_linux_sdk` already defines a K230 big-core Linux integration model:

- Buildroot overlay structure.
- External K230/Xuantie kernel source.
- K230 kernel defconfig.
- K230 in-tree DTS names.
- OpenSBI integration.
- U-Boot integration.
- SD-card image layout.
- Linux rootfs layout.

Those are the right first facts to adapt.

## Reference Profile

The closest observed official profile is:

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

Reference kernel and boot settings observed from that profile:

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
BR2_TARGET_OPENSBI_CUSTOM_VERSION_VALUE="1.4"
BR2_TARGET_UBOOT_BOARDNAME="k230_canmv_v3"
BR2_TARGET_UBOOT_CUSTOM_VERSION_VALUE="2022.10"
```

These are reference inputs. TDVP must adapt them to T-Display K230 instead of
copying the full SDK configuration.

## Required TDVP Delta

The first kernel-selection work is to define the delta between the official
K230/CanMV Linux profile and T-Display K230:

| Area | Required decision |
| --- | --- |
| SDK revision | exact `k230_linux_sdk` commit to pin |
| Kernel revision | exact external kernel commit used by the selected SDK profile |
| Kernel defconfig | whether the SDK `k230` defconfig is sufficient or needs a TDVP fragment |
| DTS | whether to start from `k230-canmv-v3-lcd`, `k230-canmv-v3`, or a TDVP board DTS |
| Display | RM69A10 panel enablement and T-Display-specific reset/power/backlight facts |
| Touch | GT9895 bus, reset, interrupt, and driver compatibility |
| Camera | first CSI/sensor route and required memory reservation |
| Rootfs | minimal TDVP package set, excluding SDK demos unless used for validation |
| Image layout | SD-card layout compatible with the Canaan Linux boot path and TDVP rootfs |

## Rules

- Pin exact revisions before using any SDK, kernel, OpenSBI, or U-Boot input.
- Keep vendor driver handles below the BSP/runtime boundary.
- Keep application APIs independent of kernel, DTS, media graph, and ioctl
  details.
- Do not import the full SDK demo package set into the base image.
- Do not choose a newer upstream kernel as the first baseline unless this
  document is explicitly revised again.

## Acceptance Gates

A kernel baseline is accepted only when:

1. The SDK revision is pinned.
2. The kernel revision is pinned.
3. The kernel builds reproducibly through the platform Buildroot flow.
4. The selected DTB is traceable to T-Display K230 hardware facts.
5. U-Boot/OpenSBI hands off to Linux on the big core.
6. Linux reaches BusyBox userspace from SD-card rootfs.
7. No application code depends on Linux internals.


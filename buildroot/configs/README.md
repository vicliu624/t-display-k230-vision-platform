# Buildroot Configurations

This directory contains TDVP Buildroot defconfigs exposed through the outer
`buildroot/` BR2_EXTERNAL tree.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

## Purpose

A defconfig defines one reproducible platform system profile:

- target architecture and toolchain policy;
- Linux kernel source and kernel configuration;
- device tree selection;
- root filesystem composition;
- boot/image integration;
- platform packages included in the image.

It is not an application config and not a user preference file.

## Canonical Profile

The canonical profile is:

```text
t_display_k230_vision_defconfig
```

Current baseline:

```text
adapt T-Display K230 from kendryte/k230_linux_sdk
```

The profile must be updated to match the selected official SDK baseline after
the exact SDK/kernel/OpenSBI/U-Boot revisions are pinned.

## Source Rules

- Keep platform defconfigs here, not under `buildroot/buildroot/configs/`.
- Do not edit the official Buildroot submodule for TDVP behavior.
- Do not use moving branches as firmware inputs.
- Do not add demo-specific profiles.
- Do not encode application behavior in the system profile.

## Expected Profile Shape

The canonical profile should select only platform-level requirements:

- RISC-V K230 target.
- Toolchain policy compatible with the selected SDK baseline.
- K230 Linux kernel source and pinned revision.
- K230/T-Display device tree strategy.
- BusyBox userspace.
- deterministic rootfs image.
- deterministic SD-card image generation.
- required host tools for image generation.
- TDVP BSP/runtime/SDK packages when they exist.

The profile should not include by default:

- desktop packages;
- target package managers;
- full vendor demo package set;
- app-specific dependencies;
- broad debug tools as production dependencies.

## Interactive Configuration Flow

Run `menuconfig` from the official Buildroot source tree while pointing
`BR2_EXTERNAL` at the outer platform tree:

```sh
cd buildroot/buildroot
make BR2_EXTERNAL=.. O=../../output/t_display_k230_vision menuconfig
```

Save the canonical defconfig back to this directory:

```sh
make O=../../output/t_display_k230_vision savedefconfig \
  BR2_DEFCONFIG=../configs/t_display_k230_vision_defconfig
```

Rebuild from the repository root:

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

## Review Checklist

Before accepting a defconfig change:

- Is the SDK/kernel/boot input pinned?
- Does the change support the shared platform rather than one app?
- Does it preserve a minimal rootfs?
- Does it avoid desktop and distro assumptions?
- Does it keep hardware access behind BSP/runtime APIs?
- Is any vendor package classified as platform-required or validation-only?


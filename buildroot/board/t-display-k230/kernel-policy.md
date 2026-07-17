# T-Display K230 Kernel Version Policy

This document defines the current kernel version policy.

Chinese version:

- [kernel-policy.zh-CN.md](kernel-policy.zh-CN.md)

## Policy

The first TDVP kernel baseline must come from the official K230 Linux SDK path:

```text
kendryte/k230_linux_sdk
```

The active kernel version is not chosen by version number. It is chosen by the
SDK profile that provides the most complete K230 big-core Linux boot and driver
baseline for adaptation.

## Required Pins

Before a kernel profile is accepted, these revisions must be recorded:

- `k230_linux_sdk` repository commit;
- external kernel repository URL;
- external kernel commit;
- kernel defconfig;
- DTS names or TDVP DTS path;
- OpenSBI version and source;
- U-Boot version, board name, and source.

Moving branches are not valid firmware inputs.

## Current Reference

The closest observed official profile is:

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

Observed kernel reference:

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
```

TDVP must adapt this baseline to T-Display K230 hardware facts.

## Header Policy

Kernel headers must follow the active kernel profile.

Do not keep headers from an older milestone after the kernel baseline changes.
Do not choose headers independently from the selected kernel without a written
toolchain reason.

## Upstream Policy

Upstream Linux is a comparison and forward-port target after the vendor Linux
baseline is understood. It is not the first implementation baseline by default.

Forward-port work must not change application APIs. Linux device nodes, media
graphs, ioctl values, DRM connector names, and vendor handles remain below
BSP/runtime.

## Acceptance Gates

A kernel candidate is accepted only when:

- exact source revisions are pinned;
- kernel and DTB build reproducibly through the TDVP Buildroot flow;
- U-Boot/OpenSBI can hand off to Linux;
- serial console reaches userspace;
- SD/MMC mounts the Buildroot rootfs;
- T-Display K230 board deltas are documented;
- application code does not depend on Linux internals.

## Prohibited Moves

- Do not select a kernel because it is newest.
- Do not use moving branches in platform builds.
- Do not copy vendor kernel or media handles into public APIs.
- Do not let a demo decide the kernel baseline.
- Do not modify official `buildroot/buildroot/` files for TDVP behavior.


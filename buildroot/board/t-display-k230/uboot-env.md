# T-Display K230 U-Boot Environment Policy

This document defines how TDVP handles U-Boot environment configuration.

Chinese version:

- [uboot-env.zh-CN.md](uboot-env.zh-CN.md)

## Policy

The first U-Boot environment baseline must be derived from the official K230
Linux SDK boot flow, then adapted for T-Display K230.

Primary reference:

```text
kendryte/k230_linux_sdk
buildroot-overlay/board/canaan/k230-soc/default.env
```

TDVP may carry a platform-owned environment source, but it must represent the
selected Linux SDK path and documented T-Display delta.

## Current Reference Shape

The official Linux SDK environment uses a Linux boot path that loads OpenSBI,
kernel, and DTB from the boot partition before handing off to Linux.

TDVP must extract and pin:

- boot device;
- boot partition;
- kernel filename;
- DTB filename;
- OpenSBI payload filename or build mode;
- load addresses;
- console arguments;
- rootfs arguments.

Current TDVP environment status:

```text
bootcmd=run blinux;
mmc_boot_dev_num=1
blinux=ext4load mmc ${mmc_boot_dev_num}:1 0x3000000 /fw_jump_add_uboot_head.bin && ext4load mmc ${mmc_boot_dev_num}:1 0x200000 /${k} && ext4load mmc ${mmc_boot_dev_num}:1 0x2200000 /k.dtb && bootm 0x3000000 - 0x2200000;
```

This follows the pinned SDK baseline. Console mapping remains a T-Display
hardware validation item.

## Source File

If TDVP owns an environment source, it lives at:

```text
buildroot/board/t-display-k230/uboot-linux.env
```

Do not edit generated files under `output/` to change boot behavior.

## Source Encoding and Line Endings

`uboot-linux.env` is a machine-readable input to `mkenvimage`, not a shell
script. It **must use LF line endings**. `mkenvimage` replaces each LF with a
NUL separator but retains a preceding CR byte from Windows CRLF input. That
turns an entry such as `mmc_boot_dev_num=1` into `mmc_boot_dev_num=1\\r`, which
can invalidate U-Boot environment type checks and make a boot command expand
to an invalid MMC device.

TDVP protects this contract in two places:

- `.gitattributes` checks out this source with LF line endings;
- `post-image.sh` rejects a source that still contains a CR byte.

The U-Boot environment used by the selected K230 SDK layout occupies the raw
SD-card range beginning at `0x1e0000` and has size `0x10000`. A flashing or
repair procedure must write the generated `uboot-env.bin` to that exact range;
the boot and rootfs GPT partitions do not replace it.

## Rules

- Do not expose U-Boot commands to applications.
- Do not rely on interactive boot commands for normal startup.
- Do not copy a vendor environment without understanding the rootfs, DTB, and
  board differences.
- Do not keep stale boot commands after the SDK baseline changes.

## Acceptance

The U-Boot environment is accepted when:

- it matches the pinned SDK boot model or a documented TDVP delta;
- U-Boot loads the selected OpenSBI/kernel/DTB inputs;
- Linux receives the intended command line;
- Linux mounts the Buildroot rootfs;
- the environment source is tracked or its binary provenance is pinned.
- the generated binary contains NUL-separated entries with no `CR` byte before
  an entry terminator; and
- a cold boot reaches the Linux login prompt without interactive U-Boot input.

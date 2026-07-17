# T-Display K230 Image Layout

This document defines the current SD-card image layout policy.

Chinese version:

- [image.zh-CN.md](image.zh-CN.md)

## Policy

The TDVP image layout must be derived from the official K230 Linux SDK path and
adapted to T-Display K230.

The output image remains:

```text
sysimage-sdcard.img
```

The image must be reproducible from pinned inputs:

- Buildroot release;
- `k230_linux_sdk` revision;
- kernel revision;
- OpenSBI/U-Boot source or binary provenance;
- TDVP board files;
- rootfs package set.

## Primary Reference

Use the official Linux SDK image layout as the primary reference:

```text
kendryte/k230_linux_sdk
buildroot-overlay/board/canaan/k230-soc/genimage.cfg
```

The observed Linux SDK layout uses fixed bootloader regions plus Linux boot and
rootfs partitions:

```text
SPL / U-Boot env / U-Boot fixed regions
boot partition
rootfs partition
```

TDVP must adapt that layout to T-Display K230 and avoid importing reference
application partition semantics.

Current TDVP image assembly follows this SDK shape:

```text
GPT
  1M       SPL copy 1
  0x180000 SPL copy 2
  0x1e0000 U-Boot environment
  2M       U-Boot
  0x380000 environment copy
  30M      boot.ext4
  128M     rootfs
```

The boot partition is ext4 because the selected SDK U-Boot environment uses
`ext4load` in the `blinux` command.

## Image Contents

The accepted image must contain:

- SPL;
- U-Boot environment;
- U-Boot;
- OpenSBI or Linux payload path required by the selected SDK baseline;
- Linux kernel image;
- selected SDK DTB, later replaced by the T-Display K230 DTS delta;
- Buildroot rootfs;
- platform init and configuration;
- BSP/runtime/SDK packages when implemented.

## Rejected Defaults

Do not use these as TDVP platform semantics:

- vendor demo application partitions;
- large writable app partitions as a platform requirement;
- target-side package installation;
- app-specific image variants;
- application-visible bootloader details.

## Buildroot Assembly

Board image assembly belongs to:

```text
buildroot/board/t-display-k230/genimage.cfg
buildroot/board/t-display-k230/post-image.sh
```

Those files currently follow the pinned `k230_linux_sdk` baseline. They must be
updated again when the T-Display DTS delta or bootloader packaging policy
changes.

The post-image flow must fail clearly or skip final image generation when
required boot inputs are missing. It must not leave stale images that look
hardware-ready.

## Acceptance Criteria

An image layout is accepted when:

1. all non-generated inputs are pinned or Git-tracked;
2. the board boots from the generated SD card;
3. Linux reaches BusyBox userspace;
4. rootfs mounts without manual intervention;
5. platform init starts;
6. applications do not depend on image layout details.

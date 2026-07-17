# Branching Strategy

This repository intentionally keeps the minimal platform baseline and the
Debian desktop work on separate branches.

## `main`

`main` is the stable embedded platform baseline.

It owns:

- the pinned Buildroot submodule reference;
- the TDVP `BR2_EXTERNAL` tree;
- the K230 6.6.36 kernel/DTB/boot integration;
- board hardware facts, validation records, and bring-up documentation;
- BSP, runtime, SDK, and minimal example skeletons;
- minimal BusyBox rootfs configuration used to validate hardware.

Rules:

- Keep `main` bootable as a minimal system.
- Do not add Debian, Ubuntu, X11, Wayland, desktop packages, or large
  distribution rootfs artifacts to `main`.
- Keep generated images, Buildroot output, downloaded package caches, and local
  boot binaries out of git.
- Only merge hardware fixes from desktop work back into `main` when they are
  useful for the minimal platform as well.

## `debian`

`debian` is the desktop userspace branch.

It may own:

- Debian riscv64 rootfs build scripts;
- package manifests for lightweight desktop profiles;
- X11/Wayland/session configuration;
- desktop image assembly scripts that reuse the TDVP kernel, DTB, U-Boot
  environment, and bootloader artifacts from `main`;
- desktop validation notes.

Rules:

- Reuse the K230 kernel, DTB, display fixes, and boot chain from `main`.
- Treat Debian as a userspace/rootfs layer, not as a replacement for the board
  support baseline.
- Keep downloaded `.deb` caches, expanded rootfs trees, and generated SD-card
  images out of git.
- If a change fixes the board, kernel, DTB, boot, or hardware abstraction layer,
  make or backport that change to `main` first, then rebase/merge `debian`.

## Merge Direction

Normal flow:

```text
main  -> debian
```

Backflow is allowed only for board-level fixes:

```text
debian hardware discovery -> main board fix -> debian update
```

This keeps `main` small, reproducible, and suitable for SDK/runtime work while
allowing `debian` to grow into a usable desktop image.

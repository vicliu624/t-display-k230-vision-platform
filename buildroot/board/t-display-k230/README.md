# T-Display K230 Board Documentation

This directory records board-specific facts for the T-Display K230 Vision
Platform Buildroot port.

The purpose of this directory is not to document the upstream CanMV/RT-Smart
application stack. It is the adaptation layer used by our minimal Linux system,
BSP, and vision runtime.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

## Source of Truth

Primary Linux adaptation reference:

- Repository: https://github.com/kendryte/k230_linux_sdk
- Reference branch pinned: `dev`
- Reference commit pinned: `5e1f7cfc794e111a447e4db57815f2cc9dc8c0c7`
- Snapshot date verified: 2026-07-16

Important reference paths in that repository:

- `README.md`
- `buildroot-overlay/configs/k230_canmv_v3_defconfig`
- `buildroot-overlay/board/canaan/k230-soc/default.env`
- `buildroot-overlay/board/canaan/k230-soc/genimage.cfg`
- `buildroot-overlay/board/canaan/k230-soc/post-image.sh`

Primary T-Display hardware reference:

- Repository: https://github.com/Xinyuan-LilyGO/T-Display-K230_canmv_rt
- Reference commit inspected: `111b67743f0c238b717ae502a4b4a9638c45f691`
- Hardware reference date inspected: 2026-06-14

Important reference paths in that repository:

- `README.md`
- `README_CN.md`
- `schematic/T-Display K230 V1.0.pdf`
- `canmv_k230/configs/k230_canmv_v3p0_defconfig`
- `canmv_k230/boards/k230_canmv_v3p0/default.env`
- `canmv_k230/boards/k230_canmv_v3p0/genimage-sdcard.cfg`
- `canmv_k230/src/rtsmart/mpp/kernel/connector/src/rm69a10.c`
- `canmv_k230/src/rtsmart/mpp/kernel/connector/src/lt9611.c`
- `canmv_k230/src/rtsmart/mpp/kernel/sensor/src/sensor_dev.c`
- `canmv_k230/src/rtsmart/rtsmart/kernel/bsp/maix3/drivers/Kconfig`

## Documents

- [adaptation-baseline.md](adaptation-baseline.md): current project goal,
  reference-source roles, and the `k230_linux_sdk` based adaptation strategy.
- [k230-linux-sdk-baseline.md](k230-linux-sdk-baseline.md): extracted facts
  from the pinned K230 Linux SDK profile.
- [sources.lock](sources.lock): machine-readable external source and artifact
  baseline lock.
- [hardware.md](hardware.md): board hardware inventory and pin facts.
- [boot.md](boot.md): boot facts, console facts, and Linux boot policy.
- [boot-artifact-decision.md](boot-artifact-decision.md): accepted, rejected,
  and deferred boot artifacts.
- [bootloader.md](bootloader.md): bootloader artifact acceptance,
  provenance, and integration rules.
- [image.md](image.md): SD image layout references and our Buildroot image policy.
- [uboot-env.md](uboot-env.md): platform-owned Linux U-Boot environment source,
  generation, and validation rules.
- [linux.md](linux.md): Linux kernel, device tree, driver, and BSP implications.
- [kernel-policy.md](kernel-policy.md): kernel headers and target kernel
  version policy.
- [kernel-selection.md](kernel-selection.md): current kernel baseline based on
  the official K230 Linux SDK path.
- [bringup-plan.md](bringup-plan.md): first Linux bring-up strategy and next
  implementation steps.
- [artifacts.md](artifacts.md): official firmware artifact inventory and image
  layout inspection.
- [rootfs.md](rootfs.md): first Buildroot-owned root filesystem build and
  validation record.
- [validation.md](validation.md): bring-up and acceptance checklist.

Chinese translations:

- [k230-linux-sdk-baseline.zh-CN.md](k230-linux-sdk-baseline.zh-CN.md)
- [hardware.zh-CN.md](hardware.zh-CN.md)
- [boot.zh-CN.md](boot.zh-CN.md)
- [boot-artifact-decision.zh-CN.md](boot-artifact-decision.zh-CN.md)
- [bootloader.zh-CN.md](bootloader.zh-CN.md)
- [image.zh-CN.md](image.zh-CN.md)
- [uboot-env.zh-CN.md](uboot-env.zh-CN.md)
- [linux.zh-CN.md](linux.zh-CN.md)
- [kernel-policy.zh-CN.md](kernel-policy.zh-CN.md)
- [kernel-selection.zh-CN.md](kernel-selection.zh-CN.md)
- [bringup-plan.zh-CN.md](bringup-plan.zh-CN.md)
- [artifacts.zh-CN.md](artifacts.zh-CN.md)
- [rootfs.zh-CN.md](rootfs.zh-CN.md)
- [validation.zh-CN.md](validation.zh-CN.md)

## Boundary Rule

`kendryte/k230_linux_sdk` is the primary official Linux adaptation reference.
It may define boot-flow, Buildroot-overlay, kernel, DTS, OpenSBI, U-Boot, and
image-layout facts for the first Linux baseline.

The LilyGO repository is used as a T-Display board fact source only.

We may reuse facts such as display controller, touch controller, GPIO numbers,
CSI routing, image offsets, and boot environment values. We do not inherit its
application model, CanMV UI model, MicroPython model, or RT-Smart split as our
platform architecture.

Upstream Linux is a later forward-port and comparison target. It is not the
first bring-up baseline unless a later decision explicitly changes this policy.

For this project:

- Buildroot owns deterministic system image generation.
- Linux owns kernel, drivers, device tree, and minimal userspace.
- BSP owns stable hardware APIs.
- Vision runtime owns camera, AI, overlay, display, buffers, and event loop.
- Applications must not depend on `/dev/video*`, DRM nodes, `ioctl`, or vendor
  device paths directly.

## Fact Quality Labels

Board facts in this directory use these labels:

- `confirmed`: found in schematic, config, board README, or driver source.
- `reference-only`: found in the RT-Smart/CanMV image flow, useful but not yet
  a Linux contract.
- `needs-validation`: plausible from sources, but must be proven on hardware.
- `linux-decision`: a policy chosen by this platform, not inherited from the
  reference repository.

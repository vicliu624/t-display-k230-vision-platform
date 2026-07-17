# Buildroot Customization Tutorial

This document explains where TDVP Linux customization happens.

Chinese version:

- [CUSTOMIZATION.zh-CN.md](CUSTOMIZATION.zh-CN.md)

## Mental Model

The official Buildroot tree is only the build engine:

```text
buildroot/buildroot/
```

TDVP policy lives in the outer Buildroot external tree:

```text
buildroot/
```

The current platform customization goal is:

```text
adapt T-Display K230 from kendryte/k230_linux_sdk
```

## Build vs Customization

The build command runs Buildroot:

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

Customization changes the tracked inputs consumed by those commands:

| Need | Edit here |
| --- | --- |
| System profile | `buildroot/configs/t_display_k230_vision_defconfig` |
| Board adaptation | `buildroot/board/t-display-k230/` |
| Kernel config fragments | `buildroot/kernel_config/` |
| Platform packages | `buildroot/package/` |
| Small rootfs additions | `buildroot/rootfs_overlay/` |
| Buildroot upstream pin | `buildroot/UPSTREAM.md` and the submodule gitlink |

Do not customize inside `buildroot/buildroot/`.

## Current Customization Order

For the current baseline, customize in this order:

1. Pin a `kendryte/k230_linux_sdk` revision.
2. Select the closest official SDK profile.
3. Extract kernel, DTS, OpenSBI, U-Boot, image layout, and rootfs facts.
4. Compare them with T-Display K230 hardware facts.
5. Create the TDVP board delta.
6. Update the TDVP Buildroot defconfig.
7. Keep the rootfs minimal and remove full vendor demo assumptions.

The closest observed SDK profile is:

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

## Defconfig Customization

Use `menuconfig` only to produce a tracked defconfig. The generated `.config`
under `output/` is not the source of truth.

```sh
cd buildroot/buildroot
make BR2_EXTERNAL=.. O=../../output/t_display_k230_vision menuconfig
```

Save the canonical profile:

```sh
make O=../../output/t_display_k230_vision savedefconfig \
  BR2_DEFCONFIG=../configs/t_display_k230_vision_defconfig
```

Then rebuild from a clean tracked input:

```sh
cd ../..
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

## Board Customization

Board-specific adaptation belongs under:

```text
buildroot/board/t-display-k230/
```

This directory owns:

- adaptation baseline;
- hardware facts;
- kernel selection;
- Linux design;
- bring-up plan;
- image layout;
- U-Boot environment;
- validation rules.

Use `kendryte/k230_linux_sdk` for Linux boot/kernel/image facts. Use the LilyGO
T-Display reference for board wiring facts.

## Rootfs Customization

Prefer Buildroot packages for real software:

```text
buildroot/package/
```

Use rootfs overlay only for small platform-owned files:

```text
buildroot/rootfs_overlay/
```

Allowed overlay examples:

- init scripts;
- platform configuration;
- small validation files.

Forbidden overlay use:

- large binaries;
- generated build outputs;
- app-specific state;
- vendor demo dumps.

## Package Selection Rules

Every package must be classified:

| Class | Meaning |
| --- | --- |
| platform-required | needed by boot, BSP, runtime, SDK, or hardware validation |
| validation-only | useful for bring-up, removable from production profile |
| demo-only | not part of base image |
| rejected | conflicts with minimal deterministic platform |

Do not copy the full `k230_linux_sdk` package set into TDVP by default.

## Clean Build Rules

- Keep the repository and Buildroot output under a Linux filesystem when using
  WSL.
- Treat `output/` as disposable generated state.
- Rebuild after changing toolchain, kernel, or SDK baseline.
- Do not repair builds by editing generated files under `output/`.

## Acceptance Checklist

Customization is correctly captured when:

- a clean checkout can load `t_display_k230_vision_defconfig`;
- all external SDK/kernel/boot inputs are pinned;
- the image builds without manual edits under `output/`;
- the rootfs stays minimal;
- applications still see only TDVP BSP/runtime/SDK APIs.


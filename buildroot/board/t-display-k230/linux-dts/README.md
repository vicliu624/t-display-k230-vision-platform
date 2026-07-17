# T-Display K230 Linux Device Tree

This directory is reserved for TDVP-owned Linux DTS files or DTS deltas.

Current status: the active Buildroot defconfig uses the SDK in-tree DTS names
`canaan/k230-canmv-v3-lcd` and `canaan/k230-canmv-v3`. The files in this
directory are not active build inputs until the T-Display K230 DTS delta is
defined and the defconfig is changed back to `BR2_LINUX_KERNEL_CUSTOM_DTS_DIR`.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

## Policy

The first DTS baseline must be derived from the selected `k230_linux_sdk`
profile and adapted to T-Display K230 hardware facts.

Possible starting points:

```text
canaan/k230-canmv-v3-lcd
canaan/k230-canmv-v3
TDVP-owned tdisplay-k230.dts
```

The choice must be recorded in the kernel-selection and Linux design documents.

## Source Boundaries

Use `kendryte/k230_linux_sdk` for Linux DTS structure, bindings, and driver
expectations.

Use the LilyGO T-Display K230 reference for board hardware facts:

- display panel;
- touch controller;
- camera routing;
- GPIO reset/power/interrupt lines;
- schematic-level wiring.

Do not copy a U-Boot or RTOS device tree as the Linux board contract.

## Node Admission Rule

A node enters the TDVP DTS only after these are known:

- compatible binding;
- register range;
- clocks and resets;
- interrupts;
- pinctrl state;
- power/reset sequencing;
- Linux driver path;
- T-Display K230 board fact provenance.

Deferred does not mean out of scope. It means the node is not yet part of the
Linux contract.

## Buildroot Integration

If TDVP owns custom DTS files, Buildroot uses:

```text
BR2_LINUX_KERNEL_DTS_SUPPORT=y
BR2_LINUX_KERNEL_CUSTOM_DTS_DIR="$(BR2_EXTERNAL_TDVP_PATH)/board/t-display-k230/linux-dts"
```

The current defconfig uses SDK in-tree DTS files directly:

```text
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
```

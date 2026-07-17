# Board Layer

This directory contains board-specific integration files for the T-Display K230
Vision Platform.

The board layer adapts Buildroot and Linux to the physical board. It does not
define application behavior.

## Board Targets

- [t-display-k230](t-display-k230/README.md): LilyGO T-Display K230 board
  facts, boot notes, image layout notes, Linux porting notes, and validation
  checklist.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

## Purpose

The board layer bridges:

- Buildroot image generation.
- K230 boot chain.
- Linux kernel and device tree.
- Board peripherals.
- Platform bring-up requirements.

## Responsibilities

### Boot Configuration

Allowed:

- U-Boot environment defaults.
- Boot scripts.
- Partition/image layout hooks.
- Kernel command line defaults.
- Early boot files required by the target board.

### Device Tree Management

Allowed:

- LCD panel configuration.
- Camera sensor configuration.
- MIPI/CSI/DSI wiring.
- GPIO mapping.
- I2C/SPI bus mapping.
- Button or touch input mapping.
- Backlight and power sequencing declarations.
- Memory reservations required by display, camera, DMA, or AI.

### Board Bring-Up Validation

The board layer should make it possible to validate:

- Kernel boots on the target board.
- Display can be initialized by the BSP.
- Camera is visible to the BSP.
- Input device is visible to the BSP.
- Required buses are enabled.
- Runtime can start after init.

## Forbidden

Do not put these in the board layer:

- Application logic.
- Vision pipeline logic.
- AI model logic.
- SDK example behavior.
- User-space policy that belongs in runtime config.
- Workarounds that expose Linux device names to applications.
- Demo-specific boot behavior.

## Ownership Rule

Board files may describe hardware facts. They must not define product behavior.

If a change answers "what hardware exists and how Linux boots it", it may
belong here.

If a change answers "what should the vision app do", it does not belong here.

## Review Checklist

Before accepting a board-layer change, answer yes to all items:

- Is this required for the board to boot or expose hardware correctly?
- Is the behavior stable across all applications?
- Does the BSP still hide the hardware detail from runtime and apps?
- Is the change independent of a single demo?
- Is the device tree still a hardware description, not an app policy file?

# Kernel Configuration

This directory contains kernel configuration rules and fragments for the
T-Display K230 Vision Platform.

Kernel configuration is part of the platform foundation. It must be minimal,
deterministic, and driven by BSP/runtime requirements.

Chinese version:

- [README.zh-CN.md](README.zh-CN.md)

Current board fragment:

- [t_display_k230_vision.fragment](t_display_k230_vision.fragment)

## Purpose

Kernel configuration enables only the Linux capabilities required by the
platform:

- Booting the K230 target.
- Camera capture for the BSP.
- Display output for the BSP.
- Input events for the BSP.
- Required board buses.
- DMA-friendly frame movement.
- Additional populated board capabilities.

## Required Subsystems

The canonical platform kernel config must include:

- V4L2 media stack for camera.
- DRM/KMS for display.
- Input subsystem for buttons, touch, or controls.
- SPI and I2C for peripherals.
- GPIO and pinctrl support required by the board.
- DMA/CMA support for camera, display, and AI paths.
- Filesystems required by the boot image.

The first board fragment currently fixes only the Linux controller drivers that
match the tracked DTS skeleton:

- `CONFIG_COMMON_CLK_K230`
- `CONFIG_RESET_K230`
- `CONFIG_PINCTRL_K230`

Do not grow this fragment into a full feature profile before the corresponding
Linux DTS node and BSP/runtime boundary are accepted.

Additional board capability subsystems:

- ALSA when audio is part of the platform profile.
- USB when required for debug, storage, or network workflows.
- Networking only when required by a supported platform workflow.

## Selection Rules

Use the smallest driver set that supports the target hardware.

Rules:

- Prefer built-in drivers for boot-critical hardware.
- Use modules only when load order is deterministic.
- Do not expose module loading as an application responsibility.
- Do not enable broad subsystems without a platform use case.
- Do not add kernel features for one demo without platform review.
- Keep kernel options aligned with BSP and runtime contracts.

## Forbidden

Forbidden kernel config changes:

- Desktop-oriented subsystems without platform need.
- Debug features in production profiles unless explicitly justified.
- Filesystems unrelated to supported image or storage workflows.
- Drivers for unsupported board variants in the canonical profile.
- Options that force applications to know Linux device internals.

## Review Checklist

Before accepting a kernel config change, answer yes to all items:

- Is this required for board boot, BSP hardware access, or runtime execution?
- Is the option minimal for the supported board?
- Is the behavior deterministic across boots?
- Does the BSP still hide Linux details from apps?
- Is the option documented in the platform architecture or API contract?
- Can CI reproduce the same kernel configuration?

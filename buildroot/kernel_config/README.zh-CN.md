# Kernel Configuration

本目录包含 T-Display K230 Vision Platform 的 kernel configuration rules 和 fragments。

Kernel configuration 是 platform foundation 的一部分。它必须最小、确定，并由 BSP/runtime
requirements 驱动。

英文版本：

- [README.md](README.md)

当前 board fragment：

- [t_display_k230_vision.fragment](t_display_k230_vision.fragment)

## 目的

Kernel configuration 只启用平台需要的 Linux capabilities：

- 启动 K230 target。
- BSP 所需 camera capture。
- BSP 所需 display output。
- BSP 所需 input events。
- 必需 board buses。
- DMA-friendly frame movement。
- 额外且实际存在的板载能力。

## 必需 Subsystems

Canonical platform kernel config 必须包含：

- Camera 所需的 V4L2 media stack。
- Display 所需的 DRM/KMS。
- Buttons、touch 或 controls 所需的 input subsystem。
- Peripherals 所需的 SPI 和 I2C。
- Board 所需的 GPIO 和 pinctrl support。
- Camera、display 和 AI paths 所需的 DMA/CMA support。
- Boot image 所需 filesystems。

第一版 board fragment 目前只固定与已跟踪 DTS skeleton 对齐的 Linux controller drivers：

- `CONFIG_COMMON_CLK_K230`
- `CONFIG_RESET_K230`
- `CONFIG_PINCTRL_K230`

在对应 Linux DTS node 和 BSP/runtime boundary 被接受前，不要把这个 fragment 扩展成完整 feature
profile。

额外板载能力 subsystems：

- 当 audio 是 platform profile 的一部分时启用 ALSA。
- 当 debug、storage 或 network workflows 需要时启用 USB。
- 只有 supported platform workflow 需要时才启用 networking。

## Selection Rules

使用支持目标硬件的最小 driver set。

规则：

- Boot-critical hardware 优先使用 built-in drivers。
- 只有 load order 确定时才使用 modules。
- 不得把 module loading 暴露成 application responsibility。
- 没有 platform use case 时，不启用宽泛 subsystems。
- 不得为了一个 demo 加 kernel feature，除非经过 platform review。
- Kernel options 必须与 BSP 和 runtime contracts 对齐。

## 禁止事项

禁止的 kernel config changes：

- 没有 platform need 的 desktop-oriented subsystems。
- Production profiles 中未经明确证明的 debug features。
- 与受支持 image 或 storage workflows 无关的 filesystems。
- Canonical profile 中面向 unsupported board variants 的 drivers。
- 迫使 applications 理解 Linux device internals 的 options。

## Review Checklist

接受 kernel config change 之前，必须能对以下问题全部回答“是”：

- 这是 board boot、BSP hardware access 或 runtime execution 所必需的吗？
- 这个 option 对受支持 board 来说是否最小？
- 行为是否跨启动确定？
- BSP 是否仍然对 apps 隐藏 Linux details？
- 这个 option 是否记录在 platform architecture 或 API contract 中？
- CI 是否能复现同一个 kernel configuration？

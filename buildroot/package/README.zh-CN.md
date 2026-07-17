# Buildroot Packages

本目录包含 T-Display K230 Vision Platform 的 platform-specific Buildroot packages。

它是 Linux system image 和 platform SDK layers 之间的集成点。

英文版本：

- [README.md](README.md)

## 目的

本目录中的 packages 以确定方式把 platform components 安装进 target root filesystem。

允许的 package categories：

- BSP package。
- Vision runtime package。
- SDK examples package。
- 当 target-side tools 必要时的 SDK tools package。
- Bring-up validation-only package。
- 目标 profile 上实际存在时的 board capability packages，例如 LoRa、audio、power、radio 或
  sensor support。

## Package Ownership

### BSP Package

负责安装：

- Camera abstraction library。
- Display abstraction library。
- Input abstraction library。
- 目标 profile 上实际存在时的 LoRa/audio/storage/power/radio abstractions。
- 如果支持 target-side development，则安装 public BSP headers。

BSP package 可以依赖 Linux drivers 已存在，但它的 public API 不得暴露 Linux device details。

### Vision Runtime Package

负责安装：

- Pipeline engine。
- Buffer manager。
- AI inference wrapper。
- Event loop。
- 如果支持 target-side development，则安装 public runtime headers。

Runtime package 依赖 BSP APIs，而不是 application code。

### SDK Examples Package

负责安装：

- `demo_camera`。
- `demo_ai`。
- Minimal validation examples。
- 这些 examples 所需的 example assets。

Examples 必须证明 platform API 可用。它们不得定义 platform behavior。

### Bring-Up Validation Package

负责安装：

- BSP API 存在前使用的 board smoke-test tools。
- 用来验证 Linux/device-tree/driver readiness 的 hardware diagnostics。

Validation-only packages 可以直接使用 Linux internals，但只能用于 bring-up 阶段证明板级
readiness。它们不得变成 application examples 或 public SDK APIs。

## 禁止的 Packages

不要添加以下 packages：

- Desktop apps。
- GUI shells。
- 与 platform operation 无关的 generic Linux tools。
- Per-demo system dependencies。
- Application-specific services。
- Runtime package managers。
- 烧录后需要手工 target setup 的 packages。

## Package Design Rules

每个 package 必须：

- 从 versioned source inputs 构建。
- 通过 Buildroot package rules 安装文件。
- 避免依赖 host machine state。
- 避免 normal use 期间 target-side downloads。
- 只暴露 stable platform APIs。
- 把 Linux internals 保持在 BSP/runtime boundary 之下。
- 在 CI 中可复现。

## 期望 Package Names

推荐 package names：

```text
tdvp-bsp
tdvp-runtime
tdvp-sdk-examples
tdvp-sdk-tools
```

Board capability packages 应使用相同 prefix：

```text
tdvp-lora
tdvp-audio
```

## Review Checklist

添加或修改 package 之前，必须能对以下问题全部回答“是”：

- 这是 platform component，而不是 app workaround 吗？
- 它是否通过 Buildroot rules 安装，而不是通过 overlay copy？
- Package 是否 deterministic？
- 烧录后 image 是否能无需手工 setup 启动？
- Package 是否保持 API boundaries 完整？
- Dependency 是否由 `docs/architecture.md` 或 `docs/api.md` 证明？

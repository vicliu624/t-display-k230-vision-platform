# 历史启动链调试归档

本文是项目基线回归到 `kendryte/k230_linux_sdk` 之前，早期启动实验留下的历史归档。

它不是当前平台规格的一部分。

## 当前决策

当前 Linux bring-up 路径是：

```text
kendryte/k230_linux_sdk
  -> 最接近的 K230/CanMV Linux profile
  -> T-Display K230 硬件差异
  -> TDVP Buildroot board profile
```

本归档不得用于决定：

- target kernel version，
- OpenSBI baseline，
- U-Boot policy，
- hidden image slot，
- SD-card image layout，
- Linux payload format，
- application/runtime architecture。

## 历史探针证明了什么

早期 probes 仍然是有价值的证据。它们证明过：

- 板子可以进入 U-Boot；
- U-Boot 可以读取可见 FAT boot 分区；
- U-Boot 可以读取 ext4 rootfs 分区；
- UART0 在 K230 启动固件路径下可以输出；
- 一个最小 S-mode payload 可以通过当时测试的 handoff 形态被执行。

这些事实在调试板级启动时仍有参考价值。

## 它们没有证明什么

这些 probes 没有建立干净的 Linux 平台基线。

它们没有证明本地拼装的 OpenSBI/Linux payload chain 应该成为项目架构，
也没有证明非 Linux reference payload 应该被接受为平台组件。

## 未来使用规则

只把本文作为调试证据归档使用。如果未来还需要本文中的某个事实，必须基于当前
`k230_linux_sdk` baseline 重新推导，并把结果记录到活跃 board documents：

- `adaptation-baseline.zh-CN.md`
- `boot.zh-CN.md`
- `image.zh-CN.md`
- `uboot-env.zh-CN.md`
- `kernel-selection.zh-CN.md`
- `bringup-plan.zh-CN.md`
- `validation.zh-CN.md`

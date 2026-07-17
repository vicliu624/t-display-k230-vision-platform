# T-Display K230 Linux Device Tree

本目录预留给 TDVP-owned Linux DTS files 或 DTS deltas。

当前状态：active Buildroot defconfig 使用 SDK in-tree DTS names：
`canaan/k230-canmv-v3-lcd` 和 `canaan/k230-canmv-v3`。本目录中的文件在
T-Display K230 DTS delta 定义完成，并且 defconfig 改回
`BR2_LINUX_KERNEL_CUSTOM_DTS_DIR` 前，不是 active build inputs。

英文版本：

- [README.md](README.md)

## 策略

第一版 DTS baseline 必须来自所选 `k230_linux_sdk` profile，并适配到 T-Display K230
硬件事实。

可能起点：

```text
canaan/k230-canmv-v3-lcd
canaan/k230-canmv-v3
TDVP-owned tdisplay-k230.dts
```

具体选择必须记录在 kernel-selection 和 Linux design 文档中。

## 来源边界

使用 `kendryte/k230_linux_sdk` 获取 Linux DTS structure、bindings 和 driver expectations。

使用 LilyGO T-Display K230 reference 获取板级硬件事实：

- display panel；
- touch controller；
- camera routing；
- GPIO reset/power/interrupt lines；
- schematic-level wiring。

不要把 U-Boot 或 RTOS device tree 复制成 Linux board contract。

## Node Admission Rule

一个 node 只有在以下内容明确后，才能进入 TDVP DTS：

- compatible binding；
- register range；
- clocks and resets；
- interrupts；
- pinctrl state；
- power/reset sequencing；
- Linux driver path；
- T-Display K230 board fact provenance。

Deferred 不等于 out of scope。它只表示该 node 还不是 Linux contract 的一部分。

## Buildroot 集成

如果 TDVP 拥有 custom DTS files，Buildroot 使用：

```text
BR2_LINUX_KERNEL_DTS_SUPPORT=y
BR2_LINUX_KERNEL_CUSTOM_DTS_DIR="$(BR2_EXTERNAL_TDVP_PATH)/board/t-display-k230/linux-dts"
```

当前 defconfig 直接使用 SDK in-tree DTS files：

```text
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
```

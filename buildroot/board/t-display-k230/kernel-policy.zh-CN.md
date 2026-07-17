# T-Display K230 Kernel 版本策略

本文定义当前 kernel 版本策略。

英文版本：

- [kernel-policy.md](kernel-policy.md)

## 策略

第一版 TDVP kernel baseline 必须来自官方 K230 Linux SDK 路径：

```text
kendryte/k230_linux_sdk
```

Active kernel version 不由版本号决定，而由能提供最完整 K230 大核 Linux boot 和 driver
baseline 的 SDK profile 决定。

## 必须固定的输入

Kernel profile 被接受前，必须记录这些 revision：

- `k230_linux_sdk` repository commit；
- external kernel repository URL；
- external kernel commit；
- kernel defconfig；
- DTS names 或 TDVP DTS path；
- OpenSBI version and source；
- U-Boot version、board name 和 source。

移动 branch 不是合法 firmware input。

## 当前参考

目前观察到最接近的官方 profile 是：

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

观察到的 kernel reference：

```text
BR2_LINUX_KERNEL_CUSTOM_REPO_URL="https://github.com/ruyisdk/linux-xuantie-kernel.git"
BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="7d4e1f444f461dbe3833bd99a4640e7b6c2cd529"
BR2_LINUX_KERNEL_DEFCONFIG="k230"
BR2_LINUX_KERNEL_INTREE_DTS_NAME="canaan/k230-canmv-v3-lcd canaan/k230-canmv-v3"
```

TDVP 必须把这个 baseline 适配到 T-Display K230 硬件事实。

## Headers 策略

Kernel headers 必须跟随 active kernel profile。

Kernel baseline 改变后，不保留旧 milestone 的 headers。没有书面 toolchain 理由时，
不要让 headers 独立于所选 kernel。

## Upstream 策略

Upstream Linux 是 vendor Linux baseline 明确后的 comparison 和 forward-port target。
默认不是第一轮实现 baseline。

Forward-port 工作不得改变 application APIs。Linux device nodes、media graphs、ioctl
values、DRM connector names 和 vendor handles 都必须停在 BSP/runtime 之下。

## 验收门槛

Kernel candidate 只有满足以下条件才被接受：

- exact source revisions 已固定；
- kernel 和 DTB 可以通过 TDVP Buildroot flow 可复现构建；
- U-Boot/OpenSBI 可以 hand off 到 Linux；
- serial console 进入 userspace；
- SD/MMC 挂载 Buildroot rootfs；
- T-Display K230 board deltas 已记录；
- application code 不依赖 Linux internals。

## 禁止动作

- 不因为某个 kernel 最新就选择它。
- 平台构建不使用移动 branch。
- 不把 vendor kernel 或 media handles 放进 public APIs。
- 不让 demo 决定 kernel baseline。
- 不为了 TDVP behavior 修改官方 `buildroot/buildroot/` 文件。


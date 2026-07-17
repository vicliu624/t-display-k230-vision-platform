# Buildroot 定制教程

本文说明 TDVP Linux 定制应该发生在哪里。

英文版本：

- [CUSTOMIZATION.md](CUSTOMIZATION.md)

## 心智模型

官方 Buildroot tree 只是构建引擎：

```text
buildroot/buildroot/
```

TDVP 策略位于外层 Buildroot external tree：

```text
buildroot/
```

当前平台定制目标是：

```text
adapt T-Display K230 from kendryte/k230_linux_sdk
```

## Build vs Customization

构建命令只是运行 Buildroot：

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

定制是修改这些命令消费的已跟踪输入：

| Need | Edit here |
| --- | --- |
| System profile | `buildroot/configs/t_display_k230_vision_defconfig` |
| Board adaptation | `buildroot/board/t-display-k230/` |
| Kernel config fragments | `buildroot/kernel_config/` |
| Platform packages | `buildroot/package/` |
| Small rootfs additions | `buildroot/rootfs_overlay/` |
| Buildroot upstream pin | `buildroot/UPSTREAM.md` 和 submodule gitlink |

不要在 `buildroot/buildroot/` 里定制。

## 当前定制顺序

当前 baseline 下，按这个顺序定制：

1. 固定一个 `kendryte/k230_linux_sdk` revision。
2. 选择最接近的官方 SDK profile。
3. 提取 kernel、DTS、OpenSBI、U-Boot、image layout 和 rootfs facts。
4. 与 T-Display K230 硬件事实对比。
5. 创建 TDVP board delta。
6. 更新 TDVP Buildroot defconfig。
7. 保持 rootfs 最小化，并移除完整 vendor demo assumptions。

目前观察到最接近的 SDK profile 是：

```text
buildroot-overlay/configs/k230_canmv_v3_defconfig
```

## Defconfig 定制

`menuconfig` 只用于生成已跟踪 defconfig。`output/` 下生成的 `.config` 不是 truth source。

```sh
cd buildroot/buildroot
make BR2_EXTERNAL=.. O=../../output/t_display_k230_vision menuconfig
```

保存 canonical profile：

```sh
make O=../../output/t_display_k230_vision savedefconfig \
  BR2_DEFCONFIG=../configs/t_display_k230_vision_defconfig
```

然后从干净的已跟踪输入重新构建：

```sh
cd ../..
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

## Board 定制

板级适配放在：

```text
buildroot/board/t-display-k230/
```

该目录负责：

- adaptation baseline；
- hardware facts；
- kernel selection；
- Linux design；
- bring-up plan；
- image layout；
- U-Boot environment；
- validation rules。

使用 `kendryte/k230_linux_sdk` 获取 Linux boot/kernel/image facts。使用 LilyGO
T-Display reference 获取板级 wiring facts。

## Rootfs 定制

真实软件优先做成 Buildroot packages：

```text
buildroot/package/
```

Rootfs overlay 只用于小型平台自有文件：

```text
buildroot/rootfs_overlay/
```

允许的 overlay 例子：

- init scripts；
- platform configuration；
- small validation files。

禁止的 overlay 用法：

- large binaries；
- generated build outputs；
- app-specific state；
- vendor demo dumps。

## Package 选择规则

每个 package 必须分类：

| Class | Meaning |
| --- | --- |
| platform-required | boot、BSP、runtime、SDK 或硬件验证需要 |
| validation-only | bring-up 有用，production profile 可移除 |
| demo-only | 不属于 base image |
| rejected | 与 minimal deterministic platform 冲突 |

不要默认照搬完整 `k230_linux_sdk` package set。

## Clean Build 规则

- 使用 WSL 时，把 repository 和 Buildroot output 放在 Linux filesystem。
- `output/` 是可丢弃的生成状态。
- 修改 toolchain、kernel 或 SDK baseline 后重新构建。
- 不通过编辑 `output/` 下的生成文件修 build。

## 验收清单

定制正确捕获的条件：

- clean checkout 可以加载 `t_display_k230_vision_defconfig`；
- 所有外部 SDK/kernel/boot inputs 都已固定；
- image 构建不需要手工编辑 `output/`；
- rootfs 保持最小；
- applications 仍然只看到 TDVP BSP/runtime/SDK APIs。


# Buildroot Configurations

本目录包含外层 `buildroot/` BR2_EXTERNAL tree 暴露的 TDVP Buildroot defconfigs。

英文版本：

- [README.md](README.md)

## 目的

一个 defconfig 定义一个可复现的平台系统 profile：

- target architecture 和 toolchain policy；
- Linux kernel source 和 kernel configuration；
- device tree selection；
- root filesystem composition；
- boot/image integration；
- image 中包含的平台 packages。

它不是 application config，也不是用户偏好文件。

## Canonical Profile

Canonical profile 是：

```text
t_display_k230_vision_defconfig
```

当前 baseline：

```text
adapt T-Display K230 from kendryte/k230_linux_sdk
```

在 exact SDK/kernel/OpenSBI/U-Boot revisions 固定后，该 profile 必须更新为匹配所选
官方 SDK baseline。

## Source Rules

- 平台 defconfigs 放在这里，不放到 `buildroot/buildroot/configs/`。
- 不为了 TDVP behavior 修改官方 Buildroot submodule。
- 不把移动 branch 当作 firmware input。
- 不添加 demo-specific profiles。
- 不把 application behavior 写进 system profile。

## Expected Profile Shape

Canonical profile 只应该选择平台级需求：

- RISC-V K230 target。
- 与所选 SDK baseline 兼容的 toolchain policy。
- K230 Linux kernel source 和 pinned revision。
- K230/T-Display device tree strategy。
- BusyBox userspace。
- deterministic rootfs image。
- deterministic SD-card image generation。
- image generation 所需 host tools。
- 存在后加入 TDVP BSP/runtime/SDK packages。

默认不应包含：

- desktop packages；
- target package managers；
- full vendor demo package set；
- app-specific dependencies；
- 作为 production dependencies 的大范围 debug tools。

## Interactive Configuration Flow

从官方 Buildroot source tree 运行 `menuconfig`，并让 `BR2_EXTERNAL` 指向外层平台 tree：

```sh
cd buildroot/buildroot
make BR2_EXTERNAL=.. O=../../output/t_display_k230_vision menuconfig
```

把 canonical defconfig 保存回本目录：

```sh
make O=../../output/t_display_k230_vision savedefconfig \
  BR2_DEFCONFIG=../configs/t_display_k230_vision_defconfig
```

从仓库根目录重新构建：

```sh
make -C buildroot/buildroot BR2_EXTERNAL=.. O=../../output/t_display_k230_vision t_display_k230_vision_defconfig
make -C buildroot/buildroot O=../../output/t_display_k230_vision
```

## Review Checklist

接受 defconfig change 前检查：

- SDK/kernel/boot input 是否已固定？
- change 是否服务 shared platform，而不是某个 app？
- 是否保持 minimal rootfs？
- 是否避免 desktop 和 distro assumptions？
- 是否保持 hardware access 位于 BSP/runtime APIs 之后？
- vendor package 是否被分类为 platform-required 或 validation-only？


# T-Display K230 Image Layout

本文定义当前 SD-card image layout 策略。

英文版本：

- [image.md](image.md)

## 策略

TDVP image layout 必须来自官方 K230 Linux SDK 路径，并适配到 T-Display K230。

输出镜像名保持：

```text
sysimage-sdcard.img
```

镜像必须可以从 pinned inputs 可复现生成：

- Buildroot release；
- `k230_linux_sdk` revision；
- kernel revision；
- OpenSBI/U-Boot source 或 binary provenance；
- TDVP board files；
- rootfs package set。

## 主要参考

使用官方 Linux SDK image layout 作为主要参考：

```text
kendryte/k230_linux_sdk
buildroot-overlay/board/canaan/k230-soc/genimage.cfg
```

观察到的 Linux SDK layout 使用固定 bootloader regions，加上 Linux boot 和 rootfs partitions：

```text
SPL / U-Boot env / U-Boot fixed regions
boot partition
rootfs partition
```

TDVP 必须把这个 layout 适配到 T-Display K230，并避免导入参考 application partition
semantics。

当前 TDVP image assembly 跟随这个 SDK 形态：

```text
GPT
  1M       SPL copy 1
  0x180000 SPL copy 2
  0x1e0000 U-Boot environment
  2M       U-Boot
  0x380000 environment copy
  30M      boot.ext4
  128M     rootfs
```

Boot partition 使用 ext4，因为所选 SDK U-Boot environment 的 `blinux` command 使用
`ext4load`。

## 镜像内容

被接受的 image 必须包含：

- SPL；
- U-Boot environment；
- U-Boot；
- 所选 SDK baseline 需要的 OpenSBI 或 Linux payload path；
- Linux kernel image；
- 所选 SDK DTB，后续由 T-Display K230 DTS delta 替换；
- Buildroot rootfs；
- platform init and configuration；
- 实现后的 BSP/runtime/SDK packages。

## 拒绝的默认值

不要把下面内容作为 TDVP platform semantics：

- vendor demo application partitions；
- 把大型 writable app partitions 作为平台要求；
- target-side package installation；
- app-specific image variants；
- application-visible bootloader details。

## Buildroot Assembly

板级 image assembly 属于：

```text
buildroot/board/t-display-k230/genimage.cfg
buildroot/board/t-display-k230/post-image.sh
```

这些文件当前已经跟随 pinned `k230_linux_sdk` baseline。后续当 T-Display DTS delta
或 bootloader packaging policy 变化时，必须再次更新。

Post-image flow 在缺少必需 boot inputs 时必须清楚失败或跳过 final image generation。
它不能留下看起来 hardware-ready 的 stale images。

## 验收标准

Image layout 满足以下条件才算接受：

1. 所有非生成输入都已 pinned 或 Git-tracked；
2. board 可以从 generated SD card 启动；
3. Linux 进入 BusyBox userspace；
4. rootfs 无需手工干预即可挂载；
5. platform init 启动；
6. applications 不依赖 image layout details。

# T-Display K230 RootFS 策略

本文定义当前 root filesystem 策略。

英文版本：

- [rootfs.md](rootfs.md)

## 范围

TDVP rootfs 是为 T-Display K230 平台生成的最小 Buildroot Linux userspace。

它必须支持：

- BusyBox init 和 shell utilities；
- serial bring-up；
- platform init scripts；
- BSP/runtime libraries and services；
- 平台明确选择的 validation tools。

它不得变成：

- desktop system；
- Debian/Ubuntu-like runtime；
- target-side package-managed environment；
- 完整 reference SDK demo rootfs 的 dump；
- application-specific filesystem。

## 当前 Baseline

Rootfs 必须随所选 `k230_linux_sdk` baseline 适配。

SDK 可以提供有用的 rootfs structure 和 package references，但 TDVP 必须分类每个导入的
package：

| Class | Rule |
| --- | --- |
| platform-required | 允许进入 base image |
| validation-only | bring-up 阶段允许，后续可移除 |
| demo-only | 不属于 base image |
| rejected | 不导入 |

当前 bring-up 阶段的 `validation-only` 集合包括：

- Dropbear：只作为 SSH/debug control plane，配合 `eth0` DHCP 使用；root 密码固定为
  `tdvp`；
- `ethtool`、`iperf3`、`tcpdump`、`strace`、`lsof`：网络和系统诊断；
- `v4l2-ctl`、`media-ctl`：camera/media graph 验证；
- `aplay/arecord`、`amixer`、`speaker-test`：audio/speaker 验证；
- `spi-tools`、`spidev_test`：SPI wiring 验证。

这些工具不改变平台 API 边界。Applications 仍不得直接依赖 SSH、V4L2、ALSA、SPI、
evdev、sysfs 或 ioctl。

## 必需内容

最小预期内容：

```text
/bin
/sbin
/lib
/usr/bin
/usr/lib
/etc/init.d
/etc/vision-platform
/opt/vision
/proc
/sys
/dev
/tmp
/var
```

必需行为：

- devtmpfs 提供 device nodes；
- init 进入 platform startup path；
- flash 后不要求 target 安装 packages；
- app code 不依赖手工复制文件到 rootfs。

## Overlay 规则

Rootfs overlay 只用于小型平台自有文件：

- init scripts；
- platform config；
- small validation assets。

不要把 rootfs overlay 用于：

- large binaries；
- generated files；
- vendor demo dumps；
- app-specific state；
- private developer convenience files。

## 验收

Rootfs 满足以下条件才算接受：

- 由 Buildroot 从 tracked inputs 生成；
- 通过所选 K230 Linux path 启动到 BusyBox userspace；
- platform init 运行；
- base image 只包含已分类 packages；
- bring-up/debug control plane 可用时，必须记录 root password、network interface 和用途边界；
- BSP/runtime APIs 仍是唯一支持的 application hardware interface。

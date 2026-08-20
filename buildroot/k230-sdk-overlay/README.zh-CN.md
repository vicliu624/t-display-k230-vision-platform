# K230 SDK Overlay

这个 overlay 会在配置前被复制到固定版本的 K230 Linux SDK 工作目录。它提供
镜像需要的板级配置和本地 package recipe。

## 当前板级 Profile

```text
configs/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig
```

## 本地 Package

| Package | 职责 |
| --- | --- |
| `gtk-layer-shell` | 为面板提供 layer-shell 协议支持。 |
| `sfwbar` | XDG 应用菜单、任务栏和时钟。 |
| `tdvp-labwc-desktop` | Labwc 会话、输出旋转、触摸校准、背景与面板启动。 |
| `tdvp-display-smoke` | DRM/KMS 显示验收工具。 |
| `tdvp-keyboard-layout` | T-Display K230 键盘布局服务。 |
| `tdvp-wayland-acceptance` | Wayland 会话验收工具。 |
| `tdvp-kpu-acceptance` | KPU runtime 验收工具。 |
| `vicliu-pocket-linux-hardware` | 板级硬件状态发布与集成服务。 |

`board/tdvp/` 包含 rootfs hook、Linux fragment、可重复镜像配置和镜像验证脚本。
`linux/` 包含受跟踪的 K230 内核补丁序列。

在准备工作目录时，
`buildroot/tools/register-k230-sdk-tdvp-packages.sh` 会注册这些 package。

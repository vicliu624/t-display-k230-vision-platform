# T-Display K230 的 Vicliu Pocket Linux

Vicliu Pocket Linux 为 LILYGO T-Display K230 V1.3 构建可启动的 Buildroot
镜像。镜像会在 K230 的内置屏幕上启动一套完整、标准的 Wayland 桌面。

## 桌面

```text
Linux DRM/KMS + libinput
        |
      seatd
        |
     Labwc
     |   |
     |   +-- Swaybg: 桌面背景
     +------ SFWBar: 应用菜单、任务栏、时钟
        |
      Foot: 镜像自带的终端
```

Labwc 管理 Wayland 合成器会话和窗口堆叠。SFWBar 按照 XDG 标准发现
`.desktop` 应用入口。软件包和本地程序只要在
`/usr/share/applications`、`/usr/local/share/applications` 或用户自己的
XDG 应用目录中安装 desktop entry，就会出现在桌面应用菜单里。

## 板级基线

该 profile 包含 K230 Linux SDK 内核，以及 RM69A10 DSI 屏幕、GT9895
触摸、键盘扩展模块、RTL8189FS Wi-Fi、RTL8152 USB 网卡、摄像头/ISP
runtime、音频工具、GPIO/I2C 工具、KPU runtime 组件和板级硬件状态服务。
图形会话使用已验证的 DRM 输出 `DSI-1`、旋转 `90` 和逻辑桌面尺寸
`1232x568`。

## 构建

请在 WSL 或 Linux 的 ext4 工作目录中执行：

```sh
bash buildroot/tools/prepare-k230-sdk-worktree.sh "$HOME/work/tdvp-k230-labwc"
bash buildroot/tools/build-k230-sdk-rm69a10.sh "$HOME/work/tdvp-k230-labwc"
```

镜像输出位置：

```text
$HOME/work/tdvp-k230-labwc/output/k230_canmv_t_display_rm69a10_labwc_desktop_defconfig/images/sysimage-sdcard.img
```

构建完成后可收集 release bundle：

```sh
bash buildroot/tools/collect-release-bundle.sh \
  "$HOME/work/tdvp-k230-labwc" \
  vicliu-pocket-linux-k230-candidate
```

继续阅读[快速开始](docs/getting_started.zh-CN.md)、
[系统架构](docs/architecture.zh-CN.md)和
[发布契约](docs/release-contract.zh-CN.md)。

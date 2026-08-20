# 系统架构

## 运行层级

```text
T-Display K230 V1.3 硬件
  |
K230 Linux 内核、设备树、firmware runtime
  |
DRM/KMS、DSI、input、I2C、SDIO、USB、ALSA、camera/ISP、KPU runtime
  |
systemd、udev、systemd-networkd、OpenSSH、seatd、D-Bus
  |
Labwc Wayland 合成器
  |-- Swaybg 桌面背景
  |-- SFWBar 应用菜单、任务栏、时钟
  `-- Foot 等标准 Wayland 应用
```

## 会话

`tdvp-labwc-desktop.service` 以桌面用户 `tdvp` 运行。它创建私有 runtime
目录，获得 `seat`、`video`、`input`、`render` 设备访问组，加载 K230 环境契约，
并在 D-Bus session 中启动 Labwc。

会话契约保存在 `/etc/tdvp/labwc/environment`：

| 设置 | 值 | 用途 |
| --- | --- | --- |
| `WLR_DRM_DEVICES` | `/dev/dri/card0` | K230 DRM 设备。 |
| `WLR_RENDERER` | `pixman` | 已验证的 K230 渲染路径。 |
| `LIBSEAT_BACKEND` | `seatd` | DRM/input 设备访问。 |
| `TDVP_K230_OUTPUT` | `DSI-1` | 内置 DSI 连接器。 |
| `TDVP_K230_OUTPUT_TRANSFORM` | `90` | 物理面板方向。 |
| logical size | `1232x568` | Wayland 桌面坐标空间。 |

Labwc 获取 DRM backend 后会处理 XDG autostart 文件。该文件应用输出旋转，并启动
Swaybg 和 SFWBar。

## 应用模型

应用使用标准 Wayland 协议和 XDG desktop-entry 格式。SFWBar 从 XDG data 目录扫描
`.desktop` 文件，并按其中的 `Exec` 命令启动应用。任务栏跟踪合成器提供的窗口。Foot
是镜像内置的终端入口。

## 板级集成

`vicliu-pocket-linux-hardware` 服务发布板级硬件状态，并执行当前 profile 使用的集成
步骤。vendor ISP 启动脚本保持启用，供摄像头 runtime 使用。键盘布局、触摸校准、显示
验收和 KPU 验收以独立的系统工具与服务方式安装。

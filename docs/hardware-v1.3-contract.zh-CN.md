# T-Display K230 V1.3 硬件契约

Labwc 桌面 profile 使用固定 K230 Linux SDK 中的 T-Display K230 V1.3 板级配置。
硬件集成通过标准 Linux 内核子系统和用户态接口暴露。

| 板级功能 | Linux 接口 | 镜像组件 |
| --- | --- | --- |
| RM69A10 内置屏幕 | DRM/KMS `DSI-1`、`/dev/dri/card0` | Canaan DRM、Labwc 会话 |
| GT9895 触摸 | Linux input event、libinput | 内核 touch fragment、`70-tdvp-touch.rules` |
| 键盘扩展模块 | Linux input event | `tdvp-keyboard-layout.service` |
| 键盘扩展 I2C 总线 | `/dev/i2c-*` | K230 keyboard/hardware fragment |
| RTL8189FS Wi-Fi | `wlan0`、NetworkManager / `nmcli` | RTL8189FS 软件包与 NetworkManager |
| RTL8152 USB 网卡 | `enu1`、NetworkManager / `nmcli` | 内核 r8152 驱动与 NetworkManager |
| 摄像头与 ISP | V4L2/media 节点和 vendor ISP 服务 | `vvcam` package 和 vendor 服务 |
| 音频录制/播放 | ALSA 设备 | ALSA 工具 |
| GPIO 与 I2C 诊断 | GPIO character device、`/dev/i2c-*` | libgpiod 工具和 i2c-tools |
| KPU 与 AI2D | K230 runtime 设备和 nncase 资源 | `libnncase`、`ai2d-kpu`、验收工具 |

可拆卸键盘扩展模块在同一块物理硬件上提供键盘、背光、BQ25896 充电管理、BQ27220
电量计，以及可选 nRF9151 LTE-M/GNSS 硬件。它的 I2C 总线使用 IO32 SCL 和 IO33 SDA。
V1.3 profile 保留 K256-04 和 K256-04-A 两个变体所需的板级连线。K256-04-A 设备不会
枚举 nRF9151。

镜像中的板级集成服务设置键盘背光，并通过标准设备文件和服务状态发布硬件状态。

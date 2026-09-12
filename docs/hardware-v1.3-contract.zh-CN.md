# T-Display K230 V1.3 硬件契约

当前 profile 使用 CPU0 Linux + CPU1 RT-Smart 的 AMP 分工。
接口是否存在、驱动是否绑定、实际功能是否通过测试，需要分别记录。

| 功能 | 资源归属与接口 | 验收范围 |
| --- | --- | --- |
| RM69A10 屏幕、VGLite | CPU0，DRM/KMS、VGLite、Labwc | 登录页和桌面均使用 VGLite |
| GT9895 触摸、键盘 | CPU0，Linux input/libinput | 按键、Fn、Menu、触摸与唤醒 |
| 键盘背光 | CPU0，IO52 / PWM4，板级服务与 Quick Settings | 关闭、低亮、高亮的实物变化 |
| RTL8189FS Wi-Fi | CPU0，NetworkManager | 扫描、连接、重连 |
| RTL8152 USB 网卡 | CPU0，r8152、NetworkManager | 接入后按实际接口名检查 |
| GC2093、VICAP/ISP、视觉缓冲区 | CPU1；Linux `/dev/tdvp-vision` | 真实帧、序号、尺寸、超时与生命周期 |
| KPU、AI2D、FFT、相关 AI 内存 | CPU1；Linux `/dev/tdvp-ai` | 支持作业的数值与错误恢复 |
| 音频采集/播放 | CPU0，ALSA/ASoC | 声卡、输入输出和扬声器 |
| 电源、充电、电量及传感器 | Linux 已绑定驱动、sysfs、板级状态服务 | 结合实际装配检查 |
| nRF52840 | 独立固件，K230 UART AT 主机工具 | UART 身份与 BLE 功能尚待实机打通 |
| LoRa | Linux 传输与板级控制 | 枚举/电源状态与 RF 收发分别验收 |

Linux profile 停用直接占有摄像头/ISP、GNNE/KPU 和 AI2D 的旧链路。
`tdvp-vision` 与 `tdvp-ai` 是跨核接口；Linux 的 CPU 数量预期为 1。

扬声器 I2S 路由使用 IO32 BCLK、IO33 LRCK、IO35 DATA，GPIO34 由 ASoC 控制。
键盘背光使用 IO52 的 PWM，默认档位为 0%、33%、100%。查引脚时应以当前板级
DTS、补丁与硬件版本共同核对，避免把旧版键盘扩展总线描述套用到音频引脚上。

扩展模块的传感器和可选 nRF9151 取决于实物型号。nRF9151 与 nRF52840 分开管理。
nRF52840 的官方 AT 应用尚未映射成 BlueZ HCI 控制器，顶部栏蓝牙缺项仍是未完成的集成。

操作与证据要求见 [基线验证](hardware-baseline-validation.zh-CN.md) 和
[V1.3 验收清单](hardware-v1.3-acceptance.zh-CN.md)。

# 系统架构

## CPU 与硬件归属

当前镜像采用 AMP 分工：Linux 运行在 CPU0，RT-Smart 运行在 CPU1。

```text
CPU0 / Linux                         CPU1 / RT-Smart
  登录、应用、网络、音频                GC2093 → VICAP / ISP → 视觉缓冲区
  greetd / gtkgreet                    AI2D、FFT、nncase / KPU、任务处理
  Labwc / VGLite → DRM/KMS 屏幕          AI 内存与加速器资源
        |                                      |
        +---- /dev/tdvp-vision：帧读取 ----------+
        +---- /dev/tdvp-ai：write/poll/read -----+
```

CPU1 管理摄像头和 AI 子系统对应的寄存器、中断、内存及资源生命周期。
CPU0 保留显示、VGLite、输入、网络和音频驱动。Linux 应用通过受控异步桥接使用 CPU1；
Linux 调度器可见一个 CPU。Linux 用户态软件需要使用 CPU0 支持的标量指令集。
系统 SDMA 仍归 Linux；当前 CPU1 FFT 通过 PIO 工作。AI 资源归属以配对设备树和
所有权声明为准，不能扩展为 CPU1 任意操作共享时钟、电源或 DMA 控制器。

`/dev/tdvp-vision` 提供 CPU1 采集的帧；`/dev/tdvp-ai` 提供有界 AI 任务。
当前支持有限的 AI2D 操作、FFT/IFFT 和固定 KWS 参考模型。完整视觉应用、任意模型接口
和语音转文字属于后续开发范围。普通用户通过 video 组访问两个节点。
旧 Linux VVCAM/ISP/GNNE/AI2D 生产路径已退出当前 profile。

只读状态分别位于 `/sys/class/misc/tdvp-vision/status` 和
`/sys/class/misc/tdvp-ai/status`。`vpl-hwctl`、硬件 daemon 和 Quick Settings
共用状态发布逻辑；状态中的可用性、任务计数和验收结果各自有独立含义。
详见 [AI 任务接口](cpu1-ai-jobs.zh-CN.md)和
[AI 状态说明](cpu1-ai-status-remote-validation-20260909.zh-CN.md)。

## 登录、桌面与锁屏

greetd 以专用 greeter 用户启动 gtkgreet，认证后为所选 Linux 账户启动 Labwc 会话。
会话目录从该账户派生。登录页与用户桌面均指定 `WLR_RENDERER=vglite`。
用户桌面通过 renderer policy 和故障标记检查后启动；GPU 异常需要诊断和显式恢复。

Labwc 负责窗口、工作区及合成，PCManFM 提供壁纸、图标和文件管理，
wf-panel-pi 提供顶部栏。Foot 是终端，nm-connection-editor 负责网络连接编辑。
当前逻辑桌面为 1232×568。菜单键打开应用菜单，Fn 使用 XKB Mod5 层。
桌面空白处长按打开右键菜单。

swayidle 默认在空闲 300 秒时调用 `tdvp-session-lock`，由 gtklock 显示密码窗口；
空闲到 330 秒时，wlopm 关闭输出。唤醒后在原会话解锁。
PAM 的 `unix_chkpwd` helper 以 root:root 4755 安装，其余图形程序使用普通用户权限。
详见[登录与锁屏](session-login-and-lock.zh-CN.md)。

## 网络、音频与板载无线模块

NetworkManager 管理 Wi-Fi 和有线连接，按需通过 D-Bus 使用 wpa_supplicant。
PulseAudio、ALSA 与音量插件处理音频，外放 GPIO 由 ASoC 驱动管理。

nRF52840 是独立的板载可编程协处理器。当前 Linux 客户端按官方 UART AT 协议工作；
桌面 Bluetooth 后端使用 BlueZ/HCI，两者尚待接口整合。实机 UART 身份和 BLE 功能仍待验证，
供电与固件升级流程也需单独完成。详见[nRF 集成状态](nrf52840-at-host.zh-CN.md)。
LoRa 控制和状态已接入硬件服务，射频收发需实测。

## 存储与软件包

镜像的 GPT 文件系统分区为 boot 1、rootfs 2，启动 payload 和 CPU1 固件位于固定 raw 区域。
U-Boot 按 root PARTUUID 启动。大卡首次启动可扩展 rootfs，并保留已有后续用户分区。

`tdvp-opkg` 在被调用时导入并校验内置公钥，随后运行 opkg；启动过程无需访问软件源。
设备配置的是可更新的 `stable` 频道。2026-09-09 的 r6 安装验收暴露了 CPU 指令集和
基础库替换问题，包安装与升级暂缓。详见[软件源状态](package-feed-status.zh-CN.md)。

# T-Display K230 的 Vicliu Pocket Linux

[English](README.md)

Vicliu Pocket Linux 是面向 LILYGO T-Display K230 V1.3 键盘掌机的 Buildroot 系统。
当前集成分支为 `codex/cpu1-rtsmart-integration`，通过 PR #1 构建候选镜像。

## 系统分工

| 处理器 | 负责的资源与工作 |
| --- | --- |
| CPU0 / Linux | 登录和应用、Wayland 桌面、VGLite 合成、屏幕、输入、网络、音频采集与播放 |
| CPU1 / RT-Smart | GC2093、摄像头采集链路、视觉缓冲区、KPU、AI2D、FFT、AI 内存与任务处理 |
| Linux 应用接口 | `/dev/tdvp-vision` 接收帧；`/dev/tdvp-ai` 异步提交 AI 任务、读取结果 |

Linux 调度器使用 CPU0，系统中看到一个 Linux CPU 符合当前设计。
CPU1 的工作通过跨核接口提交。Linux 软件包需要兼容 CPU0 的标量指令集。

CPU1 已接入摄像头采集、有限的 AI2D 操作、FFT/IFFT 和固定 KWS 参考模型。
已有实机验证记录覆盖这些路径；任意模型加载、完整视觉应用和语音转文字仍需开发。
详见[架构](docs/architecture.zh-CN.md)、[AI 任务接口](docs/cpu1-ai-jobs.zh-CN.md)和
[2026-09-09 实机记录](docs/cpu1-ai-status-remote-validation-20260909.zh-CN.md)。

## 桌面与登录

- greetd/gtkgreet 提供图形登录，会话使用认证账户的 home 和 runtime 目录。
- 登录页和用户桌面均使用 VGLite。用户桌面要求有效的镜像策略和已清除的 GPU 故障标记；
  条件不满足时会停止启动，等待诊断。
- Labwc 管理窗口，PCManFM 提供桌面和文件管理，wf-panel-pi 提供顶部栏。
  顶部栏显示应用菜单、网络、音量、电量和时间。
- 默认空闲 300 秒显示 gtklock 密码窗口，再过 30 秒关闭屏幕输出。
  按键唤醒后输入当前账户密码解锁；应用会话保持运行。
- LilyGO/Menu 键打开应用菜单，Fn 输入键帽上的黄色字符，`Alt+F4` 关闭当前应用。
- 镜像提供 Foot、PCManFM 和 nm-connection-editor。通用浏览器与 Camera 演示入口未预装。

锁屏细节见[登录页、锁屏和息屏](docs/session-login-and-lock.zh-CN.md)。

## 当前交付状态

截至 2026-09-09，CPU1 AI/摄像头、VGLite 和键盘背光已有指定镜像或热部署的验证记录。
每份记录的提交号、镜像名和 boot ID 界定其适用范围。新候选仍需完成整卡启动和硬件复查。

nRF52840 的 Linux AT 客户端已实现并通过协议回放测试。真实 UART 对端身份、
BLE 收发和桌面蓝牙接入仍未完成；目前没有刷写 nRF 固件。
LoRa 已有控制与状态接口，射频收发尚待验收。
详见[nRF52840 集成状态](docs/nrf52840-at-host.zh-CN.md)。

**软件源验收未通过，暂缓在交付卡上安装或升级软件包。**
2026-09-09 安装 NetSurf 依赖时，r6 中的 RVV 运行库覆盖了 CPU0 基础库，
随后系统在 libmount 内触发非法指令并停止启动。下一轮测试将以新烧录镜像为基线。
配置、证据和后续验收要求见[软件源状态](docs/package-feed-status.zh-CN.md)。

## 网络与外放

NetworkManager 管理 Wi-Fi 和有线连接。顶部栏的 **Edit Connections** 打开
`nm-connection-editor`；Wi-Fi 使用 D-Bus wpa_supplicant 后端。

外接 MAX98357A 兼容功放使用现有 `K230_I2S_INNO` ALSA 声卡。
IO32、IO33、IO35 分别承载 BCLK、LRCK、data-out；ASoC 驱动控制 GPIO34 关断线。
人工路由工具为 `tdvp-audio-route external|internal`。外放验收需要实际听音：

```sh
sudo tdvp-speaker-acceptance status
sudo tdvp-speaker-acceptance test
# 听到双声道测试音后按 Ctrl-C，再记录人工确认：
sudo tdvp-speaker-acceptance confirm-audible
```

## 构建、下载与烧录

使用 Ubuntu 24.04 x86_64 原生系统或容器对齐 CI，SDK 输出放在 Linux ext4 文件系统上。
准备、构建和交付命令见[快速开始](docs/getting_started.zh-CN.md)和
[Buildroot 工作流](buildroot/README.zh-CN.md)。

PR 的 Actions 使用 GitHub 合并测试提交构建，产物名中的哈希可能与分支 HEAD 不同。
下载时核对运行对应的分支提交、产物 manifest 和 `SHA256SUMS`。
collector 将 `<release-name>.img.gz` 和配套文件写入仓库的 `output/<release-name>/`；
GitHub Actions 会上传该目录。

镜像的 GPT 文件系统分区为 boot 1 和 rootfs 2，另有固定偏移的启动与 CPU1 固件区域。
大容量卡首次启动可能因根分区扩展自动重启一次；root PARTUUID 保持不变，
带后续用户分区的卡会跳过自动扩分区。

## 登录后检查

串口为 `ttyS0`、115200 8N1。开发账户 `tdvp` 和恢复账户 `root` 的初始密码均为
`tdvp`；接入不可信网络前请修改密码。

```sh
systemctl --no-pager status greetd NetworkManager sshd vicliu-pocket-linux-hardware
nmcli device status
tdvp-renderer-profile status
vpl-hwctl status
cat /sys/class/misc/tdvp-vision/status
cat /sys/class/misc/tdvp-ai/status
```

只读状态检查和实际功能验收分别记录。完整要求见[发布契约](docs/release-contract.zh-CN.md)
与[硬件验证](docs/hardware-baseline-validation.zh-CN.md)。
